import serial, time, sys

ser = serial.Serial('COM3', 115200, timeout=0.1)
print("Monitoring COM3...")
with open("scratch/serial_log.txt", "w", encoding="utf-8") as f:
    start = time.time()
    while time.time() - start < 120:
        raw = ser.readline()
        if raw:
            line = raw.decode('utf-8', errors='replace')
            timestamp = f"[{int(time.time() - start)}s] "
            safe_line = (timestamp + line).encode('ascii', errors='replace').decode('ascii')
            sys.stdout.write(safe_line)
            sys.stdout.flush()
            f.write(timestamp + line)
            f.flush()
ser.close()
