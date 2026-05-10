import asyncio
import websockets
import json
import csv
import random
import time
import os
import socket
import uuid

class PumpTester:
    def __init__(self, uri):
        self.base_uri = uri
        self.ws = None
        self.is_connected = False
        
        # Singleton/Security Handshake
        self.sid = str(uuid.uuid4())[:8]
        self.gen = 1
        self.token = "AGRI3D_SECURE_TOKEN_V1"

        # Calibration Data (ml/sec)
        self.flow_rates = {
            "Water": 10.0,
            "Fertilizer": 5.0
        }
        self.load_calibration()

    def load_calibration(self):
        filename = "pump_calibration.json"
        if os.path.exists(filename):
            try:
                with open(filename, 'r') as f:
                    data = json.load(f)
                    self.flow_rates.update(data)
                    print(f"[✓] Loaded calibration from {filename}")
            except Exception as e:
                print(f"[⚠] Error loading calibration: {e}")

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
                try:
                    data = json.loads(message)
                    if "w_rate" in data:
                        self.flow_rates["Water"] = data["w_rate"]
                    if "f_rate" in data:
                        self.flow_rates["Fertilizer"] = data["f_rate"]
                except: pass
            except Exception:
                self.is_connected = False
                await asyncio.sleep(1)

    async def heartbeat(self):
        while True:
            try:
                if self.is_connected and self.ws and not self.ws.closed:
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
    print(" AGRI-3D THESIS PUMP TESTER (M100-M103) ")
    print("="*50)

    ip = discover_esp32_ip()
    if not ip:
        ip = input("Enter Agri3D IP manually (default: 192.168.0.143): ")
        if not ip.strip():
            ip = "192.168.0.143"
    
    uri = f"ws://{ip}/ws"
    
    tester = PumpTester(uri)
    if not await tester.connect():
        return

    # Safety Homing for Z
    print("\n" + "="*40)
    print("[SAFETY] Homing Z axis first...")
    print("="*40)
    await tester.send_command("$HZ")
    print("Waiting 30 seconds for Z homing to complete...")
    await asyncio.sleep(30)
    
    print("\n[SAFETY] Moving Z up to 200mm for safety...")
    await tester.send_command("G0 Z200 F500")
    print("Waiting 25 seconds for movement to complete...")
    await asyncio.sleep(25)

    print("\nSelect Sector/Pump to Test:")
    print("1. Sector 1 & 2 (Irrigation - Water M100/M101)")
    print("2. Sector 3 & 4 (Fertigation - Fertilizer M102/M103)")
    choice = await asyncio.get_event_loop().run_in_executor(None, input, "Choice (1-2): ")
    
    pump_name = "Water" if choice == "1" else "Fertilizer"
    print(f"\n\033[96m[INFO] Using calibrated flow rate for {pump_name}: {tester.flow_rates[pump_name]} ml/sec\033[0m")
    
    seed_input = await asyncio.get_event_loop().run_in_executor(None, input, "Enter random seed (optional): ")
    if seed_input.strip():
        try:
            seed = int(seed_input)
            random.seed(seed)
            print(f"  \033[92m[✓] Using random seed: {seed}\033[0m")
        except ValueError:
            print("  \033[31m[⚠] Invalid seed, using random sequence.\033[0m")
    on_cmd = "M100" if choice == "1" else "M102"
    off_cmd = "M101" if choice == "1" else "M103"
    
    num_trials_input = await asyncio.get_event_loop().run_in_executor(None, input, "Number of trials [Default 25]: ")
    num_trials = int(num_trials_input) if num_trials_input.strip() else 25
    
    filename = f"pump_results_{pump_name.lower()}_{int(time.time())}.csv"
    
    with open(filename, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["Trial", "Target_Seconds", "Actual_ML", "ML_Per_Sec"])
        
        for trial in range(1, num_trials + 1):
            # Randomize time (1 to 8s for Water, 1 to 30s for Fert)
            if pump_name == "Water":
                duration = round(random.uniform(1.0, 8.0), 2)
            else:
                duration = round(random.uniform(1.0, 30.0), 2)
            target_volume = duration * tester.flow_rates[pump_name]
            
            print(f"\n" + "="*40)
            print(f"Trial {trial} of {num_trials}")
            print(f"Target: Run {pump_name} for {duration} seconds (Est. Vol: {target_volume:.1f} ml)")
            print("="*40)
            
            # Clear message for pause
            print(f"\n\033[93m[!] STEP 1: Prepare to measure {pump_name}.\033[0m")
            await asyncio.get_event_loop().run_in_executor(None, input, "\033[93m>>> Press ENTER to START Pump <<<\033[0m")
            
            # Start Pump
            await tester.send_command(on_cmd)
            start_t = time.time()
            
            # Wait for duration
            print(f"  \033[94mPump ACTIVE... ({duration}s)\033[0m")
            await asyncio.sleep(duration)
            
            # Stop Pump
            await tester.send_command(off_cmd)
            end_t = time.time()
            actual_duration = round(end_t - start_t, 2)
            
            print("\033[92m  Pump STOPPED.\033[0m")
            
            # CSV-ready string output
            time_ms = int(actual_duration * 1000)
            print(f"\n\033[96m[CSV DATA] {trial}, {time_ms}, {target_volume:.1f}\033[0m")
            
            # Get Measurement
            try:
                ml_input = await asyncio.get_event_loop().run_in_executor(None, input, f"\n  > Enter measured amount in ml: ")
                ml = float(ml_input)
                
                ml_per_sec = round(ml / actual_duration, 3)
                print(f"  \033[96mResult: {ml_per_sec} ml/sec\033[0m")
                
                writer.writerow([trial, actual_duration, ml, ml_per_sec])
                file.flush()
            except ValueError:
                print("  [!] Invalid input, skipping record.")

    print(f"\n[!] Testing Complete. Results saved to {filename}")

if __name__ == "__main__":
    asyncio.run(main())
