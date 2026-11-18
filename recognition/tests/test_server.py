from fastapi.testclient import TestClient

from tabink_ocr import server

client = TestClient(server.app)


def test_health():
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json()["status"] == "ok"


def test_rejects_non_image():
    r = client.post("/v1/recognize", files={"image": ("x.txt", b"hi", "text/plain")})
    assert r.status_code == 415


def test_rejects_oversized_image(monkeypatch):
    monkeypatch.setattr(server, "MAX_IMAGE_BYTES", 10)
    r = client.post("/v1/recognize", files={"image": ("x.png", b"0" * 11, "image/png")})
    assert r.status_code == 413


def test_recognize_not_implemented_yet():
    r = client.post("/v1/recognize", files={"image": ("x.png", b"\x89PNG", "image/png")})
    assert r.status_code == 501
