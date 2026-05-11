#!/usr/bin/env python3
import argparse
import asyncio
import functools
import http.server
import importlib
import json
import socket
import sys
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Optional
from urllib.parse import quote

OTA_COMMAND_UUID = "6e400011-b5a3-f393-e0a9-e50e24dcca9e"
OTA_STATUS_UUID = "6e400012-b5a3-f393-e0a9-e50e24dcca9e"
OTA_SERVICE_UUID = "6e400010-b5a3-f393-e0a9-e50e24dcca9e"
DEFAULT_DISPLAY_HOST = "192.168.4.1"


class DependencyError(RuntimeError):
    pass


class QuietHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        return


@dataclass(frozen=True)
class OtaConfig:
    firmware: Optional[str]
    serve: bool
    bind: str
    port: int
    url_host: Optional[str]
    display_host: str
    ble_name: str
    ble_address: Optional[str]
    scan_timeout: float
    scan_only: bool


class FirmwareServer:
    def __init__(self, firmware_path: str, bind: str, port: int, url_host: str):
        self.path = Path(firmware_path).expanduser().resolve()
        self.bind = bind
        self.port = port
        self.url_host = url_host
        self._server = None
        self._thread = None
        self.url = ""

    def __enter__(self):
        if not self.path.is_file():
            raise RuntimeError(f"Firmware file does not exist: {self.path}")

        handler = functools.partial(
            QuietHTTPRequestHandler,
            directory=str(self.path.parent),
        )
        self._server = http.server.ThreadingHTTPServer(
            (self.bind, self.port),
            handler,
        )
        actual_port = self._server.server_address[1]
        self.url = f"http://{self.url_host}:{actual_port}/{quote(self.path.name)}"

        self._thread = threading.Thread(target=self._server.serve_forever)
        self._thread.daemon = True
        self._thread.start()

        print(f"Serving {self.path} at {self.url}")
        return self

    def __exit__(self, exc_type, exc, traceback):
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
        if self._thread is not None:
            self._thread.join(timeout=2)


class BleOtaProgram:
    def __init__(self, config: OtaConfig):
        self.config = config
        self.done = asyncio.Event()
        self.failed = False
        self.status_buffer = ""

    async def run(self) -> int:
        bleak = load_bleak()
        if self.config.scan_only:
            await self._scan_devices(bleak)
            return 0

        if not self.config.firmware:
            raise RuntimeError("firmware URL/path is required unless --scan is set")

        firmware_url = self.config.firmware

        if self.config.serve:
            url_host = self.config.url_host or get_local_ip_for_peer(
                self.config.display_host,
            )
            with FirmwareServer(
                self.config.firmware,
                self.config.bind,
                self.config.port,
                url_host,
            ) as server:
                firmware_url = server.url
                return await self._run_ble_ota(bleak, firmware_url)

        return await self._run_ble_ota(bleak, firmware_url)

    async def _run_ble_ota(self, bleak, firmware_url: str) -> int:
        target = await self._find_device(bleak)

        async with bleak.BleakClient(target) as client:
            await client.start_notify(OTA_STATUS_UUID, self._on_status)
            await client.write_gatt_char(
                OTA_COMMAND_UUID,
                f"URL {firmware_url}".encode(),
            )
            await client.write_gatt_char(OTA_COMMAND_UUID, b"START")
            await self.done.wait()

        return 1 if self.failed else 0

    async def _find_device(self, bleak):
        if self.config.ble_address:
            return self.config.ble_address

        print(
            "Scanning for PEAK OTA service "
            f"({OTA_SERVICE_UUID}) or name {self.config.ble_name!r}..."
        )
        found = await self._discover_with_advertisements(bleak)
        fallback = None

        for device, advertisement in found:
            names = self._device_names(device, advertisement)
            service_uuids = self._service_uuids(advertisement)

            if OTA_SERVICE_UUID in service_uuids:
                print(f"Found OTA device: {format_device(device, advertisement)}")
                return device

            if self.config.ble_name in names:
                fallback = device

        if fallback is not None:
            print("Found device by name. OTA service UUID was not advertised.")
            return fallback

        raise RuntimeError(
            "Could not find a PEAK OTA device. Run with --scan to inspect "
            "nearby BLE advertisements, or pass --address."
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
        names = set()
        if getattr(device, "name", None):
            names.add(device.name)
        if getattr(advertisement, "local_name", None):
            names.add(advertisement.local_name)
        return names

    @staticmethod
    def _service_uuids(advertisement):
        return {uuid.lower() for uuid in getattr(advertisement, "service_uuids", [])}

    def _on_status(self, _, data: bytearray):
        self.status_buffer += data.decode("utf-8", errors="replace")
        while "\n" in self.status_buffer:
            line, self.status_buffer = self.status_buffer.split("\n", 1)
            if not line:
                continue

            status = decode_status(line)
            if not status:
                continue

            state = status.get("state")
            if state == "success":
                self.done.set()
            elif state == "failed":
                self.failed = True
                self.done.set()


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


def decode_status(text: str):
    try:
        status = json.loads(text)
    except json.JSONDecodeError:
        print(text)
        return None

    state = status.get("state", "unknown")
    percent = status.get("percent", -1)
    speed = status.get("speed_bps", 0)
    message = status.get("message", "")
    if percent >= 0:
        print(f"{state:12} {percent:3}% {speed / 1024:7.1f} KiB/s {message}")
    else:
        print(f"{state:12} --- {speed / 1024:7.1f} KiB/s {message}")
    return status


def get_local_ip_for_peer(peer_host: str) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect((peer_host, 80))
        return sock.getsockname()[0]


def format_device(device, advertisement) -> str:
    address = getattr(device, "address", "unknown")
    device_name = getattr(device, "name", None) or "-"
    local_name = getattr(advertisement, "local_name", None) or "-"
    services = ",".join(getattr(advertisement, "service_uuids", []) or [])
    services = services or "-"
    return (
        f"address={address} device_name={device_name!r} "
        f"local_name={local_name!r} services={services}"
    )


def parse_args() -> OtaConfig:
    parser = argparse.ArgumentParser(
        description="Trigger PEAK firmware OTA over BLE.",
        epilog=(
            "Examples:\n"
            "  scripts/ble-ota/ble-ota.py --scan\n"
            "  scripts/ble-ota/ble-ota.py http://192.168.4.2:8000/build/peak.bin\n"
            "  scripts/ble-ota/ble-ota.py --serve build/peak.bin\n"
            "  scripts/ble-ota/ble-ota.py --serve --url-host 192.168.4.2 build/peak.bin"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "firmware",
        nargs="?",
        help="Firmware URL, or a local .bin path when --serve is set",
    )
    parser.add_argument(
        "--scan",
        action="store_true",
        help="List BLE advertisements and exit",
    )
    parser.add_argument(
        "--scan-timeout",
        type=float,
        default=8.0,
        help="BLE scan duration in seconds",
    )
    parser.add_argument(
        "--serve",
        action="store_true",
        help="Serve the local firmware file over HTTP and send that URL",
    )
    parser.add_argument(
        "--bind",
        default="0.0.0.0",
        help="HTTP bind address used with --serve",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8000,
        help="HTTP port used with --serve; use 0 for an ephemeral port",
    )
    parser.add_argument(
        "--url-host",
        help="Host/IP placed in the generated URL; auto-detected by default",
    )
    parser.add_argument(
        "--display-host",
        default=DEFAULT_DISPLAY_HOST,
        help="Display IP used only for auto-detecting the local HTTP URL host",
    )
    parser.add_argument("--name", default="PEAK", help="BLE device name")
    parser.add_argument("--address", help="BLE device address")
    args = parser.parse_args()

    return OtaConfig(
        firmware=args.firmware,
        serve=args.serve,
        bind=args.bind,
        port=args.port,
        url_host=args.url_host,
        display_host=args.display_host,
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
    except DependencyError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
