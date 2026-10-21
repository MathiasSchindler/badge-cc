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

    def test_page_has_no_external_dependencies_or_html_injection(self):
        _, body = self.response("--page")
        page = body.decode("ascii")
        self.assertIn("No LoRa transmissions", page)
        self.assertNotIn("https://", page)
        self.assertNotIn("http://", page)
        self.assertNotIn("innerHTML", page)
        self.assertIn("textContent", page)
        self.assertIn("'X-Pico-Request':'1'", page)
        self.assertIn("setTimeout(poll,1000)", page)

    @unittest.skipUnless(shutil.which("node"), "Node.js not installed; syntax check unavailable")
    def test_javascript_syntax(self):
        _, body = self.response("--page")
        script = re.search(rb"<script>(.*?)</script>", body, re.S).group(1)
        subprocess.run([shutil.which("node"), "--check"], input=script, check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
