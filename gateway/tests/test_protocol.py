from __future__ import annotations

import unittest

from agri_gateway.protocol import ProtocolError, decode_frame, encode_frame, parse_ai_request
from agri_gateway.providers import MockProvider


class ProtocolTests(unittest.TestCase):
    def test_ai_request_round_trip(self) -> None:
        frame = encode_frame("V1", "AI_REQ", 42, 31, 39, 82, "STRONG")
        fields = decode_frame(frame.rstrip(b"\r\n"))
        request = parse_ai_request(fields)
        self.assertEqual(request.request_id, 42)
        self.assertEqual(request.light_level, "STRONG")

    def test_bad_light_is_rejected(self) -> None:
        fields = ["V1", "AI_REQ", "1", "25", "50", "101", "STRONG"]
        with self.assertRaises(ProtocolError):
            parse_ai_request(fields)

    def test_mock_provider_returns_safe_enums(self) -> None:
        fields = ["V1", "AI_REQ", "7", "33", "45", "90", "STRONG"]
        result = MockProvider().analyze(parse_ai_request(fields))
        self.assertEqual(result.status, "WARN")
        self.assertEqual(result.issue, "HOT_AND_BRIGHT")
        self.assertNotIn("PUMP", result.advice)
        self.assertTrue(result.suggestion_en.isascii())


if __name__ == "__main__":
    unittest.main()
