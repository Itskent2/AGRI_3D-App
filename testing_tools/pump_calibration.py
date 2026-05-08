import asyncio
import websockets
import json
import time
import uuid
import socket
import os

class PumpCalibration:
    def __init__(self, uri):
        self.base_uri = uri
        self.ws = None
        self.is_connected = False
        
        # Singleton/Security Handshake
        self.sid = str(uuid.uuid4())[:8]
        self.gen = 1
        self.token = "AGRI3D_SECURE_TOKEN_V1"

    def get_auth_uri(self):
        connector = "&" if "?" in self.base_uri else "?"
        return f"{self.base_uri}{connector}key={self.token}&sid={self.sid}&gen={self.gen}"

    async def connect(self):
        auth_uri = self.get_auth_uri()
        print(f"Connecting to {auth_uri}...")
        try:
            self.ws = await websockets.connect(auth_uri)
            self.is_connected = True
            print("Connected to Agri3D Pump System!")
            asyncio.create_task(self.listen())
            asyncio.create_task(self.heartbeat())
        except Exception as e:
            print(f"Connection failed: {e}")
            return False
        return True

    async def listen(self):
        while True:
            try:
                if not self.ws or self.ws.closed:
                    self.gen += 1
                    auth_uri = self.get_auth_uri()
                    print(f"\n[\u26A0] Connection lost! Reconnecting (Gen {self.gen})...")
                    try:
                        self.ws = await websockets.connect(auth_uri)
                        self.is_connected = True
                    except:
                        await asyncio.sleep(2)
                        continue

                message = await self.ws.recv()
                print(f"\033[92m<- RX: {message}\033[0m")
            except Exception as e:
                print(f"\n[\u26A0] Listen error: {e}")
                self.is_connected = False
                await asyncio.sleep(1)

    async def heartbeat(self):
        while True:
            try:
                if self.is_connected and self.ws and not self.ws.closed:
                    print(f"  \033[90m-> TX: PING\033[0m")
                    await self.ws.send("PING")
            except: pass
            await asyncio.sleep(5)

    async def send_command(self, cmd):
        while not (self.is_connected and self.ws and not self.ws.closed):
            print(f"  \033[33m[!] Waiting for reconnection to send: {cmd}\033[0m")
            await asyncio.sleep(1)
            
        print(f"  \033[90m-> TX: {cmd}\033[0m")
        await self.ws.send(cmd)

def discover_esp32_ip(timeout=4):
    print("Listening for AGRI-3D UDP discovery broadcast (port 4210)...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        sock.bind(('', 4210))
    except Exception as e:
        print(f"Could not bind to UDP port 4210: {e}")
        return None
        
    sock.settimeout(timeout)
    try:
        data, server = sock.recvfrom(1024)
        msg = data.decode('utf-8')
        if msg.startswith("AGRI3D_DISCOVERY:"):
            ip = msg.split(":")[1].strip()
            print(f"[\u2713] Auto-detected ESP32 at {ip}")
            return ip
    except socket.timeout:
        print("Timeout waiting for discovery broadcast.")
    finally:
        sock.close()
    return None

async def main():
    print("="*50)
    print(" AGRI-3D PUMP CALIBRATION (1 Second Test) ")
    print("="*50)

    ip = discover_esp32_ip()
    if not ip:
        ip = input("Enter Agri3D IP manually (default: 192.168.0.143): ")
        if not ip.strip():
            ip = "192.168.0.143"
    
    uri = f"ws://{ip}/ws"
    
    calibrator = PumpCalibration(uri)
    if not await calibrator.connect():
        return

    # Safety Homing for Z
    print("\n" + "="*40)
    print("[SAFETY] Homing Z axis first...")
    print("="*40)
    await calibrator.send_command("$HZ")
    print("Waiting 30 seconds for Z homing to complete...")
    await asyncio.sleep(30)
    
    print("\n[SAFETY] Moving Z up to 200mm for safety...")
    await calibrator.send_command("G0 Z200 F500")
    print("Waiting 25 seconds for movement to complete...")
    await asyncio.sleep(25)

    print("\nSelect Pump to Calibrate:")
    print("1. Water (M100/M101)")
    print("2. Fertilizer (M102/M103)")
    choice = await asyncio.get_event_loop().run_in_executor(None, input, "Choice (1-2): ")
    
    pump_name = "Water" if choice == "1" else "Fertilizer"
    on_cmd = "M100" if choice == "1" else "M102"
    off_cmd = "M101" if choice == "1" else "M103"
    
    print(f"\nReady to calibrate {pump_name} pump.")
    try:
        duration_input = await asyncio.get_event_loop().run_in_executor(None, input, "Enter test duration in seconds (default: 2.0): ")
        test_duration = float(duration_input) if duration_input.strip() else 2.0
    except ValueError:
        print("  [!] Invalid input, using default 2.0 seconds.")
        test_duration = 2.0
        
    print(f"This will turn the pump on for EXACTLY {test_duration} seconds.")
    await asyncio.get_event_loop().run_in_executor(None, input, f"Press ENTER to start the {test_duration}-second run...")
    
    # Start Pump
    await calibrator.send_command(on_cmd)
    start_t = time.time()
    
    print(f"  \033[94mPump ACTIVE... ({test_duration}s)\033[0m")
    await asyncio.sleep(test_duration)
    
    # Stop Pump
    await calibrator.send_command(off_cmd)
    end_t = time.time()
    
    actual_duration = round(end_t - start_t, 3)
    print("\033[92m  Pump STOPPED.\033[0m")
    print(f"  Actual duration: {actual_duration} seconds")
    
    # Get Measurement
    try:
        ml_input = await asyncio.get_event_loop().run_in_executor(None, input, f"\n> Enter measured amount in ml: ")
        ml = float(ml_input)
        
        flow_rate = round(ml / actual_duration, 3)
        print(f"\n\033[96m[RESULT] Flow Rate for {pump_name}: {flow_rate} ml/sec\033[0m")
        
        # Send to ESP32
        esp32_cmd = f"SET_WATER_RATE:{flow_rate}" if choice == "1" else f"SET_FERT_RATE:{flow_rate}"
        await calibrator.send_command(esp32_cmd)
        print(f"[✓] Sent calibration to ESP32: {esp32_cmd}")
    except ValueError:
        print("  [!] Invalid input, calibration cancelled.")

if __name__ == "__main__":
    asyncio.run(main())
