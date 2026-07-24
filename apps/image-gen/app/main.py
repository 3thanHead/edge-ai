#!/usr/bin/env python3
"""Image-generation node -- the fleet's GPU box, headless.

One model held resident behind a small HTTP API: POST a prompt, get PNG bytes.
No web UI and no node graph -- this is deliberately not ComfyUI. The contract is
ours on both ends (storefront-ai's imagery agent is the only caller), so it stays
a synchronous request/response instead of submit-poll-fetch.

This is the service IMAGE_BASE_URL points at: a fleet.json node with
role "image" and image_port (default 8188). It runs ON the GPU box, not the
master -- `edge deploy` skips image nodes, the same way camera-vision runs on
the Jetson rather than being shipped to the master.

    IMAGE_MODEL=black-forest-labs/FLUX.1-schnell python3 -m app.main

Why FLUX.1-schnell and not dev: schnell is Apache-2.0 while FLUX.1-dev is
non-commercial, and these images sell products. It is also 4-step distilled, so
iteration is seconds rather than a minute.

Why NF4: the 12B transformer is ~24GB at bf16. NF4 puts the transformer near
6.5GB and the T5-XXL text encoder near 3GB; model CPU offload keeps only the
active stage resident, which fits a 12GB laptop card with room for the desktop
compositor. Set IMAGE_QUANT=bf16 on a big card to skip quantization entirely.

Env: IMAGE_MODEL, IMAGE_HOST (0.0.0.0), IMAGE_PORT (8188), IMAGE_STEPS,
     IMAGE_GUIDANCE, IMAGE_MAX_SEQ, IMAGE_QUANT (nf4|bf16), HF_HOME.
"""
import asyncio
import io
import logging
import os
import time
from contextlib import asynccontextmanager

import torch
from fastapi import FastAPI, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel, Field

log = logging.getLogger("image-gen")

MODEL_ID = os.environ.get("IMAGE_MODEL", "black-forest-labs/FLUX.1-schnell")
HOST = os.environ.get("IMAGE_HOST", "0.0.0.0")
PORT = int(os.environ.get("IMAGE_PORT", "8188"))
QUANT = os.environ.get("IMAGE_QUANT", "nf4").lower()
# schnell is guidance-distilled: 4 steps, guidance 0. dev wants ~28 steps and 3.5.
STEPS = int(os.environ.get("IMAGE_STEPS", "4"))
GUIDANCE = float(os.environ.get("IMAGE_GUIDANCE", "0.0"))
# T5 sequence cap: 256 for schnell, 512 for dev. Longer costs VRAM for nothing.
MAX_SEQ = int(os.environ.get("IMAGE_MAX_SEQ", "256"))

MAX_DIM = 1536          # a 12GB card starts thrashing past this
DIM_MULTIPLE = 16       # FLUX's VAE needs dimensions on a 16px grid

# The GPU is one resource: serialize generation regardless of how many callers
# arrive. storefront-ai's router already pins concurrency=1 per host; this is
# the same rule enforced on the side that actually owns the VRAM.
_GPU = asyncio.Lock()
_PIPE = None
_LOADED_AT = 0.0


def _load_pipeline():
    """Build the FLUX pipeline. Blocking and slow (weights -> VRAM); called once."""
    from diffusers import FluxPipeline

    t0 = time.time()
    log.info("loading %s (quant=%s)…", MODEL_ID, QUANT)
    kwargs = {"torch_dtype": torch.bfloat16}

    if QUANT == "nf4":
        # Quantize the two memory hogs -- the transformer and the T5 text encoder.
        # They come from different libraries, so they need each library's own
        # BitsAndBytesConfig; the rest of the pipeline stays bf16.
        from diffusers import BitsAndBytesConfig as DiffusersQuant
        from diffusers import FluxTransformer2DModel
        from transformers import BitsAndBytesConfig as TransformersQuant
        from transformers import T5EncoderModel

        nf4 = dict(load_in_4bit=True, bnb_4bit_quant_type="nf4",
                   bnb_4bit_compute_dtype=torch.bfloat16)
        kwargs["transformer"] = FluxTransformer2DModel.from_pretrained(
            MODEL_ID, subfolder="transformer",
            quantization_config=DiffusersQuant(**nf4), torch_dtype=torch.bfloat16)
        kwargs["text_encoder_2"] = T5EncoderModel.from_pretrained(
            MODEL_ID, subfolder="text_encoder_2",
            quantization_config=TransformersQuant(**nf4), torch_dtype=torch.bfloat16)

    pipe = FluxPipeline.from_pretrained(MODEL_ID, **kwargs)
    # Keep only the executing stage on the GPU (text encoder -> transformer -> VAE
    # in turn). Costs a little latency per call, buys the headroom that makes 12GB
    # workable. Do NOT also call .to("cuda") -- offload manages placement itself.
    pipe.enable_model_cpu_offload()
    pipe.set_progress_bar_config(disable=True)
    log.info("loaded in %.1fs", time.time() - t0)
    return pipe


@asynccontextmanager
async def lifespan(_: FastAPI):
    global _PIPE, _LOADED_AT
    if not torch.cuda.is_available():
        # Fail loudly at boot rather than 40s into the first request. On WSL this
        # almost always means a torch build without kernels for this GPU's arch.
        raise RuntimeError(
            "no CUDA device visible to torch. On Blackwell (sm_120) install a "
            "cu128+ build: pip install torch --index-url "
            "https://download.pytorch.org/whl/cu128")
    _PIPE = await asyncio.to_thread(_load_pipeline)
    _LOADED_AT = time.time()
    yield
    _PIPE = None


app = FastAPI(title="image-gen", lifespan=lifespan)


class GenerateRequest(BaseModel):
    prompt: str = Field(min_length=1)
    width: int = 1024
    height: int = 1024
    steps: int | None = None
    seed: int | None = None
    guidance: float | None = None


def _dim(v: int) -> int:
    """Clamp to the VAE's 16px grid and to what this card can hold."""
    v = max(DIM_MULTIPLE, min(MAX_DIM, int(v)))
    return v - (v % DIM_MULTIPLE)


def _render(req: GenerateRequest, seed: int) -> bytes:
    gen = torch.Generator(device="cpu").manual_seed(seed)
    image = _PIPE(
        prompt=req.prompt,
        width=_dim(req.width), height=_dim(req.height),
        num_inference_steps=req.steps or STEPS,
        guidance_scale=GUIDANCE if req.guidance is None else req.guidance,
        max_sequence_length=MAX_SEQ,
        generator=gen,
    ).images[0]
    buf = io.BytesIO()
    image.save(buf, format="PNG")
    return buf.getvalue()


@app.post("/generate")
async def generate(req: GenerateRequest):
    """One image, synchronously. PNG bytes out; the seed comes back in a header
    so a caller that likes a result can reproduce it exactly."""
    if _PIPE is None:
        raise HTTPException(503, "model still loading")
    seed = req.seed if req.seed is not None else int.from_bytes(os.urandom(4), "big")
    async with _GPU:
        t0 = time.time()
        try:
            png = await asyncio.to_thread(_render, req, seed)
        except torch.cuda.OutOfMemoryError:
            torch.cuda.empty_cache()
            raise HTTPException(507, "GPU out of memory -- ask for a smaller image")
        took = time.time() - t0
    log.info("generated %dx%d in %.1fs (seed=%d)", req.width, req.height, took, seed)
    return Response(content=png, media_type="image/png",
                    headers={"X-Seed": str(seed), "X-Model": MODEL_ID,
                             "X-Duration-Ms": str(int(took * 1000))})


@app.get("/health")
async def health():
    """What the fleet asks. Mirrors the shape of Ollama's /api/tags closely
    enough that a router can treat 200 as 'this node is usable'."""
    free, total = (torch.cuda.mem_get_info() if torch.cuda.is_available() else (0, 0))
    return {
        "status": "ok" if _PIPE is not None else "loading",
        "model": MODEL_ID,
        "models": [{"name": MODEL_ID}],      # /api/tags-ish, for generic probes
        "quant": QUANT,
        "device": torch.cuda.get_device_name(0) if torch.cuda.is_available() else "cpu",
        "vram_free_mb": free // (1024 * 1024),
        "vram_total_mb": total // (1024 * 1024),
        "busy": _GPU.locked(),
        "uptime_s": int(time.time() - _LOADED_AT) if _LOADED_AT else 0,
    }


if __name__ == "__main__":
    import uvicorn

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    uvicorn.run(app, host=HOST, port=PORT)
