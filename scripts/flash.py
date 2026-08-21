#!/usr/bin/env python3
"""Flash MilluBoard over OTA when possible, USB only for bootstrap/recovery."""

from __future__ import annotations

import argparse
import getpass
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HOST = "milluboard.local"
DEFAULT_USB_PORT = "/dev/ttyUSB0"
PIO_BIN = Path.home() / ".platformio" / "penv" / "bin" / "pio"
ESPOTA = Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "espota.py"


class CommandFailed(Exception):
    def __init__(self, returncode: int) -> None:
        self.returncode = returncode


def load_dotenv(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        name = name.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
            value = value[1:-1]
        values[name] = value
    return values


def secret(name: str, dotenv: dict[str, str]) -> str | None:
    return os.environ.get(name) or dotenv.get(name)


def pio() -> str:
    if PIO_BIN.is_file():
        return str(PIO_BIN)
    path = shutil.which("pio")
    if path:
        return path
    raise SystemExit("PlatformIO not found. Expected ~/.platformio/penv/bin/pio or pio in PATH.")


def run(command: list[str], *, cwd: Path = ROOT) -> None:
    result = subprocess.run(command, cwd=cwd, check=False)
    if result.returncode != 0:
        raise CommandFailed(result.returncode)


def host_resolves(host: str) -> bool:
    try:
        socket.gethostbyname(host)
        return True
    except OSError:
        return False


def source_ip_for(host: str) -> str:
    """Return the LAN address the board can use to call the OTA client back."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect((host, 3232))
        return probe.getsockname()[0]


def ensure_usb_access(port: str) -> None:
    if os.access(port, os.R_OK | os.W_OK):
        return

    pkexec = shutil.which("pkexec")
    if not pkexec:
        raise SystemExit(
            f"{port} is not writable and pkexec is not installed. "
            "Use OTA after the bootstrap flash, or fix udev/uucp permissions once."
        )

    user = getpass.getuser()
    print(f"Granting this login temporary access to {port} via pkexec setfacl...")
    run([pkexec, "setfacl", "-m", f"u:{user}:rw", port], cwd=Path("/"))


def flash_ota(host: str, password: str) -> None:
    if not ESPOTA.is_file():
        raise SystemExit(f"espota.py not found at {ESPOTA}. Run a PlatformIO build once.")

    run([pio(), "run", "-e", "esp32dev_ota"])
    firmware = ROOT / ".pio" / "build" / "esp32dev_ota" / "firmware.bin"
    run([
        sys.executable,
        str(ESPOTA),
        "--ip", host,
        "--host_ip", source_ip_for(host),
        "--port", "3232",
        "--host_port", "3233",
        "--auth", password,
        "--file", str(firmware),
        "--progress",
        "--timeout", "20",
    ])


def flash_usb(port: str) -> None:
    ensure_usb_access(port)
    run([pio(), "run", "-e", "esp32dev", "--target", "upload", "--upload-port", port])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=("auto", "ota", "usb"), default="auto")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", default=DEFAULT_USB_PORT, help="USB serial port for bootstrap/recovery")
    args = parser.parse_args()

    dotenv = load_dotenv(ROOT / ".env")
    ota_password = secret("OTA_PASSWORD", dotenv) or secret("API_TOKEN", dotenv)
    if not ota_password:
        raise SystemExit("Set API_TOKEN or OTA_PASSWORD in .env before OTA flashing.")

    try:
        if args.mode == "ota":
            flash_ota(args.host, ota_password)
            return 0

        if args.mode == "usb":
            flash_usb(args.port)
            return 0

        if host_resolves(args.host):
            try:
                flash_ota(args.host, ota_password)
                return 0
            except CommandFailed:
                print("OTA failed; falling back to USB bootstrap/recovery.", file=sys.stderr)

        flash_usb(args.port)
        return 0
    except CommandFailed as error:
        return error.returncode


if __name__ == "__main__":
    raise SystemExit(main())
