# recognition/ — Handwriting-to-LaTeX sidecar (v1)

Runs on Windows (natively or in Docker), not on the tablet. Design: [architecture.md §14](../docs/architecture.md#14-handwriting-to-latex-in-v1).

**Status:** service stub only. `GET /health` works. `POST /v1/recognize` checks the upload type and size, then returns `501`. No engine is wired in yet.

## Layout

| Path | Contents |
|---|---|
| `tabink_ocr/` | FastAPI service. Bind to `127.0.0.1` only. |
| `tests/` | pytest |
| `requirements/*.in` | Hand-edited top-level dependencies |
| `requirements/*.txt` | Fully pinned lock files generated from the `.in` files. Don't edit by hand. |
| `requirements.txt` | Default install = TexTeller lock |
| `benchmarks/` | 100-equation set + results CSV (later) |
| `Dockerfile` | `ENGINE` build arg selects the lock file |

## Requirement sets — one environment per engine

The engines pin incompatible versions of the same libraries, so **never install two engines into one venv**:

| Set | Engine | Key pins (Python 3.12) |
|---|---|---|
| `base` | none (service only) | fastapi, uvicorn, pillow |
| `dev` | none + test tools | base + pytest, httpx2 |
| `texteller` | TexTeller 1.0.2 (primary) | `transformers==4.47.0`, torch 2.15 CPU, ray[serve], streamlit |
| `pix2text` | Pix2Text 1.1.7 (baseline) | `transformers==4.57.6`, cnocr, doclayout-yolo |
| `pix2tex` | pix2tex 0.1.4 (baseline) | `timm==0.5.4`, `x-transformers==0.15.0`, `transformers==5.23.0` |

Locks are universal (Windows, Linux, macOS) and use the PyTorch **CPU** wheel index. A GPU build will need its own lock against a CUDA index.

## Common commands (from `recognition/`)

Native venv for the primary engine:

```bash
py -3.12 -m venv .venv-texteller
```

```bash
.venv-texteller\Scripts\pip install -r requirements/texteller.txt
```

Run the tests:

```bash
python -m pytest -q
```

Regenerate a lock file after editing a `.in` file (needs [uv](https://docs.astral.sh/uv/)):

```bash
uv pip compile requirements/texteller.in -o requirements/texteller.txt --universal --python-version 3.12 --extra-index-url https://download.pytorch.org/whl/cpu --index-strategy unsafe-best-match --emit-index-url
```

## Licenses to check before shipping

TexTeller (Apache-2.0 code), Pix2Text (MIT) and pix2tex (MIT) have permissive code licenses. **Model weights may have separate terms.** Check each one before redistributing an image with weights baked in.
