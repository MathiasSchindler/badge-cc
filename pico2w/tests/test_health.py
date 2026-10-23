import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / ("health_test.exe" if os.name == "nt" else "health_test")


@unittest.skipUnless(EXE.exists(), "native health harness must be built first")
class HealthTests(unittest.TestCase):
    def test_fault_snapshot_json(self):
        data = json.loads(subprocess.check_output([str(EXE), "--json"]))
        self.assertEqual(data["serial"], "76EDD29A5E2CD5B4")
        self.assertEqual(data["wifi_stage"], 7)
        self.assertEqual(data["dhcp_state"], 5)
        self.assertEqual(data["network_error"], 1)
        self.assertEqual(data["radio_state"], 2)
        self.assertEqual(data["radio_error"], -3005)
        self.assertEqual(data["tx_error"], -3021)
        self.assertEqual(data["restore_error"], -3005)
        self.assertFalse(data["delivery_confirmed"])
        self.assertEqual(data["heartbeat_ms"], 300000)
        self.assertEqual(data["min_report_ms"], 60000)

    def test_page_safety_and_fault_reporting(self):
        page = subprocess.check_output([str(EXE), "--page"]).decode("ascii")
        self.assertIn("No acknowledgement protocol", page)
        self.assertIn("remote reporting is unavailable", page)
        self.assertIn("last snapshot may be stale", page)
        self.assertIn("textContent", page)
        for unsafe in ("innerHTML", "http://", "https://", "localStorage", "sessionStorage"):
            self.assertNotIn(unsafe, page)

    @unittest.skipUnless(shutil.which("node"), "Node.js not available")
    def test_health_ui_javascript_syntax(self):
        page = subprocess.check_output([str(EXE), "--page"]).decode("ascii")
        script = re.search(r"<script>(.*?)</script>", page, re.S).group(1)
        subprocess.run([shutil.which("node"), "--check"], input=script.encode(),
                       check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
