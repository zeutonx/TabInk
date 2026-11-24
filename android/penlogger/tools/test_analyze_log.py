import unittest

from analyze_log import analyze, percentile, report

HEADER = ("sample_time_ns,receive_time_ns,event_seq,source,action,action_index,pointer_index,"
          "pointer_id,tool,x,y,pressure,distance,tilt,orientation,button_state,history_size,unbuffered")


def row(t_ms, action, seq=0, src="C", tool="stylus", p=0.5, dist=0.0, btn=0, ptr=0):
    t = t_ms * 1_000_000
    return f"{t},{t + 2_000_000},{seq},{src},{action},0,{ptr},0,{tool},123.25,456.75,{p},{dist},0,0,{btn},0,0"


def run(*rows):
    return analyze(["# view 100x200", HEADER, *rows])


class AnalyzeTest(unittest.TestCase):
    def test_rate_batch_lag_and_history(self):
        r = run(row(0, "DOWN", 1), row(4, "MOVE", 2, "H"), row(8, "MOVE", 2), row(12, "UP", 3, p=0))
        self.assertEqual(r["contact"], [4_000_000] * 3)
        self.assertEqual(r["batches"], {1: 1, 2: 2, 3: 1})
        self.assertEqual(r["lag"], [2_000_000] * 3)  # historical rows excluded
        self.assertIn("250 Hz", report(r))
        self.assertEqual((r["strokes"], r["lost_ups"], r["open_at_end"]), (1, 0, False))

    def test_gap_between_strokes_is_not_an_interval(self):
        r = run(row(0, "DOWN"), row(4, "UP"), row(500, "DOWN"), row(504, "UP"))
        self.assertEqual(r["contact"], [4_000_000] * 2)

    def test_lost_up_open_end_and_cancel(self):
        r = run(row(0, "DOWN"), row(4, "DOWN"), row(8, "CANCEL"), row(20, "DOWN"))
        self.assertEqual((r["lost_ups"], r["canceled"], r["open_at_end"]), (1, 1, True))

    def test_backwards_and_equal_timestamps(self):
        self.assertEqual(run(row(0, "DOWN"), row(4, "MOVE"), row(3, "MOVE"), row(5, "UP"))["backwards"], 1)
        self.assertEqual(run(row(0, "DOWN"), row(4, "MOVE"), row(4, "UP"))["backwards"], 0)

    def test_hover_buttons_palm(self):
        r = run(row(0, "HOVER_ENTER", dist=2), row(5, "HOVER_MOVE", dist=9, btn=32), row(6, "DOWN"),
                row(7, "MOVE", tool="finger", ptr=1), row(8, "UP"), row(9, "MOVE", tool="finger"))
        self.assertEqual((r["hover"], r["distances"]), ([5_000_000], [2.0, 9.0]))
        self.assertEqual((r["buttons"], r["touch_during_pen"]), (32, 1))
        self.assertIn("STYLUS_PRIMARY", report(r))

    def test_malformed_and_empty(self):
        self.assertEqual(run(row(0, "DOWN"), "12,garbage")["bad_rows"], 1)
        self.assertEqual(run()["samples"], 0)
        report(run())

    def test_percentile_and_no_coordinates(self):
        self.assertIsNone(percentile([], 50))
        self.assertEqual(percentile([4, 1, 3, 2], 50), 2)
        out = report(run(row(0, "DOWN"), row(4, "UP")))
        self.assertNotIn("123.25", out)
        self.assertNotIn("456.75", out)


if __name__ == "__main__":
    unittest.main()
