#!/usr/bin/env python3
"""mdev — build, simulate, and deploy Mellivora apps.

    mdev build NAME            build tools/sdk/NAME.c into NAME.mapp (ARM)
    mdev sim NAME              run the same source under the host simulator
    mdev run NAME --port X     deploy NAME.mapp to /apps over XMODEM and run it

`run` needs pyserial (pip install pyserial) and the PicoCalc's USB serial
port. The transfer itself is a self-contained XMODEM sender in this file,
unit-tested against a scripted receiver, so the protocol survives without
hardware in the loop.
"""

import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SDK = os.path.join(ROOT, "tools", "sdk")
SIM = os.path.join(ROOT, "tools", "sim")

SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
SUB = 0x1A


class XModemError(Exception):
    pass


def xmodem_checksum(data):
    return sum(data) & 0xFF


def xmodem_crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def xmodem_blocks(data, block_size=128, pad=SUB):
    """Split `data` into blocks, each padded to block_size (or one empty)."""
    blocks = []
    for offset in range(0, len(data), block_size):
        block = bytearray(data[offset : offset + block_size])
        block.extend(bytes([pad]) * (block_size - len(block)))
        blocks.append(bytes(block))
    return blocks or [bytes([pad]) * block_size]


class XModemSender:
    """Drive an XMODEM receive into a callback-based transport.

    `read(timeout_s)` returns one byte or None on timeout; `write` takes
    bytes. The receiver picks the mode with its first byte: `C` starts CRC
    mode, NAK starts checksum mode. Every packet is retried on NAK.
    """

    def __init__(self, read, write, retries=10, timeout_s=2.0, block_size=128):
        self._read = read
        self._write = write
        self._retries = retries
        self._timeout = timeout_s
        self._block_size = block_size

    def _read_byte(self):
        return self._read(self._timeout)

    def _wait_for(self, wanted):
        for _ in range(self._retries * 4):
            byte = self._read_byte()
            if byte is None:
                raise XModemError("timed out waiting for 0x%02X" % wanted[0])
            if byte in wanted:
                return byte
            if byte == CAN:
                raise XModemError("receiver cancelled the transfer")
        raise XModemError("protocol error waiting for %s" % [hex(w) for w in wanted])

    def send(self, payload):
        """Send `payload`; returns the number of payload bytes accepted."""
        if not payload:
            payload = b""

        # Negotiation: the receiver announces CRC or checksum mode.
        first = self._wait_for([ord("C"), NAK])
        crc = first == ord("C")

        blocks = xmodem_blocks(payload, self._block_size)
        block_number = 0
        for block in blocks:
            sent = False
            for _ in range(self._retries):
                packet = bytearray([SOH, (block_number + 1) & 0xFF, (~(block_number + 1)) & 0xFF])
                packet.extend(block)
                if crc:
                    value = xmodem_crc16(block)
                    packet.extend([(value >> 8) & 0xFF, value & 0xFF])
                else:
                    packet.append(xmodem_checksum(block))
                self._write(bytes(packet))
                reply = self._wait_for([ACK, NAK])
                if reply == ACK:
                    sent = True
                    break
            if not sent:
                raise XModemError(
                    "block %d not acknowledged after %d retries" % (block_number + 1, self._retries)
                )
            block_number += 1

        self._write(bytes([EOT]))
        self._wait_for([ACK])
        return len(payload)


class FakeSerial:
    """Minimal serial shim for tests: deadline-driven blocking reads."""

    def __init__(self):
        self.timeout = 0.1
        self.from_device = bytearray()
        self.to_device = bytearray()

    def read(self, count):
        import time as _time

        deadline = _time.monotonic() + self.timeout
        while not self.from_device:
            if _time.monotonic() >= deadline:
                return b""
            _time.sleep(0.001)
        take = min(count, len(self.from_device))
        data = bytes(self.from_device[:take])
        del self.from_device[:take]
        return data

    def write(self, data):
        self.to_device.extend(data)


def _drain(serial, seconds=0.4):
    deadline = time.time() + seconds
    while time.time() < deadline:
        serial.timeout = max(0.05, deadline - time.time())
        if serial.read(4096) == b"":
            break


def _read_one(serial, timeout):
    serial.timeout = timeout
    data = serial.read(1)
    return data[0] if data else None


def cmd_build(name):
    subprocess.run(["make", "-C", SDK, "APP=%s" % name], check=True)
    artifact = os.path.join(SDK, "build", "%s.mapp" % name)
    print("built %s" % artifact)
    return artifact


def cmd_sim(name, autoquit_ms):
    args = ["make", "-C", SIM, "APP=%s" % name]
    if autoquit_ms:
        args.append("MAPP_AUTOQUIT_MS=%d" % autoquit_ms)
    subprocess.run(args, check=True)


def cmd_run(name, port, baud, launch, watch):
    try:
        import serial  # lazy: build/sim/test paths need no dependency
    except ImportError:
        sys.exit("mdev run needs pyserial:  pip install pyserial")

    artifact = cmd_build(name)
    with serial.Serial(port, baud, timeout=0.5) as device:
        # Get to a clean shell prompt.
        for _ in range(3):
            device.write(b"\x03")
            time.sleep(0.3)
        _drain(device)
        device.write(b"\r")
        time.sleep(0.5)
        _drain(device)

        # Start the receiver on the device.
        device.write(b"xmodem recv /apps/%s.mapp -p usb\r" % name.encode())
        sender = XModemSender(lambda timeout: _read_one(device, timeout), device.write)
        with open(artifact, "rb") as payload:
            data = payload.read()
        sent = sender.send(data)
        print("xmodem: %d bytes -> /apps/%s.mapp" % (sent, name))

        if launch:
            time.sleep(1.0)
            device.write(b"\r%s\r" % name.encode())
            print("launched %s" % name)
        if watch:
            deadline = time.time() + watch
            while time.time() < deadline:
                device.timeout = min(0.5, deadline - time.time())
                chunk = device.read(4096)
                if not chunk:
                    break
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()


def main(argv=None):
    parser = argparse.ArgumentParser(prog="mdev", description=__doc__)
    parser.add_argument("command", choices=["build", "sim", "run"])
    parser.add_argument("app", help="app name, matching tools/sdk/APP.c")
    parser.add_argument("--port", help="serial port for `run` (e.g. /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--no-launch", action="store_true", help="deploy without running")
    parser.add_argument("--autoquit-ms", type=int, default=4000,
                        help="virtual key timeout for `sim` (0 = default)")
    parser.add_argument("--watch", type=float, default=5.0,
                        help="seconds of device output to show after launching")
    args = parser.parse_args(argv)

    if args.command == "build":
        cmd_build(args.app)
    elif args.command == "sim":
        cmd_sim(args.app, args.autoquit_ms if args.autoquit_ms > 0 else 4000)
    else:
        if not args.port:
            sys.exit("mdev run needs --port (the PicoCalc USB serial device)")
        cmd_run(args.app, args.port, args.baud, not args.no_launch, args.watch)
    return 0


if __name__ == "__main__":
    sys.exit(main())
