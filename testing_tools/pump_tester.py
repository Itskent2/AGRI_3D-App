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
        if self.is_connected:
            print(f"  \033[90m-> TX: {cmd}\033[0m")
            await self.ws.send(cmd)

async def main():
    print("="*50)
    print(" AGRI-3D THESIS PUMP TESTER (M100-M103) ")
    print("="*50)

    ip = input("Enter Agri3D IP (e.g. 192.168.0.143): ")
    uri = f"ws://{ip}/ws"
    
    tester = PumpTester(uri)
    if not await tester.connect():
        return

    print("\nSelect Pump to Test:")
    print("1. Water (M100/M101)")
    print("2. Fertilizer (M102/M103)")
    choice = input("Choice (1-2): ")
    
    pump_name = "Water" if choice == "1" else "Fertilizer"
    on_cmd = "M100" if choice == "1" else "M102"
    off_cmd = "M101" if choice == "1" else "M103"
    
    num_trials = int(input("Number of trials: "))
    
    filename = f"pump_results_{pump_name.lower()}_{int(time.time())}.csv"
    
    with open(filename, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["Trial", "Target_Seconds", "Actual_ML", "ML_Per_Sec"])
        
        for trial in range(1, num_trials + 1):
            # Randomize time (1 to 10 seconds)
            duration = round(random.uniform(1.0, 10.0), 2)
            
            print(f"\nTrial {trial}/{num_trials}:")
            print(f"  Action: Run {pump_name} for {duration} seconds")
            input("  [Press Enter to START Pump]")
            
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
            
            # Get Measurement
            try:
                ml_input = input(f"  > Enter measured amount in ml: ")
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
