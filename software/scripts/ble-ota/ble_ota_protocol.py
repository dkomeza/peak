"""Wire-format helpers for the PEAK BLE OTA v1 protocol."""

from __future__ import annotations

import enum
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path


PROTOCOL_VERSION = 1

OTA_SERVICE_UUID = "a6ed0701-d344-460a-8075-b9e8ec90d71b"
OTA_CONTROL_UUID = "a6ed0702-d344-460a-8075-b9e8ec90d71b"
OTA_DATA_UUID = "a6ed0703-d344-460a-8075-b9e8ec90d71b"
OTA_STATUS_UUID = "a6ed0704-d344-460a-8075-b9e8ec90d71b"

COMMAND_BEGIN = 0x01
COMMAND_FINISH = 0x02
COMMAND_ABORT = 0x03
COMMAND_QUERY = 0x04

BEGIN_PACKET_SIZE = 38
STATUS_PACKET_SIZE = 16
DEFAULT_FIRMWARE_CHUNK_SIZE = 245


class OtaState(enum.IntEnum):
    IDLE = 0
    PREPARING = 1
    READY = 2
    RECEIVING = 3
    VERIFYING = 4
    SUCCESS = 5
    FAILED = 6
    ABORTED = 7


@dataclass(frozen=True)
class FirmwareInfo:
    path: Path
    size: int
    sha256: bytes

    @property
    def sha256_hex(self) -> str:
        return self.sha256.hex()


@dataclass(frozen=True)
class OtaStatus:
    protocol: int
    state: int
    last_command: int
    flags: int
    written: int
    total: int
    error: int

    @property
    def state_name(self) -> str:
        try:
            return OtaState(self.state).name
        except ValueError:
            return f"UNKNOWN({self.state})"


def inspect_firmware(path: str | Path) -> FirmwareInfo:
    resolved = Path(path).expanduser().resolve()
    if not resolved.is_file():
        raise ValueError(f"firmware file does not exist: {resolved}")

    size = resolved.stat().st_size
    if size == 0:
        raise ValueError(f"firmware file is empty: {resolved}")
    if size > 0xFFFFFFFF:
        raise ValueError("firmware is too large for the OTA v1 size field")

    digest = hashlib.sha256()
    with resolved.open("rb") as firmware:
        for block in iter(lambda: firmware.read(1024 * 1024), b""):
            digest.update(block)

    return FirmwareInfo(path=resolved, size=size, sha256=digest.digest())


def encode_begin(image_size: int, sha256: bytes) -> bytes:
    if not 0 < image_size <= 0xFFFFFFFF:
        raise ValueError("image_size must fit in a non-zero uint32")
    if len(sha256) != hashlib.sha256().digest_size:
        raise ValueError("sha256 must contain exactly 32 bytes")

    packet = struct.pack("<BBI32s", COMMAND_BEGIN, PROTOCOL_VERSION, image_size, sha256)
    assert len(packet) == BEGIN_PACKET_SIZE
    return packet


def encode_command(command: int) -> bytes:
    if command not in (COMMAND_FINISH, COMMAND_ABORT, COMMAND_QUERY):
        raise ValueError(f"unsupported single-byte command: {command}")
    return bytes((command,))


def encode_data(offset: int, payload: bytes) -> bytes:
    if not 0 <= offset <= 0xFFFFFFFF:
        raise ValueError("offset must fit in uint32")
    if not payload:
        raise ValueError("OTA data payload must not be empty")
    return struct.pack("<I", offset) + payload


def decode_status(packet: bytes | bytearray) -> OtaStatus:
    if len(packet) != STATUS_PACKET_SIZE:
        raise ValueError(
            f"OTA status must be exactly {STATUS_PACKET_SIZE} bytes, got {len(packet)}"
        )
    return OtaStatus(*struct.unpack("<BBBBIIi", packet))
