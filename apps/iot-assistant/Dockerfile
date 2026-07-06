# iot-assistant — CLI agent that drives the ESP32-S3 over its HTTP API.
#
# Pure Python, stdlib-only (no GPU): the LLM work happens on the cluster via
# HAProxy, the device work on the ESP32 over HTTP. A slim base is plenty.
# The firmware/ tree is NOT part of the image — it's flashed over USB (make flash).
FROM python:3.12-slim

# Deps first so code edits rebuild only the final layer (currently stdlib-only).
COPY requirements.txt /opt/iot-assistant/requirements.txt
RUN pip install --no-cache-dir -r /opt/iot-assistant/requirements.txt

COPY app/ /opt/iot-assistant/app/
WORKDIR /opt/iot-assistant

# One-shot CLI, not a server: pass the instruction as the container command.
#   docker compose run --rm agent "blink led 1 fast"
ENTRYPOINT ["python", "app/main.py"]
