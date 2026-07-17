from __future__ import annotations

import unittest

from agri_gateway.protocol import (
    ProtocolError,
    decode_frame,
    encode_frame,
    parse_ai_request,
    validate_device_id,
    validate_species_id,
)


class ProtocolTests(unittest.TestCase):
    def test_ai_request_round_trip(self) -> None:
        frame = encode_frame("V1", "AI_REQ", 42, 31, 39, 82, "STRONG")
        fields = decode_frame(frame.rstrip(b"\r\n"))
        request = parse_ai_request(fields)
        self.assertEqual(request.request_id, 42)
        self.assertEqual(request.light_level, "STRONG")
        self.assertEqual(request.device_id, "GM001")
        self.assertEqual(request.species_id, "pothos")
        self.assertEqual(request.message_type, "AI_REQ")

    def test_plant_ai_request_round_trip(self) -> None:
        frame = encode_frame(
            "V1", "PLANT_AI_REQ", 43, "GM001", "orchid", 26, 65, 62, "NORMAL"
        )
        request = parse_ai_request(decode_frame(frame.rstrip(b"\r\n")))
        self.assertEqual(request.request_id, 43)
        self.assertEqual(request.device_id, "GM001")
        self.assertEqual(request.species_id, "orchid")
        self.assertEqual(request.message_type, "PLANT_AI_REQ")

    def test_bad_light_is_rejected(self) -> None:
        fields = ["V1", "AI_REQ", "1", "25", "50", "101", "STRONG"]
        with self.assertRaises(ProtocolError):
            parse_ai_request(fields)

    def test_bad_device_id_is_rejected(self) -> None:
        fields = [
            "V1",
            "PLANT_AI_REQ",
            "1",
            "bad device",
            "pothos",
            "25",
            "50",
            "60",
            "NORMAL",
        ]
        with self.assertRaises(ProtocolError):
            parse_ai_request(fields)

    def test_plant_identifiers_are_validated(self) -> None:
        self.assertEqual(validate_device_id("GM001"), "GM001")
        self.assertEqual(validate_species_id("pothos"), "pothos")
        with self.assertRaises(ProtocolError):
            validate_device_id("bad device")
        with self.assertRaises(ProtocolError):
            validate_species_id("POTHOS")


if __name__ == "__main__":
    unittest.main()
