#!/usr/bin/env python3
"""Unit tests for mdev's XMODEM sender against a scripted receiver that
implements the same rules as the firmware (core/xmodem.c xm_receive)."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mdev import ACK, CAN, EOT, NAK, SOH, XModemError, XModemSender  # noqa: E402

BLOCK = 128


class Receiver:
    """Reactive device: its `read` returns scripted bytes, its `write`
    consumes sender packets and appends responses."""

    def __init__(self, mode="crc", nak_once=None, cancel_after_block=None):
        self.mode = mode  # "crc" | "checksum"
        self.nak_once = nak_once
        self.cancel_after_block = cancel_after_block
        self.out = bytearray([ord("C") if mode == "crc" else NAK])
        self.rx = bytearray()
        self.payload = bytearray()
        self.eot_seen = False
        self.block_count = 0
        self.expected = 1
        self.parsed = []

    def write(self, data):
        self.rx.extend(data)
        self._parse()

    def read(self, timeout):
        del timeout
        if self.out:
            return self.out.pop(0)
        return None

    def _parse(self):
        block_len = BLOCK + (2 if self.mode == "crc" else 1)
        while len(self.rx) >= 3 + block_len:
            header, block, inverted = self.rx[0], self.rx[1], self.rx[2]
            data = self.rx[3 : 3 + BLOCK]
            trailer = self.rx[3 + BLOCK : 3 + block_len]
            del self.rx[: 3 + block_len]

            assert header == SOH, "expected SOH, got 0x%02X" % header
            assert (block ^ inverted) & 0xFF == 0xFF, "block number not inverted"
            # After a NAK the sender retries the same block number, exactly
            # like the firmware receiver expects.
            assert block == (self.expected & 0xFF), \
                "block out of sequence: got %d, wanted %d" % (block, self.expected & 0xFF)

            if self.mode == "crc":
                from mdev import xmodem_crc16

                valid = xmodem_crc16(data) == (trailer[0] << 8 | trailer[1])
            else:
                from mdev import xmodem_checksum

                valid = xmodem_checksum(data) == trailer[0]

            self.block_count += 1
            self.parsed.append(bytes(data))
            if valid and self.nak_once != -1 and not (self.nak_once and self.nak_once == self.block_count):
                self.payload.extend(data)
                self.expected += 1
                if self.cancel_after_block and self.block_count >= self.cancel_after_block:
                    self.out.extend([CAN, CAN])
                else:
                    self.out.append(ACK)
            else:
                if self.nak_once != -1:
                    self.nak_once = None
                self.out.append(NAK)
        while len(self.rx) >= 1 and self.rx[0] == EOT:
            del self.rx[0]
            self.eot_seen = True
            self.out.append(ACK)

    def received(self):
        """Payload with trailing SUB padding trimmed, as a receiver with a
        known size (YMODEM metadata) would store it."""
        data = bytes(self.payload)
        while data.endswith(b"\x1a"):
            data = data[:-1]
        return data


def checks():
    # 1. CRC round trip, 300 bytes -> 3 blocks with SUB padding.
    payload = bytes((i * 7 + 3) & 0xFF for i in range(300))
    receiver = Receiver(mode="crc")
    sender = XModemSender(receiver.read, receiver.write, retries=5)
    assert sender.send(payload) == 300
    assert receiver.eot_seen
    assert receiver.block_count == 3
    assert receiver.received() == payload

    # 2. A NAKed first block is retried verbatim and then accepted.
    receiver = Receiver(mode="crc", nak_once=1)
    sender = XModemSender(receiver.read, receiver.write, retries=5)
    assert sender.send(b"A" * 200) == 200
    assert receiver.block_count == 3  # 1 retried + 2 fresh
    assert receiver.parsed[0] == receiver.parsed[1]
    assert receiver.received() == b"A" * 200

    # 3. Checksum mode: the sender follows a NAK negotiation.
    receiver = Receiver(mode="checksum")
    sender = XModemSender(receiver.read, receiver.write, retries=5)
    assert sender.send(b"checksum mode") == len(b"checksum mode")
    assert receiver.received() == b"checksum mode"
    assert receiver.eot_seen

    # 4. An exact multiple of the block size needs no padding block.
    payload = bytes(range(256))
    receiver = Receiver(mode="crc")
    sender = XModemSender(receiver.read, receiver.write, retries=5)
    assert sender.send(payload) == 256
    assert receiver.block_count == 2
    assert receiver.received() == payload

    # 5. Empty payloads still transfer (one padded block), matching the
    # firmware receiver which always expects at least one packet.
    receiver = Receiver(mode="crc")
    sender = XModemSender(receiver.read, receiver.write, retries=5)
    assert sender.send(b"") == 0
    assert receiver.block_count == 1
    assert receiver.eot_seen

    # 6. A receiver that cancels aborts the transfer with a clear error.
    receiver = Receiver(mode="crc", cancel_after_block=1)
    sender = XModemSender(receiver.read, receiver.write, retries=3, timeout_s=0.1)
    try:
        sender.send(b"x" * 300)
        raise AssertionError("cancel was not detected")
    except XModemError as error:
        assert "cancelled" in str(error)

    # 7. A silent receiver times out.
    receiver = Receiver(mode="crc")
    receiver.out.clear()
    sender = XModemSender(receiver.read, receiver.write, retries=2, timeout_s=0.05)
    try:
        sender.send(b"x")
        raise AssertionError("timeout was not detected")
    except XModemError as error:
        assert "timed out" in str(error)

    # 8. NAK exhaustion fails loudly.
    receiver = Receiver(mode="crc", nak_once=1)
    receiver.nak_once = -1  # NAK every block
    sender = XModemSender(receiver.read, receiver.write, retries=2, timeout_s=0.1)
    try:
        sender.send(b"x")
        raise AssertionError("NAK exhaustion was not detected")
    except XModemError as error:
        assert "not acknowledged" in str(error)

    print("mdev tests: 8 scenarios passed")


if __name__ == "__main__":
    checks()
