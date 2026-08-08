# image-gen — the fleet's image node

A headless image-generation service: POST a prompt, get PNG bytes. It is the
thing `IMAGE_BASE_URL` points at; the caller today is ecommerce-ai's
`backend/research/images.py` (product image generation, from a resolved CJ
product's title/description).

Like camera-vision (which runs on the Jetson), this runs **on its own box** — the
GPU node — not on the master. `edge deploy` deliberately skips nodes with
`role: "image"`, so nothing here is shipped or started by the fleet tooling. You
start it on the GPU box yourself.

## Why not ComfyUI (still, for now)

ComfyUI is already headless — the canvas is just a page you don't have to open.
The reason it's not used here is the payload, not the UI: `POST /prompt` takes a
full node-graph JSON and returns a job id you then poll and fetch. For one fixed
pipeline (product images), owning both ends of a synchronous
`prompt in → PNG out` contract is less code than templating a graph.

This condition is starting to flip: product-placement compositing (a real
product photo pasted into a generated scene), IC-Light relighting, and video
generation are all on the roadmap, and each is a branch — exactly the case
this section says to switch on. When that lands, ComfyUI runs behind a thin
FastAPI shim that keeps this same `POST /generate` contract, so callers don't
change.

## Model choice

Default is **FLUX.2 klein 4B** (`black-forest-labs/FLUX.2-klein-4B`),
Apache-2.0 and fully commercial — no self-host license fee, unlike FLUX.1-dev/
FLUX.2-dev (non-commercial by default, $999/mo to self-host commercially).
These images sell products, so licensing that's unambiguous at any scale beats
a bigger model with strings attached. Unlike FLUX.1-schnell's gated repo,
klein needs no HuggingFace terms-acceptance/token — it's plainly open.

klein is also a 4B *distilled* model (4 steps, like schnell was), so it's a
near drop-in on the same hardware: **~8.4GB VRAM at plain bf16** on a 12GB
card, no NF4 quantization needed (FLUX.1's 12B transformer needed NF4 to fit;
klein's 4B doesn't). `IMAGE_QUANT=nf4` stays available in `app/main.py`'s
FLUX.1 fallback path for anyone rolling back to a bigger "dev"-class model on
a smaller card.

Measured for the previous FLUX.1-schnell setup on an RTX 5070 Ti Laptop
(12,226 MiB, sm_120), 1024x1024, 4 steps — kept here as the reference point
until klein is measured on the same box:

| | |
|---|---|
| weights on disk | **32GB** (bf16 -- NF4 happens at load, so it saves VRAM, not disk) |
| load from cache | ~51s (573s the first time, including the download) |
| render, warm | **~18s** |
| render, first after load | ~27s (warmup) |
| peak VRAM during render | 8,406 MiB |
| idle VRAM | ~555 MiB (offload hands it back between calls) |
| system RAM, resident | ~12GB -- see the WSL note under "Running it" |

klein should land in the same neighborhood (same VRAM figure, no NF4 quantize
step to add latency) — re-measure once it's actually pulled and running.

18s, not the "seconds" a 4-step model suggests: CPU offload streams each stage
from system RAM per call, and that dominates. Four image prompts for one product
is ~75s of wall clock -- size timeouts accordingly.

Alternatives: **Qwen-Image-Edit-2511** (Apache-2.0) is worth a second endpoint
for hero shots where product-identity fidelity (logos, packaging text) is
paying rent — it's VRAM-heavier (GGUF Q4_K_M ~14GB, needs offload on this
card) and slower, so it's a "max fidelity" branch, not the default. **SDXL**
(~7GB, commercially permissive) is noticeably worse at prompt adherence and
text rendering — which matters for product labels — so it's a fallback, not
a first choice.

## Running it

Runs fine in WSL — CUDA passthrough works, and the Linux wheels for
`bitsandbytes` are far less trouble than the native-Windows ones.

```bash
cd apps/image-gen
python3 -m venv .venv && . .venv/bin/activate

# torch FIRST, and it must match the GPU arch. Blackwell (RTX 50xx) is sm_120
# and needs CUDA 12.8+; the default PyPI wheel has no kernels for it.
pip install torch --index-url https://download.pytorch.org/whl/cu128
pip install -r requirements.txt

cp .env.example .env          # then: set -a; . ./.env; set +a

# klein's repo is plainly open -- no terms-acceptance, no token needed. If you
# point IMAGE_MODEL at a gated repo instead (e.g. FLUX.1-schnell/dev), accept
# the terms on that model's page while signed in, mint a READ token at
# huggingface.co/settings/tokens, then either:
#   hf auth login                 # interactive, stores it in ~/.cache/huggingface
#   ...or put HF_TOKEN=hf_... in .env

python3 -m app.main
```

First start downloads the weights (klein is a 4B model — a fraction of
FLUX.1-schnell's 32GB; expect a much shorter cold start, re-measure once run).
Later starts load from cache. Keep the process resident — there is no
`keep_alive` equivalent, the model lives as long as the process does.

**Memory, if you run this in WSL:** quantizing bf16 shards to NF4 at load time
spikes *system* RAM, not just VRAM. On WSL's default allocation (~50% of host,
15GB here) that spike took down the entire VM — not just the process — and VS
Code lost its connection. Give WSL room in `%USERPROFILE%\.wslconfig`
(`memory=20GB`, `swap=16GB`) and `wsl --shutdown` to apply. The alternative is
avoiding the conversion entirely with a pre-quantized NF4 checkpoint, which also
cuts the download — at the cost of trusting a community re-upload.

```bash
curl localhost:8188/health
curl -X POST localhost:8188/generate -H 'content-type: application/json' \
  -d '{"prompt":"a ceramic mug on a walnut desk, soft window light","width":1024,"height":1024}' \
  -o out.png
```

## API

| | |
|---|---|
| `POST /generate` | `{prompt, width?, height?, steps?, seed?, guidance?}` → `image/png`. Response headers carry `X-Seed` (reproduce a result you liked), `X-Model`, `X-Duration-Ms`. |
| `GET /health` | model, quantization, device, free/total VRAM, whether the GPU is busy. Also exposes a `models[]` list so a generic fleet probe can treat it like a node. |

Generation is serialized behind one lock — the GPU is a single resource, and
ecommerce-ai already pins `concurrency: 1` for this host.

## Reachability

Bind is `0.0.0.0`, so during development on the same machine it's just
`IMAGE_BASE_URL=http://localhost:8188`.

Reaching it from *another* box (e.g. the master running ecommerce-ai) while it
lives in WSL needs one of:

- **`networkingMode=mirrored`** in `%USERPROFILE%\.wslconfig` (Windows 11 22H2+),
  which gives WSL the host's LAN interfaces directly. Cleanest, but it changes
  networking for every distro and has known friction with some VPNs and Docker
  bridge setups — turn it on deliberately, not mid-debug.
- **`netsh interface portproxy`** plus a firewall rule. Works without changing
  global behavior, but WSL's NAT address moves across reboots, so it needs a
  startup script.

Or run it natively on Windows, where the `bitsandbytes` NF4 path is rougher and
a GGUF quant is the better route.

## Fleet wiring

In `fleet.json`, the GPU box is a node with `role: "image"`:

```json
{ "name": "gpu", "host": "192.168.1.x", "ssh": "interop", "os": "windows",
  "role": "image", "image_port": 8188, "models": ["black-forest-labs/FLUX.2-klein-4B"] }
```

`edge` derives `IMAGE_BASE_URL` from `host` + `image_port`, and `IMAGE_MODEL`
from the **first** entry in `models`. `edge model set <id> --node gpu` promotes a
model to the front without pulling anything (there's no Ollama here — the weights
are whatever this service downloads).
