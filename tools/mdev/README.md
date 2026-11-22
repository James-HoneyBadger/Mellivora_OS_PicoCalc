# mdev — build, simulate, and deploy

```sh
python3 tools/mdev/mdev.py build plasma       # ARM .mapp via tools/sdk
python3 tools/mdev/mdev.py sim plasma         # run under the host simulator
python3 tools/mdev/mdev.py run plasma \
    --port /dev/ttyACM0                       # build, XMODEM to /apps, run
```

`run` opens the PicoCalc's USB serial port, drops to a clean shell prompt,
starts `xmodem recv /apps/PLASMA.mapp -p usb`, and drives an XMODEM
transfer (CRC or checksum, whichever the device announces) with retries
and cancellation handling. When the transfer completes it types the app
name, which the shell resolves through the `/apps` catalogue.

Requirements: `pyserial` for `run` only. `mdev-test` exercises the sender
against a scripted receiver that implements the firmware's exact protocol
rules, so it runs without any hardware:

```sh
python3 tools/mdev/test_mdev.py
```
