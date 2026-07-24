# image-gen — the fleet's image node

A headless image-generation service: POST a prompt, get PNG bytes. It is the
thing `IMAGE_BASE_URL` points at, and the only caller today is storefront-ai's
imagery agent.

Like camera-vision (which runs on the Jetson), this runs **on its own box** — the
GPU node — not on the master. `edge deploy` deliberately skips nodes with
`role: "image"`, so nothing here is shipped or started by the fleet tooling. You
start it on the GPU box yourself.

## Why not ComfyUI

ComfyUI is already headless — the canvas is just a page you don't have to open.
The reason it's not used here is the payload, not the UI: `POST /prompt` takes a
full node-graph JSON and returns a job id you then poll and fetch. For one fixed
pipeline (product mockups), owning both ends of a synchronous
`prompt in → PNG out` contract is less code than templating a graph.

Switch to ComfyUI when the pipeline branches — ControlNet, inpainting, upscale
chains for print files. At that point this service is the wrong shape and
re-implementing it would be worse than adopting the graph format.

## Model choice

Default is **FLUX.1-schnell**, and the deciding factor is licensing:
FLUX.1-schnell is Apache-2.0 while **FLUX.1-dev is non-commercial**. These images
sell products. Verify the license text yourself before switching to dev.

Note that Apache-2.0 describes the *license*, not the *access*: the weights are
behind a gated repo. You need a HuggingFace account that has accepted the terms
on the model page and a read token, or startup fails with a 401
`GatedRepoError`. See "Running it" below.

VRAM, on a 12GB card: the 12B transformer is ~24GB at bf16, so it is loaded at
**NF4** along with the T5-XXL text encoder, and `enable_model_cpu_offload()`
keeps only the executing stage resident. On a card with 24GB+, set
`IMAGE_QUANT=bf16`.

Measured on an RTX 5070 Ti Laptop (12,226 MiB, sm_120), 1024x1024, 4 steps:

| | |
|---|---|
| weights on disk | **32GB** (bf16 -- NF4 happens at load, so it saves VRAM, not disk) |
| load from cache | ~51s (573s the first time, including the download) |
| render, warm | **~18s** |
| render, first after load | ~27s (warmup) |
| peak VRAM during render | 8,406 MiB |
| idle VRAM | ~555 MiB (offload hands it back between calls) |
| system RAM, resident | ~12GB -- see the WSL note under "Running it" |

18s, not the "seconds" a 4-step model suggests: CPU offload streams each stage
from system RAM per call, and that dominates. Four image prompts for one product
is ~75s of wall clock -- size timeouts accordingly.

Alternatives if FLUX misbehaves: **SDXL** (~7GB, commercially permissive, but
noticeably worse at prompt adherence and text rendering — which matters for
product labels), or **SD 3.5 Medium** (fits, but its community license has a
revenue threshold).

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

# One-time: the weights are gated. Accept the terms on the model page while
# signed in (huggingface.co/black-forest-labs/FLUX.1-schnell), mint a READ token
# at huggingface.co/settings/tokens, then either:
hf auth login                 # interactive, stores it in ~/.cache/huggingface
#   ...or put HF_TOKEN=hf_... in .env

python3 -m app.main
```

First start downloads **32GB** of weights; measured cold start including that
download was 573s. Later starts load from cache. Keep the process resident —
there is no `keep_alive` equivalent, the model lives as long as the process does.

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
storefront-ai's router already pins `concurrency: 1` for this host.

## Reachability

Bind is `0.0.0.0`, so during development on the same machine it's just
`IMAGE_BASE_URL=http://localhost:8188`.

Reaching it from *another* box (e.g. the master running storefront-ai) while it
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
  "role": "image", "image_port": 8188, "models": ["black-forest-labs/FLUX.1-schnell"] }
```

`edge` derives `IMAGE_BASE_URL` from `host` + `image_port`, and `IMAGE_MODEL`
from the **first** entry in `models`. `edge model set <id> --node gpu` promotes a
model to the front without pulling anything (there's no Ollama here — the weights
are whatever this service downloads).
