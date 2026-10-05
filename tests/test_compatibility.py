from pathlib import Path
import tempfile
import unittest
from tools.check_compatibility import report

ROOT = Path(__file__).resolve().parents[1]


class CompatibilityTests(unittest.TestCase):
    def test_finds_vtable_without_fixed_rva(self):
        result = report(ROOT / 'scriptrunner.dll', ROOT / 'scriptrunner.dll')
        self.assertTrue(result['can_run_with_current_host'])
        self.assertEqual(len(result['runner_vtables']), 1)
        methods = result['runner_vtables'][0]['methods']
        self.assertEqual([m['first_linear_ret_cleanup'] for m in methods[1:4]], ['4', '0', '0xc'])

    def test_same_layout_unknown_hash_is_not_auto_approved(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            dll = Path(folder) / 'new.dll'
            dll.write_bytes((ROOT / 'scriptrunner.dll').read_bytes() + b'changed')
            result = report(dll, ROOT / 'scriptrunner.dll')
            self.assertFalse(result['can_run_with_current_host'])
            self.assertEqual(result['runner_vtables'], result['baseline_vtables'])
