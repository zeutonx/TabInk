# TabInk OCR sidecar.
#   docker build -t tabink-ocr recognition                          # service only, no engine
#   docker build -t tabink-ocr:texteller --build-arg ENGINE=texteller recognition
# ENGINE = base | texteller | pix2text | pix2tex  (each uses its own pinned lock file)

FROM python:3.12-slim AS runtime

ARG ENGINE=base
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    TABINK_OCR_ENGINE=${ENGINE} \
    HF_HOME=/models/huggingface \
    XDG_CACHE_HOME=/models/cache

# OpenCV (used by every engine) needs these shared libs even in headless builds.
RUN if [ "$ENGINE" != "base" ]; then \
      apt-get update && apt-get install -y --no-install-recommends libgl1 libglib2.0-0 \
      && rm -rf /var/lib/apt/lists/*; \
    fi

WORKDIR /app
COPY requirements/ requirements/
RUN pip install -r requirements/${ENGINE}.txt

COPY tabink_ocr/ tabink_ocr/

RUN useradd --create-home --uid 10001 tabink \
    && mkdir -p /models && chown tabink:tabink /models
USER tabink
VOLUME ["/models"]

EXPOSE 8765
HEALTHCHECK --interval=30s --timeout=3s --start-period=20s \
    CMD python -c "import urllib.request,sys; sys.exit(0 if urllib.request.urlopen('http://127.0.0.1:8765/health').status==200 else 1)"

# 0.0.0.0 is inside the container only; compose publishes it on 127.0.0.1 (never the LAN).
CMD ["uvicorn", "tabink_ocr.server:app", "--host", "0.0.0.0", "--port", "8765"]
