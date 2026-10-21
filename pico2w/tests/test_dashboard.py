import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / ("dashboard_test.exe" if os.name == "nt" else "dashboard_test")


@unittest.skipUnless(EXE.exists(), "native dashboard harness must be built first")
class DashboardTests(unittest.TestCase):
    def response(self, option):
        raw = subprocess.check_output([str(EXE), option])
        header, body = raw.split(b"\r\n\r\n", 1)
        length = int(re.search(rb"Content-Length: (\d+)", header).group(1))
        self.assertEqual(length, len(body))
        return header, body

    def test_json_shape_and_capacity(self):
        header, body = self.response("--json")
        self.assertTrue(header.startswith(b"HTTP/1.1 200 OK"))
        data = json.loads(body)
        self.assertEqual(data["build_id"], "12345678")
        self.assertEqual(len(data["packets"]), 8)
        self.assertLess(len(body), 15360)
        for packet in data["packets"]:
            self.assertEqual(len(bytes.fromhex(packet["hex"])), packet["length"])
            self.assertEqual(packet["length"], 255)
            self.assertEqual(packet["devaddr"], "12345678")
            self.assertEqual(packet["fcnt16"], 0x1234)
            self.assertTrue(packet["lorawan_candidate"])
            self.assertEqual(packet["rssi_qdb"], -401)

    def test_activity_shape_and_worst_case_capacity(self):
        header, body = self.response("--activity")
        self.assertTrue(header.startswith(b"HTTP/1.1 200 OK"))
        self.assertLess(len(body), 15360)
        data = json.loads(body)
        self.assertEqual(len(data["channels"]), 65)
        self.assertEqual(len(data["events"]), 8)
        self.assertEqual(len(data["channel_columns"]), 10)
        self.assertEqual(data["channels"][0][2], 4294967295)
        self.assertEqual(data["events"][0]["above_floor_qdb"], 510)
        self.assertEqual(data["events"][0]["interval_ms"], 4294967295)
        self.assertEqual(data["active_burst"]["frequency_hz"], 870000000)

    def test_page_has_no_external_dependencies_or_html_injection(self):
        _, body = self.response("--page")
        page = body.decode("ascii")
        _, core = self.response("--core")
        _, ui = self.response("--script")
        script = (core + ui).decode("ascii")
        self.assertIn("No LoRa transmissions", page)
        for content in (page, script):
            self.assertNotIn("https://", content)
            self.assertNotIn("http://", content)
            self.assertNotIn("innerHTML", content)
        self.assertIn("textContent", script)
        self.assertIn("'X-Pico-Request':'1'", script)
        self.assertIn("setTimeout(poll,1000)", script)
        self.assertNotIn("localStorage", script)
        self.assertNotIn("sessionStorage", script)
        self.assertIn("1,000", page)
        for option in ("--core", "--script"):
            header, body = self.response(option)
            self.assertIn(b"Content-Type: text/javascript", header)
            self.assertLess(len(header) + len(body) + 4, 16384)

    @unittest.skipUnless(shutil.which("node"), "Node.js not installed; syntax check unavailable")
    def test_javascript_syntax(self):
        _, core = self.response("--core")
        _, ui = self.response("--script")
        script = core + ui
        subprocess.run([shutil.which("node"), "--check"], input=script, check=True, capture_output=True)

    @unittest.skipUnless(shutil.which("node"), "Node.js not installed")
    def test_live_viewer_behavior(self):
        subprocess.run([shutil.which("node"), str(ROOT / "tests" / "dashboard_js_test.js")],
                       check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
