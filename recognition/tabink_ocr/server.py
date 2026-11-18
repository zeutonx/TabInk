"""Loopback-only OCR sidecar API.

Placeholder until v1: exposes the contract and limits, returns 501 for recognition.
Run: uvicorn tabink_ocr.server:app --host 127.0.0.1 --port 8765
"""

import os

from fastapi import FastAPI, HTTPException, UploadFile

from . import __version__

ENGINE = os.environ.get("TABINK_OCR_ENGINE", "none")
MAX_IMAGE_BYTES = int(os.environ.get("TABINK_OCR_MAX_IMAGE_BYTES", str(8 * 1024 * 1024)))
ALLOWED_TYPES = {"image/png", "image/jpeg"}

app = FastAPI(title="TabInk OCR", version=__version__, docs_url=None, redoc_url=None)


@app.get("/health")
def health() -> dict:
    return {"status": "ok", "version": __version__, "engine": ENGINE}


@app.post("/v1/recognize")
async def recognize(image: UploadFile) -> dict:
    if image.content_type not in ALLOWED_TYPES:
        raise HTTPException(status_code=415, detail="image must be PNG or JPEG")
    data = await image.read(MAX_IMAGE_BYTES + 1)
    if len(data) > MAX_IMAGE_BYTES:
        raise HTTPException(status_code=413, detail="image too large")
    raise HTTPException(status_code=501, detail=f"engine '{ENGINE}' not implemented yet")
