"""Summarise a penlogger CSV into the P0.2 results table. Prints statistics, never coordinates."""

import csv
import math
import sys

PEN = {"stylus", "eraser"}
CONTACT = {"DOWN", "MOVE", "POINTER_DOWN", "UP", "POINTER_UP"}
HOVER = {"HOVER_ENTER", "HOVER_MOVE", "HOVER_EXIT"}
STARTS = {"DOWN", "POINTER_DOWN"}
ENDS = {"UP", "POINTER_UP", "CANCEL"}
BUTTONS = {1: "PRIMARY", 2: "SECONDARY", 32: "STYLUS_PRIMARY", 64: "STYLUS_SECONDARY"}


def percentile(values, p):
    """Nearest rank, as in SampleStats.kt. None if empty."""
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s), max(1, math.ceil(p / 100 * len(s)))) - 1]


def analyze(lines):
    meta, body = [], []
    for line in lines:
        (meta if line.startswith("#") else body).append(line)
    r = dict(meta=[m[1:].strip() for m in meta], samples=0, historical=0, contact=[], hover=[], lag=[],
             batches={}, pressures=[], distances=[], backwards=0, buttons=0, presses=0, strokes=0,
             canceled=0, lost_ups=0, open_at_end=False, touch_during_pen=0, tools={}, bad_rows=0,
             unbuffered=set())
    last_any = last_contact = last_hover = None
    down = False
    for row in csv.DictReader(body):
        try:
            t, recv = int(row["sample_time_ns"]), int(row["receive_time_ns"])
            seq, button = int(row["event_seq"]), int(row["button_state"])
            pressure, distance = float(row["pressure"]), float(row["distance"])
            tool, action, hist = row["tool"], row["action"], row["source"] == "H"
            transition = not hist and row["action_index"] == row["pointer_index"]
            unbuffered = row["unbuffered"]
        except (KeyError, TypeError, ValueError):
            r["bad_rows"] += 1
            continue
        r["tools"][tool] = r["tools"].get(tool, 0) + 1
        if tool not in PEN:
            r["touch_during_pen"] += down
            continue
        r["samples"] += 1
        r["historical"] += hist
        r["unbuffered"].add(unbuffered)
        r["batches"][seq] = r["batches"].get(seq, 0) + 1
        if not hist:
            r["lag"].append(recv - t)
        r["buttons"] |= button
        r["presses"] += action == "BUTTON_PRESS" and not hist
        if last_any is not None and t < last_any:
            r["backwards"] += 1
        last_any = t if last_any is None else max(last_any, t)

        if transition and action in STARTS:
            r["lost_ups"] += down
            r["strokes"] += 1
            down, last_contact, last_hover = True, None, None
        if transition and action == "HOVER_ENTER":
            last_hover = None
        if action in CONTACT:
            if last_contact is not None:
                r["contact"].append(t - last_contact)
            last_contact = t
            if pressure > 0:
                r["pressures"].append(pressure)
        elif action in HOVER:
            if last_hover is not None:
                r["hover"].append(t - last_hover)
            last_hover = t
            r["distances"].append(distance)
        if transition and action in ENDS:
            r["canceled"] += action == "CANCEL"
            down, last_contact = False, None
        if transition and action == "HOVER_EXIT":
            last_hover = None
    r["open_at_end"] = down
    return r


def report(r):
    def ms(v):
        return "-" if v is None else f"{v / 1e6:.2f} ms"

    def hz(v):
        return "-" if not v else f"{1e9 / v:.0f} Hz"

    def span(v):
        return f"{min(v):.3f}..{max(v):.3f}" if v else "-"

    batch = list(r["batches"].values())
    names = [n for bit, n in BUTTONS.items() if r["buttons"] & bit]
    mode = {"0": "off", "1": "on"}
    rows = [
        ("Unbuffered", " + ".join(mode.get(u, u) for u in sorted(r["unbuffered"])) or "-"),
        ("Contact sample rate (p50)", hz(percentile(r["contact"], 50))),
        ("Contact interval p95 / max", f"{ms(percentile(r['contact'], 95))} / {ms(percentile(r['contact'], 100))}"),
        ("Hover sample rate", hz(percentile(r["hover"], 50))),
        ("Hover distance range (units)", span(r["distances"])),
        ("Samples per event (p50 / max)", f"{percentile(batch, 50) or '-'} / {percentile(batch, 100) or '-'}"),
        ("Delivery lag p50 / p95", f"{ms(percentile(r['lag'], 50))} / {ms(percentile(r['lag'], 95))}"),
        ("% historical samples", f"{100 * r['historical'] / r['samples']:.0f}%" if r["samples"] else "-"),
        ("Pressure min..max", span(r["pressures"])),
        ("Non-monotonic timestamps", r["backwards"]),
        ("Side button bits seen", f"0x{r['buttons']:x} {'+'.join(names) or '(none)'}; {r['presses']} press events"),
        ("Other-touch samples while pen down", r["touch_during_pen"]),
        ("Tool types", ", ".join(f"{k} {v}" for k, v in sorted(r["tools"].items())) or "-"),
        ("Strokes / canceled", f"{r['strokes']} / {r['canceled']}"),
        ("Lost pen-ups / open at end", f"{r['lost_ups']} / {'yes' if r['open_at_end'] else 'no'}"),
        ("Malformed rows", r["bad_rows"]),
    ]
    out = [f"# {m}" for m in r["meta"]] + ["| Measurement | Value |", "|---|---|"]
    out += [f"| {k} | {v} |" for k, v in rows]
    if len(r["unbuffered"]) > 1:
        out.append("\nWARNING: log mixes buffered and unbuffered samples; record them separately.")
    return "\n".join(out)


def main(argv):
    if len(argv) != 2:
        print("usage: analyze_log.py <pen.csv>", file=sys.stderr)
        return 1
    with open(argv[1], encoding="utf-8", newline="") as f:
        r = analyze(f)
    print(report(r))
    if not r["samples"]:
        print("no pen samples found", file=sys.stderr)
        return 1
    # Ordering and pen-up are hard requirements: make a violation visible to scripts.
    return 2 if r["backwards"] or r["lost_ups"] or r["open_at_end"] or r["bad_rows"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
