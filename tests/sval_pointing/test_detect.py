"""Run pointing_detect.c's probes against simulated modules on the host; no device is accessed."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


class PointingDetectTests(unittest.TestCase):
    def test_probes_find_each_module_and_leave_the_pins_released(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'detect'
            subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Werror', '-DSVAL_DETECT_HOST_TEST',
                            '-I', str(HERE), '-I', str(ROOT / 'keyboards/svalboard'),
                            str(ROOT / 'keyboards/svalboard/pointing_detect.c'), str(HERE / 'sim.c'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('OK', result.stdout)


if __name__ == '__main__':
    unittest.main()
