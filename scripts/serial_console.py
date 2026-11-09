"""Send commands to the zclaw serial console and capture the reply.

Usage:
    python scripts/serial_console.py --port COM12 --send "/peripheral list" --wait 4
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("The pyserial package is required: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM12")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--send", action="append", default=[])
    parser.add_argument("--wait", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=1.0)
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        port.reset_input_buffer()
        time.sleep(args.settle)

        for command in args.send:
            port.write((command + "\r\n").encode("utf-8"))
            port.flush()
            time.sleep(args.wait)

        deadline = time.time() + args.wait
        while time.time() < deadline:
            data = port.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", errors="replace"))
                sys.stdout.flush()
                sys.stdout.flush()


if __name__ == "__main__":
    main()
