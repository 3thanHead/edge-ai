# Linux LLM node (e.g. Jetson Orin Nano Super, 192.168.1.11)

Native Ollama on a systemd Linux box, exposed to the LAN, serving the shared model.
Works on any systemd Linux; on a Jetson it uses the CUDA GPU automatically.

```bash
bash setup.sh
# or, from the repo root on this machine:  ./edge install-node
```

What it does:
1. Installs Ollama (native build, GPU-enabled where available).
2. Adds a systemd override so Ollama binds `0.0.0.0:11434` (LAN-reachable, not just localhost).
3. Pulls the model `fleet.json` names (passed in as `LLM_MODEL`; no default).
4. Self-checks `/api/tags`.

Verify from another machine:
```bash
curl http://192.168.1.11:11434/api/tags
```

> ⚠️ On an 8GB Jetson the camera app's stack (YOLOv8m + the moondream VLM on Ollama)
> already uses most of the ~7.3 GiB usable, so the same box can't also serve a cluster
> text model comfortably. If this Jetson runs the camera app, use it as an LLM node only
> while that app is stopped — or keep the camera and LLM roles on separate boxes.
