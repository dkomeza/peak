import hashlib
import struct
import tempfile
import unittest
from pathlib import Path

from ble_ota_protocol import (
    COMMAND_ABORT,
    COMMAND_BEGIN,
    COMMAND_FINISH,
    COMMAND_QUERY,
    OtaState,
    decode_status,
    encode_begin,
    encode_command,
    encode_data,
    inspect_firmware,
)


class ProtocolEncodingTests(unittest.TestCase):
    def test_begin_packet_layout(self):
        digest = bytes(range(32))
        packet = encode_begin(0x12345678, digest)

        self.assertEqual(len(packet), 38)
        self.assertEqual(packet[:6], bytes((COMMAND_BEGIN, 1, 0x78, 0x56, 0x34, 0x12)))
        self.assertEqual(packet[6:], digest)

    def test_single_byte_commands(self):
        self.assertEqual(encode_command(COMMAND_FINISH), b"\x02")
        self.assertEqual(encode_command(COMMAND_ABORT), b"\x03")
        self.assertEqual(encode_command(COMMAND_QUERY), b"\x04")
        with self.assertRaises(ValueError):
            encode_command(COMMAND_BEGIN)

    def test_data_packet_uses_little_endian_offset(self):
        self.assertEqual(encode_data(0x12345678, b"abc"), b"xV4\x12abc")

    def test_status_packet_layout_and_signed_error(self):
        packet = struct.pack(
            "<BBBBIIi",
            1,
            OtaState.FAILED,
            COMMAND_FINISH,
            0x05,
            1234,
            4096,
            -7,
        )
        status = decode_status(packet)

        self.assertEqual(status.protocol, 1)
        self.assertEqual(status.state, OtaState.FAILED)
        self.assertEqual(status.state_name, "FAILED")
        self.assertEqual(status.last_command, COMMAND_FINISH)
        self.assertEqual(status.flags, 0x05)
        self.assertEqual(status.written, 1234)
        self.assertEqual(status.total, 4096)
        self.assertEqual(status.error, -7)

    def test_status_requires_exact_length(self):
        for packet in (b"", bytes(15), bytes(17)):
            with self.subTest(length=len(packet)), self.assertRaises(ValueError):
                decode_status(packet)

    def test_firmware_is_hashed_before_transfer(self):
        contents = b"test PEAK firmware\x00\xff"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "peak.bin"
            path.write_bytes(contents)
            info = inspect_firmware(path)

        self.assertEqual(info.size, len(contents))
        self.assertEqual(info.sha256, hashlib.sha256(contents).digest())


if __name__ == "__main__":
    unittest.main()
