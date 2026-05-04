import serial
import time

try:
    s = serial.Serial('COM11', 115200, dsrdtr=True)
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)
    time.sleep(0.1)
    
    t = time.time()
    while time.time() - t < 5:
        if s.in_waiting > 0:
            print(s.readline().decode('utf-8', 'ignore').strip())
        else:
            time.sleep(0.01)
    s.close()
except Exception as e:
    print(f"Error: {e}")
