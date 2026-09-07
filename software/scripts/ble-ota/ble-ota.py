#!/usr/bin/env python3
"""Stream a firmware image directly to a PEAK device over BLE."""

from __future__ import annotations

import argparse
import asyncio
import importlib
import sys
import time
from dataclasses import dataclass
from typing import Optional

from ble_ota_protocol import (
    COMMAND_ABORT,
    COMMAND_FINISH,
    COMMAND_QUERY,
    DEFAULT_FIRMWARE_CHUNK_SIZE,
    OTA_CONTROL_UUID,
    OTA_DATA_UUID,
    OTA_SERVICE_UUID,
    OTA_STATUS_UUID,
    PROTOCOL_VERSION,
    FirmwareInfo,
    OtaState,
    OtaStatus,
    decode_status,
    encode_begin,
    encode_command,
    encode_data,
    inspect_firmware,
)

READY_TIMEOUT_SECONDS = 20.0
FINISH_TIMEOUT_SECONDS = 45.0


class DependencyError(RuntimeError):
    pass


class DeviceRejectedUpdate(RuntimeError):
    pass


@dataclass(frozen=True)
class OtaConfig:
    firmware: Optional[str]
    ble_name: str
    ble_address: Optional[str]
    scan_timeout: float
    scan_only: bool


class BleOtaProgram:
    def __init__(self, config: OtaConfig):
        self.config = config
        self.status_queue: asyncio.Queue[OtaStatus] = asyncio.Queue()
        self.disconnected = asyncio.Event()
        self.last_status: Optional[OtaStatus] = None

    async def run(self) -> int:
        bleak = load_bleak()
        if self.config.scan_only:
            await self._scan_devices(bleak)
            return 0

        if not self.config.firmware:
            raise RuntimeError("a local firmware .bin is required unless --scan is set")

        # Read and hash before scanning or connecting so local failures never begin an
        # OTA session on the device.
        firmware = inspect_firmware(self.config.firmware)
        print(f"Firmware: {firmware.path}")
        print(f"Size:     {firmware.size} bytes")
        print(f"SHA-256:  {firmware.sha256_hex}")

        target = await self._find_device(bleak)
        client = bleak.BleakClient(target, disconnected_callback=self._on_disconnect)
        try:
            await client.connect()
            print(f"Connected to {getattr(target, 'address', target)}")
            await client.start_notify(OTA_STATUS_UUID, self._on_status)
            chunk_size = self._select_chunk_size(client)
            print(f"Firmware payload per write: {chunk_size} bytes")
            try:
                await self._perform_update(client, firmware, chunk_size)
            except BaseException:
                await self._abort_if_possible(client)
                raise
        finally:
            if getattr(client, "is_connected", False):
                await client.disconnect()

        return 0

    async def _perform_update(self, client, firmware: FirmwareInfo, chunk_size: int):
        await client.write_gatt_char(
            OTA_CONTROL_UUID,
            encode_begin(firmware.size, firmware.sha256),
            response=True,
        )
        print("BEGIN sent; waiting for device to prepare the OTA partition...")
        await self._await_state(
            client,
            {OtaState.READY},
            READY_TIMEOUT_SECONDS,
            "OTA preparation",
        )

        started = time.monotonic()
        offset = 0
        with firmware.path.open("rb") as image:
            while payload := image.read(chunk_size):
                self._raise_if_disconnected("firmware transfer")
                await client.write_gatt_char(
                    OTA_DATA_UUID,
                    encode_data(offset, payload),
                    response=True,
                )
                offset += len(payload)
                self._consume_pending_status()
                self._print_progress(offset, firmware.size, started)

        print()
        await client.write_gatt_char(
            OTA_CONTROL_UUID,
            encode_command(COMMAND_FINISH),
            response=True,
        )
        print("FINISH sent; waiting for image verification...")
        status = await self._await_state(
            client,
            {OtaState.SUCCESS},
            FINISH_TIMEOUT_SECONDS,
            "image verification",
        )
        print(
            f"OTA succeeded: device accepted {status.written}/{status.total} bytes. "
            "It may now reboot."
        )

    async def _await_state(
        self,
        client,
        wanted: set[OtaState],
        timeout: float,
        phase: str,
    ) -> OtaStatus:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                status = await self._next_status(remaining, phase)
            except asyncio.TimeoutError:
                break
            self._show_device_status(status)
            if status.protocol != PROTOCOL_VERSION:
                raise RuntimeError(
                    f"device reported unsupported OTA protocol {status.protocol}"
                )
            if status.error != 0:
                raise DeviceRejectedUpdate(
                    f"device rejected OTA operation: {status.error_name} "
                    f"({status.error})"
                )
            if status.state in wanted:
                return status
            if status.state in (OtaState.FAILED, OtaState.ABORTED):
                raise DeviceRejectedUpdate(
                    f"device entered {status.state_name}; OTA error {status.error}"
                )

        status = await self._query_status(client)
        if status is not None:
            self._show_device_status(status)
            if status.protocol != PROTOCOL_VERSION:
                raise RuntimeError(
                    f"device reported unsupported OTA protocol {status.protocol}"
                )
            if status.error != 0:
                raise DeviceRejectedUpdate(
                    f"device rejected OTA operation: {status.error_name} "
                    f"({status.error})"
                )
            if status.state in wanted:
                return status
            detail = f"; last state was {status.state_name}, error {status.error}"
        else:
            detail = ""
        raise TimeoutError(f"timed out during {phase}{detail}")

    async def _next_status(self, timeout: float, phase: str) -> OtaStatus:
        status_task = asyncio.create_task(self.status_queue.get())
        disconnect_task = asyncio.create_task(self.disconnected.wait())
        done, pending = await asyncio.wait(
            {status_task, disconnect_task},
            timeout=timeout,
            return_when=asyncio.FIRST_COMPLETED,
        )
        for task in pending:
            task.cancel()
        if pending:
            await asyncio.gather(*pending, return_exceptions=True)
        if not done:
            raise asyncio.TimeoutError
        # A final status notification and disconnect can race during reboot. Honor
        # an already-delivered status first so SUCCESS is not reported as failure.
        if status_task in done:
            return status_task.result()
        status_task.cancel()
        raise RuntimeError(f"BLE device disconnected during {phase}")

    async def _query_status(self, client) -> Optional[OtaStatus]:
        self._raise_if_disconnected("status query")
        print("No status notification received; querying the device...")
        try:
            await asyncio.wait_for(
                client.write_gatt_char(
                    OTA_CONTROL_UUID,
                    encode_command(COMMAND_QUERY),
                    response=True,
                ),
                timeout=5.0,
            )
            packet = await asyncio.wait_for(
                client.read_gatt_char(OTA_STATUS_UUID),
                timeout=5.0,
            )
            return decode_status(packet)
        except Exception as exc:
            if self.disconnected.is_set():
                raise RuntimeError("BLE device disconnected during status query") from exc
            print(f"warning: status query failed: {exc}", file=sys.stderr)
            return None

    async def _abort_if_possible(self, client):
        if self.disconnected.is_set() or not getattr(client, "is_connected", False):
            return
        if self.last_status is not None and self.last_status.state in (
            OtaState.SUCCESS,
            OtaState.FAILED,
            OtaState.ABORTED,
        ):
            return
        try:
            await asyncio.wait_for(
                client.write_gatt_char(
                    OTA_CONTROL_UUID,
                    encode_command(COMMAND_ABORT),
                    response=True,
                ),
                timeout=3.0,
            )
            print("OTA session aborted.", file=sys.stderr)
        except BaseException as exc:
            print(f"warning: could not abort OTA session: {exc}", file=sys.stderr)

    def _consume_pending_status(self):
        while True:
            try:
                status = self.status_queue.get_nowait()
            except asyncio.QueueEmpty:
                return
            self._show_device_status(status, progress_line=True)
            if status.protocol != PROTOCOL_VERSION:
                raise RuntimeError(
                    f"device reported unsupported OTA protocol {status.protocol}"
                )
            if status.error != 0:
                raise DeviceRejectedUpdate(
                    f"device rejected OTA operation: {status.error_name} "
                    f"({status.error})"
                )
            if status.state in (OtaState.FAILED, OtaState.ABORTED):
                raise DeviceRejectedUpdate(
                    f"device entered {status.state_name}; OTA error {status.error}"
                )

    def _show_device_status(self, status: OtaStatus, progress_line: bool = False):
        changed = self.last_status is None or (
            status.state,
            status.error,
        ) != (
            self.last_status.state,
            self.last_status.error,
        )
        self.last_status = status
        if changed and not progress_line:
            print(
                f"Device: {status.state_name} "
                f"({status.written}/{status.total} bytes, "
                f"error={status.error_name})"
            )

    @staticmethod
    def _print_progress(written: int, total: int, started: float):
        elapsed = max(time.monotonic() - started, 0.001)
        percent = written * 100.0 / total
        speed_kib = written / elapsed / 1024.0
        print(
            f"\rSending: {percent:6.2f}%  {written}/{total} bytes  "
            f"{speed_kib:7.1f} KiB/s",
            end="",
            flush=True,
        )

    @staticmethod
    def _select_chunk_size(client) -> int:
        candidates = [DEFAULT_FIRMWARE_CHUNK_SIZE]

        # A write request can carry ATT_MTU - 3 bytes. Four bytes belong to the
        # OTA offset, leaving ATT_MTU - 7 bytes of firmware payload.
        mtu = getattr(client, "mtu_size", None)
        if isinstance(mtu, int) and mtu > 7:
            candidates.append(mtu - 7)

        # Bleak exposes this limit on the characteristic on supported backends.
        # It is formally the command (no-response) limit, but is also a safe upper
        # bound for a request sent on the same negotiated ATT bearer.
        services = getattr(client, "services", None)
        characteristic = (
            services.get_characteristic(OTA_DATA_UUID) if services is not None else None
        )
        max_write = getattr(characteristic, "max_write_without_response_size", None)
        if isinstance(max_write, int) and max_write > 4:
            candidates.append(max_write - 4)

        return max(1, min(candidates))

    async def _find_device(self, bleak):
        if self.config.ble_address:
            return self.config.ble_address

        print(
            "Scanning for PEAK OTA service "
            f"({OTA_SERVICE_UUID}) or name prefix {self.config.ble_name!r}..."
        )
        found = await self._discover_with_advertisements(bleak)
        fallback = None

        for device, advertisement in found:
            names = self._device_names(device, advertisement)
            service_uuids = self._service_uuids(advertisement)
            if OTA_SERVICE_UUID in service_uuids:
                print(f"Found OTA device: {format_device(device, advertisement)}")
                return device
            if any(name.casefold().startswith(self.config.ble_name.casefold()) for name in names):
                fallback = device

        if fallback is not None:
            print("Found device by name. OTA service UUID was not advertised.")
            return fallback
        raise RuntimeError(
            "could not find a PEAK OTA device; use --scan to inspect nearby "
            "advertisements or pass --address"
        )

    async def _scan_devices(self, bleak):
        print(f"Scanning for BLE devices for {self.config.scan_timeout:.1f}s...")
        found = await self._discover_with_advertisements(bleak)
        if not found:
            print("No BLE devices found.")
            return
        for device, advertisement in found:
            marker = ""
            if OTA_SERVICE_UUID in self._service_uuids(advertisement):
                marker = "  <-- PEAK OTA service"
            print(f"{format_device(device, advertisement)}{marker}")

    async def _discover_with_advertisements(self, bleak):
        devices = await bleak.BleakScanner.discover(
            timeout=self.config.scan_timeout,
            return_adv=True,
        )
        return list(devices.values())

    @staticmethod
    def _device_names(device, advertisement):
        return {
            name
            for name in (
                getattr(device, "name", None),
                getattr(advertisement, "local_name", None),
            )
            if name
        }

    @staticmethod
    def _service_uuids(advertisement):
        return {
            uuid.lower() for uuid in getattr(advertisement, "service_uuids", []) or []
        }

    def _on_status(self, _, data: bytearray):
        try:
            self.status_queue.put_nowait(decode_status(data))
        except ValueError as exc:
            print(f"warning: ignored malformed OTA status: {exc}", file=sys.stderr)

    def _on_disconnect(self, _):
        self.disconnected.set()

    def _raise_if_disconnected(self, phase: str):
        if self.disconnected.is_set():
            raise RuntimeError(f"BLE device disconnected during {phase}")


def load_bleak():
    try:
        return importlib.import_module("bleak")
    except ModuleNotFoundError as exc:
        if exc.name != "bleak":
            raise
        raise DependencyError(
            "missing Python dependency 'bleak'\n\n"
            "Install dependencies from the repository root:\n"
            "  python3 -m pip install -r scripts/ble-ota/requirements.txt\n\n"
            "A virtual environment is recommended:\n"
            "  python3 -m venv .venv\n"
            "  source .venv/bin/activate\n"
            "  python3 -m pip install -r scripts/ble-ota/requirements.txt"
        ) from exc


def format_device(device, advertisement) -> str:
    address = getattr(device, "address", "unknown")
    device_name = getattr(device, "name", None) or "-"
    local_name = getattr(advertisement, "local_name", None) or "-"
    services = ",".join(getattr(advertisement, "service_uuids", []) or []) or "-"
    return (
        f"address={address} device_name={device_name!r} "
        f"local_name={local_name!r} services={services}"
    )


def parse_args() -> OtaConfig:
    parser = argparse.ArgumentParser(
        description="Stream a PEAK firmware image directly over BLE.",
        epilog=(
            "Examples:\n"
            "  scripts/ble-ota/ble-ota.py --scan\n"
            "  scripts/ble-ota/ble-ota.py build/peak.bin\n"
            "  scripts/ble-ota/ble-ota.py --address AA:BB:CC:DD:EE:FF build/peak.bin\n"
            "  scripts/ble-ota/ble-ota.py --name PEAK-A1B2 build/peak.bin"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("firmware", nargs="?", help="local firmware .bin file")
    parser.add_argument(
        "--scan", action="store_true", help="list BLE advertisements and exit"
    )
    parser.add_argument(
        "--scan-timeout",
        type=float,
        default=8.0,
        help="BLE scan duration in seconds (default: 8)",
    )
    parser.add_argument(
        "--name",
        default="PEAK",
        help="BLE device name prefix (default: PEAK)",
    )
    parser.add_argument("--address", help="BLE device address or platform identifier")
    args = parser.parse_args()

    if args.scan_timeout <= 0:
        parser.error("--scan-timeout must be positive")
    if not args.scan and not args.firmware:
        parser.error("firmware is required unless --scan is set")

    return OtaConfig(
        firmware=args.firmware,
        ble_name=args.name,
        ble_address=args.address,
        scan_timeout=args.scan_timeout,
        scan_only=args.scan,
    )


async def async_main() -> int:
    return await BleOtaProgram(parse_args()).run()


def main() -> int:
    try:
        return asyncio.run(async_main())
    except KeyboardInterrupt:
        return 130
    except (DependencyError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
