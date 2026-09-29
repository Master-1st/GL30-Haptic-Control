"""Catch truncated generated font arrays before C silently zero-fills them."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_kk_assets import font_payload


class FontAssetTests(unittest.TestCase):
    def test_concatenated_octal_bytes_include_implicit_terminator(self):
        source = r'const uint8_t sample[4] = "A\377" "\0";'
        self.assertEqual(font_payload(source, "sample", 4), b"A\xff\0\0")

    def test_truncated_initializer_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "expected 4 bytes"):
            font_payload('const uint8_t sample[4] = "A";', "sample", 4)

    def test_declaration_must_match_metadata(self):
        with self.assertRaisesRegex(ValueError, "differs from font metadata"):
            font_payload('const uint8_t sample[2] = "A";', "sample", 3)


if __name__ == "__main__":
    unittest.main()
