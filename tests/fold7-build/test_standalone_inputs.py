"""Build-input gates for the optional private standalone APK."""

from pathlib import Path
import hashlib
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import standalone_inputs as inputs


class StandaloneInputsTest(unittest.TestCase):
    def test_verified_copy_is_atomic_and_hash_checked(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            source = root / "input.zip"
            output = root / "bundle" / "cache.zip"
            source.write_bytes(b"synthetic owner fixture")
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            with patch.object(inputs, "COMPLETE_CACHE_SHA256", digest):
                inputs.copy_verified_cache(source, output)
                self.assertEqual(output.read_bytes(), source.read_bytes())
                source.write_bytes(b"corrupted fixture")
                with self.assertRaisesRegex(ValueError, "differs from pinned"):
                    inputs.copy_verified_cache(source, output)
                self.assertEqual(output.read_bytes(), b"synthetic owner fixture")
                self.assertFalse(output.with_name("cache.zip.partial").exists())

    def test_same_source_and_destination_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "cache.zip"
            source.write_bytes(b"fixture")
            with patch.object(inputs, "COMPLETE_CACHE_SHA256", inputs.sha256_file(source)):
                with self.assertRaisesRegex(ValueError, "must differ"):
                    inputs.copy_verified_cache(source, source)


if __name__ == "__main__":
    unittest.main()
