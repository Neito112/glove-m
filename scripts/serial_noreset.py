"""Open the serial port without resetting the ESP32.

Opening the port with the default line states makes the USB Serial/JTAG
peripheral reset the chip, which increments the boot counter and can push
the device into safe mode. This helper opens the port with both control
lines released so the device keeps running.
"""

import argparse
import sys
import time

import serial


def open_port(port, baudrate):
    handle = serial.Serial()
    handle.port = port
    handle.baudrate = baudrate
    handle.timeout = 1
    # Release DTR and RTS before opening so the chip is not reset.
    handle.dtr = False
    handle.rts = False
    handle.open()
    handle.dtr = False
    handle.rts = False
    return handle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM12")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--send", action="append", default=[])
    parser.add_argument("--settle", type=float, default=2.0)
    parser.add_argument("--wait", type=float, default=2.0)
    args = parser.parse_args()

    handle = open_port(args.port, args.baud)
    try:
        time.sleep(0.3)
        handle.reset_input_buffer()
        for payload in args.send:
            handle.write(payload.encode() + b"\r\n")
            handle.flush()
            time.sleep(args.settle)
        deadline = time.time() + args.wait
        while time.time() < deadline:
            chunk = handle.read(4096)
            if chunk:
                sys.stdout.write(chunk.decode("utf-8", "ignore"))
                sys.stdout.flush()
    finally:
        handle.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
