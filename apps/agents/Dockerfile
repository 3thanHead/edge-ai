# agents — LangChain agents served over REST/WS, run against the LLM cluster.
#
# Pure Python (no GPU): the LLM work happens on the cluster via HAProxy, the
# device work on the ESP32 over HTTP/MQTT. A slim base is plenty.
FROM python:3.12-slim

# Deps first so code edits rebuild only the final layer.
COPY requirements.txt /opt/agents/requirements.txt
RUN pip install --no-cache-dir -r /opt/agents/requirements.txt

COPY app/ /opt/agents/app/
WORKDIR /opt/agents

EXPOSE 8810
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8810"]
