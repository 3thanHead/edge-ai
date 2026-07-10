# camera-vision — API

Two HTTP surfaces: the **gateway** (the Python app in `app/`, port 8000) and
the **camera firmware** (the ESP32, port 80).

## Gateway (`http://<gateway>:8000`)

### `GET /`
The live viewer (static page over the endpoints below).

### `GET /stream`
Annotated video as multipart MJPEG (`multipart/x-mixed-replace`) — the
camera's stream with YOLO boxes drawn in, throttled to the gateway's target
FPS. Point an `<img>` or VLC at it.

### `GET /snapshot`
Latest annotated frame as a single JPEG. `503` while the camera isn't ready.

### `GET /detections`
Current YOLO detections.

```json
{"objects": [{"label": "person", "conf": 0.87, ...}]}
```

### `GET /description`
Latest automatic VLM narration (refreshed every `VLM_INTERVAL` seconds).

```json
{"description": "a person holding a mug", "available": true}
```

### `POST /describe`
On-demand narration of the current raw frame (same engine as the periodic
loop). No body.

```json
{"description": "..."}
```
Errors: `503` camera not ready, `502` VLM unavailable (Ollama down / model not
pulled).

### `GET /config`
The gateway's effective configuration.

```json
{"esp32_host": "...", "detect": true, "model": "yolov8m-oiv7.pt",
 "classes": "all", "vlm": "moondream", "vlm_interval": 8}
```

## Camera firmware (`http://<esp32>:80`)

| endpoint | returns |
|---|---|
| `GET /stream` | raw multipart MJPEG straight off the OV2640 |
| `GET /snapshot` | single JPEG |

The gateway consumes `/stream`; both are also usable directly for debugging.
