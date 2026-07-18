#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import run_title


class CaptureResultTests(unittest.TestCase):
    def test_parse_saved_capture(self):
        result = run_title.parse_capture_result(
            "SAVED width=656 height=519 source=desktop client=640x480 "
            "samples=19200 nonblack=0.125000 colors=47 bbox=0,68,636,408 "
            "screen=123,456 window=115,417"
        )
        self.assertTrue(result["saved"])
        self.assertEqual(result["source"], "desktop")
        self.assertEqual(result["sampled_colors"], 47)
        self.assertEqual(result["nonblack_bbox"], [0, 68, 636, 408])
        self.assertEqual(result["client_screen_origin"], [123, 456])
        self.assertTrue(run_title.capture_is_visible(result, 32))

    def test_failure_and_representative_selection(self):
        failure = run_title.parse_capture_result("NOWINDOW")
        self.assertFalse(failure["saved"])
        shots = [
            failure,
            {"saved": True, "path": "flat.png", "sampled_colors": 2,
             "nonblack_fraction": 0.9},
            {"saved": True, "path": "scene.png", "sampled_colors": 80,
             "nonblack_fraction": 0.2},
        ]
        self.assertEqual(
            run_title.select_representative_shot(shots)["path"], "scene.png"
        )


class LogSummaryTests(unittest.TestCase):
    def test_structured_graphics_summary(self):
        text = "\n".join([
            "NVDRAW| frame=3 draw=0 clear op=0x0 count=0",
            "NVDRAW| frame=3 draw=4 inline op=0x5 count=3",
            "NVCRC| frame=3 addr=0x01588000 w=640 h=480 crc=0x89ABCDEF",
            "Emu: NV2A overlay received visible pixels (first=1)",
            "*Warning* sample",
            "EXC| sample",
        ])
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.log"
            log.write_text(text, encoding="utf-8")
            summary = run_title.summarize_log(log)
        self.assertEqual(summary["lines"], 6)
        self.assertEqual(summary["exception_lines"], 1)
        self.assertEqual(summary["warning_lines"], 1)
        self.assertTrue(summary["overlay_visible"])
        self.assertEqual(summary["nv2a_draws_per_frame"], {"3": 5})
        self.assertEqual(summary["nv2a_crc"][0]["crc32"], "0x89ABCDEF")


if __name__ == "__main__":
    unittest.main()
