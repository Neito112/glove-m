import serial, time, sys

port = sys.argv[1] if len(sys.argv) > 1 else "COM12"
cmds = sys.argv[2:] if len(sys.argv) > 2 else ["/espnow"]

s = serial.Serial(port, 115200, timeout=0.5)
s.reset_input_buffer()
time.sleep(1.0)
for c in cmds:
    s.write((c + "\r\n").encode())
    time.sleep(2.0)
    try:
        data = s.read(4000)
    except Exception:
        data = b""
    print("=== after %s ===" % c)
    print(data.decode("utf-8", "replace"))
s.close()
