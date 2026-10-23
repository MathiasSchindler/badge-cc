import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / ("experiment_test.exe" if os.name == "nt" else "experiment_test")


@unittest.skipUnless(EXE.exists(), "native experiment harness must be built first")
class ExperimentTests(unittest.TestCase):
    def test_baseline_readback_and_airtime(self):
        d = json.loads(subprocess.check_output([str(EXE), "--json"]))
        self.assertEqual(d["config"], dict(frequency_hz=869850000, sf=7, bandwidth_khz=125,
                                         coding_rate=1, preamble=8, power_dbm=0, sync_word=18))
        self.assertEqual(d["airtime_us"], 77056)
        self.assertEqual(d["timeout_ms"], 128)
        self.assertEqual(d["wait_ms"], 60000)
        self.assertEqual(d["runs"], [])

    def test_ui_has_explicit_recovery_and_no_unsafe_dom(self):
        page = subprocess.check_output([str(EXE), "--page"]).decode("ascii")
        script = subprocess.check_output([str(EXE), "--script"]).decode("ascii")
        self.assertIn("No automatic rollback", page)
        self.assertIn("reboot restores", page)
        self.assertIn("Restore known-good profile", page)
        self.assertIn("not necessarily lost", page)
        self.assertIn("textContent", script)
        self.assertIn("'X-Pico-Request':'1'", script)
        self.assertNotIn("innerHTML", script)
        for content in (page, script):
            self.assertNotIn("https://", content)
            self.assertNotIn("http://", content)
        self.assertLess(len(page), 15360)
        self.assertLess(len(script), 15360)

    @unittest.skipUnless(shutil.which("node"), "Node.js not installed")
    def test_ui_syntax(self):
        script = subprocess.check_output([str(EXE), "--script"])
        subprocess.run([shutil.which("node"), "--check"], input=script, check=True, capture_output=True)

    @unittest.skipUnless(shutil.which("node"), "Node.js not installed")
    def test_ui_request_serialization_and_errors(self):
        subprocess.run([shutil.which("node"), str(ROOT / "tests" / "experiment_js_test.js")],
                       check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
