import asyncio
import websockets
import json
import csv
import random
import time
import os
import socket
import math
import uuid

class FarmbotTester:
    def __init__(self, uri):
        self.base_uri = uri
        self.ws = None
        self.max_x = 1000.0
        self.max_y = 1000.0
        self.max_z = 500.0
        
        self.current_x = 0.0
        self.current_y = 0.0
        self.current_z = 0.0
        self.is_connected = False
        self.machine_state = "Unknown"
        self.identified = False

        # Machine Settings (Steps/mm)
        self.steps_x = 100.0
        self.steps_y = 100.0
        self.steps_z = 100.0

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
        self.ws = await websockets.connect(auth_uri)
        self.is_connected = True
        print("Connected! Requesting machine dimensions ($$)...")
        
        # Start background tasks
        asyncio.create_task(self.listen())
        asyncio.create_task(self.heartbeat())
        
        # Send $$ to get the settings dump
        await self.send_gcode("$$")
        
        # Wait until we get dimension info (or timeout)
        print("Waiting for settings response...")
        await asyncio.sleep(3) 
        
        print(f"Machine Dimensions Loaded:")
        print(f"  X: 20 - {self.max_x - 20} mm (Safe Zone)")
        print(f"  Y: 20 - {self.max_y - 20} mm (Safe Zone)")
        print(f"  Z: 20 - {self.max_z - 20} mm (Safe Zone)")
        print("-" * 40)

    async def listen(self):
        while True:
            try:
                if not self.ws or self.ws.closed:
                    self.gen += 1
                    auth_uri = self.get_auth_uri()
                    print(f"\n[\u26A0] Connection lost! Attempting to reconnect (Gen {self.gen}) to {auth_uri}...")
                    try:
                        self.ws = await websockets.connect(auth_uri)
                        self.is_connected = True
                        self.identified = False
                        print("[\u2713] Reconnected successfully! Resuming test...")
                    except Exception:
                        await asyncio.sleep(2)
                        continue

                async for message in self.ws:
                    if isinstance(message, str):
                        if not self.identified and ("FARMBOT_ID:" in message or '"system":"AGRI_3D"' in message):
                            self.identified = True
                            try:
                                data = json.loads(message)
                                if 'maxX' in data: self.max_x = float(data['maxX'])
                                if 'maxY' in data: self.max_y = float(data['maxY'])
                                if 'maxZ' in data: self.max_z = float(data['maxZ'])
                            except Exception:
                                pass
                        
                        try:
                            data = json.loads(message)
                            if 'nano_raw' in data:
                                raw = data['nano_raw']
                                self.parse_grbl(raw)
                        except Exception:
                            self.parse_grbl(message)
            except Exception:
                self.is_connected = False
                await asyncio.sleep(1)

    async def heartbeat(self):
        """Sends periodic PING to keep the connection alive (Singleton Watchdog)."""
        while True:
            try:
                if self.is_connected and self.ws and not self.ws.closed:
                    await self.ws.send("PING")
            except Exception:
                pass
            await asyncio.sleep(5) # Send every 5 seconds (watchdog is 10s)

    def parse_grbl(self, raw):
        if raw.startswith("<"):
            parts = raw[1:].split("|")
            self.machine_state = parts[0]
            
            # Extract position
            for p in parts:
                if p.startswith("MPos:") or p.startswith("WPos:"):
                    pos_str = p.split(":")[1].split(">")[0]
                    coords = pos_str.split(",")
                    if len(coords) >= 3:
                        self.current_x = float(coords[0])
                        self.current_y = float(coords[1])
                        self.current_z = float(coords[2])
        
        # Parse $130, $131, $132 for dimensions
        if raw.startswith("$130="): self.max_x = float(raw[5:])
        if raw.startswith("$131="): self.max_y = float(raw[5:])
        if raw.startswith("$132="): self.max_z = float(raw[5:])
        
        # Parse Steps per mm
        if raw.startswith("$100="): self.steps_x = float(raw[5:])
        if raw.startswith("$101="): self.steps_y = float(raw[5:])
        if raw.startswith("$102="): self.steps_z = float(raw[5:])

    async def send_gcode(self, cmd):
        while not self.is_connected or not self.ws or self.ws.closed:
            print("\n[\u29D7] Waiting for connection before sending command...")
            await asyncio.sleep(2)
        try:
            print(f"  \033[90m-> TX: {cmd}\033[0m")
            await self.ws.send(cmd)
            await asyncio.sleep(0.1)
        except Exception:
            self.is_connected = False
            await asyncio.sleep(1)
            await self.send_gcode(cmd) # Retry sending

    async def wait_for_idle(self):
        # Give GRBL a moment to switch from Idle -> Run
        await asyncio.sleep(0.5)
        # Wait until GRBL reports Idle again
        while self.machine_state != "Idle":
            if not self.is_connected:
                await asyncio.sleep(1)
                continue
            await asyncio.sleep(0.1)


async def run_test(tester, test_type, num_trials=100, seed_val=None, dry_run=False):
    random.seed(seed_val)
    print(f"\n[!] Using Random Seed: {seed_val}")
        
    filename = f"results_{test_type}_{int(time.time())}.csv"
    if dry_run:
        filename = f"dry_run_{test_type}_{seed_val if seed_val else 'random'}.csv"
    
    with open(filename, mode='w', newline='') as file:
        writer = csv.writer(file)
        if test_type == "XY":
            if dry_run:
                writer.writerow(["Trial", "Target_X", "Target_Y", "Target_Diagonal"])
            else:
                writer.writerow(["Trial", "Target_X", "Target_Y", "Target_Diagonal", "Actual_Diagonal"])
        else:
            if dry_run:
                writer.writerow(["Trial", f"Target_{test_type}"])
            else:
                writer.writerow(["Trial", f"Target_{test_type}", f"Actual_{test_type}"])
            
        print(f"\n[!] Starting {test_type} test for {num_trials} trials.")
        
        if not dry_run:
            print("[!] Executing specific homing sequence for this test...")
            if test_type == "X":
                await tester.send_gcode("$HX")
                await tester.wait_for_idle()
            elif test_type == "Y":
                await tester.send_gcode("$HY")
                await tester.wait_for_idle()
            elif test_type == "XY":
                await tester.send_gcode("$HX")
                await tester.wait_for_idle()
                await tester.send_gcode("$HY")
                await tester.wait_for_idle()
            elif test_type == "Z":
                await tester.send_gcode("$HZ")
                await tester.wait_for_idle()

            print("[!] Refreshing machine dimensions after auto-dimensioning...")
            await tester.send_gcode("$$")
            await asyncio.sleep(2) # Give it time to parse the updated $130, $131, $132
            print(f"    Updated Dimensions -> X: {tester.max_x}mm | Y: {tester.max_y}mm | Z: {tester.max_z}mm")

            print("[!] Enforcing Rule: Retracting Z axis before XY movement...")
            await tester.send_gcode("G0 Z20 F500")
            await tester.wait_for_idle()

        for trial in range(1, num_trials + 1):
            target_x = tester.current_x
            target_y = tester.current_y
            target_z = tester.current_z
            
            if test_type == "X":
                target_x = random.randint(20, int(tester.max_x - 20))
            elif test_type == "Y":
                target_y = random.randint(20, int(tester.max_y - 20))
            elif test_type == "XY":
                target_x = random.randint(20, int(tester.max_x - 20))
                target_y = random.randint(20, int(tester.max_y - 20))
            elif test_type == "Z":
                target_z = random.randint(20, int(tester.max_z - 20))

            # --- RETURN TO SAFE ORIGIN (20) BEFORE EACH TRIAL ---
            if not dry_run:
                print(f"Trial {trial}/{num_trials}: Returning to safe origin (20) before testing...")
                if test_type == "Z":
                    await tester.send_gcode("G0 Z20 F500")
                else:
                    await tester.send_gcode("G0 Z20 F500") # Retract Z first
                    await tester.wait_for_idle()
                    
                    if test_type == "X":
                        await tester.send_gcode("G0 X20 F1000")
                    elif test_type == "Y":
                        await tester.send_gcode("G0 Y20 F1000")
                    elif test_type == "XY":
                        await tester.send_gcode("G0 X20 Y20 F1000")
                await tester.wait_for_idle()

            # --- MOVE TO TARGET ---
            if test_type == "Z":
                cmd = f"G0 Z{target_z} F500"
                if not dry_run: print(f"Trial {trial}/{num_trials}: Moving Z to target {target_z} mm")
            else:
                cmd = f"G0 X{target_x} Y{target_y} F1000"
                if test_type == "X":
                    if not dry_run: print(f"Trial {trial}/{num_trials}: Moving X to target {target_x} mm")
                elif test_type == "Y":
                    if not dry_run: print(f"Trial {trial}/{num_trials}: Moving Y to target {target_y} mm")
                else:
                    if not dry_run: print(f"Trial {trial}/{num_trials}: Moving to target X={target_x}, Y={target_y}")

            if dry_run:
                print(f"Trial {trial}/{num_trials}: Target Generated -> " + (f"X={target_x}, Y={target_y}" if test_type=="XY" else f"{test_type}={target_z if test_type=='Z' else target_x if test_type=='X' else target_y}"))
                if test_type == "XY":
                    target_diag = round(math.hypot(target_x, target_y), 2)
                    writer.writerow([trial, target_x, target_y, target_diag])
                else:
                    target_val = target_x if test_type == "X" else target_y if test_type == "Y" else target_z
                    writer.writerow([trial, target_val])
                file.flush()
                continue # Skip physical movement and measurement

            # Send Command and Wait
            await tester.send_gcode(cmd)
            await tester.wait_for_idle()

            # Wait for user to measure
            print("\033[92mMovement Complete.\033[0m")
            if test_type == "XY":
                target_diag = round(math.hypot(target_x, target_y), 2)
                actual_diag = input(f"  > Measure physical Diagonal (Target was {target_diag} mm): ")
                writer.writerow([trial, target_x, target_y, target_diag, actual_diag])
            else:
                target_val = target_x if test_type == "X" else target_y if test_type == "Y" else target_z
                actual_input = input(f"  > Measure physical {test_type} (Target was {target_val}): ")
                try:
                    actual_val = float(actual_input)
                    writer.writerow([trial, target_val, actual_val])

                    # Calculate Correction
                    if target_val > 0 and actual_val > 0 and abs(target_val - actual_val) > 0.01:
                        current_steps = tester.steps_x if test_type == "X" else tester.steps_y if test_type == "Y" else tester.steps_z
                        # New Steps = (Current Steps * Target Distance) / Actual Distance
                        new_steps = round((current_steps * target_val) / actual_val, 3)
                        
                        print(f"  \033[93m[!] Accuracy Error detected! Error: {round(actual_val - target_val, 3)}mm\033[0m")
                        print(f"  \033[93m[!] Suggested {test_type} steps/mm: {new_steps} (was {current_steps})\033[0m")
                        
                        do_fix = input(f"  > Apply correction to Nano? (y/N): ")
                        if do_fix.lower() == 'y':
                            param_num = 100 if test_type == "X" else 101 if test_type == "Y" else 102
                            await tester.send_gcode(f"${param_num}={new_steps}")
                            # Update local value so next calculation is accurate
                            if test_type == "X": tester.steps_x = new_steps
                            elif test_type == "Y": tester.steps_y = new_steps
                            elif test_type == "Z": tester.steps_z = new_steps
                except ValueError:
                    print("  \033[31m[!] Invalid measurement entered. Skipping correction.\033[0m")
                    writer.writerow([trial, target_val, actual_input])
            
            # Save immediately to prevent data loss
            file.flush()
            print("-" * 40)

        print(f"\n[SUCCESS] Test complete. Data saved to {filename}")


def discover_esp32_ip(timeout=4):
    print("Listening for AGRI-3D UDP discovery broadcast (port 4210)...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # Allow multiple sockets to use the same port number
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
    print("=========================================")
    print("   AGRI-3D Automated Thesis Tester")
    print("=========================================")
    
    ip = discover_esp32_ip()
    
    if not ip:
        ip = input("Enter ESP32 IP address manually (default: 192.168.4.1): ")
        if not ip.strip():
            ip = "192.168.4.1"
    
    uri = f"ws://{ip}/ws"
    tester = FarmbotTester(uri)
    
    try:
        await tester.connect()
    except Exception as e:
        print(f"Failed to connect: {e}")
        return

    while True:
        print("\nSelect Test Mode:")
        print("  1. Isolated X")
        print("  2. Isolated Y")
        print("  3. Concurrent XY")
        print("  4. Isolated Z")
        print("  5. Exit")
        
        choice = input("Enter choice (1-5): ")
        
        test_type = None
        if choice == '1': test_type = "X"
        elif choice == '2': test_type = "Y"
        elif choice == '3': test_type = "XY"
        elif choice == '4': test_type = "Z"
        elif choice == '5': break
        else: 
            print("Invalid choice.")
            continue

        trials_input = input("Enter number of trials (default 100): ")
        trials = int(trials_input) if trials_input.strip().isdigit() else 100
        
        seed_input = input("Enter a random seed (leave blank for random): ")
        seed_val = int(seed_input) if seed_input.strip().isdigit() else int(time.time())
        
        dry_run_input = input("Dry run only? (generate targets without moving machine) (y/N): ")
        dry_run = dry_run_input.strip().lower() == 'y'

        await run_test(tester, test_type, trials, seed_val, dry_run)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nTest aborted by user.")
