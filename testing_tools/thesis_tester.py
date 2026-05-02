import asyncio
import websockets
import json
import csv
import random
import time
import os
import socket

class FarmbotTester:
    def __init__(self, uri):
        self.uri = uri
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

    async def connect(self):
        print(f"Connecting to {self.uri}...")
        self.ws = await websockets.connect(self.uri)
        self.is_connected = True
        print("Connected! Listening for machine dimensions...")
        asyncio.create_task(self.listen())
        
        # Wait until we get dimension info
        await asyncio.sleep(2)
        print(f"Machine Dimensions Loaded:")
        print(f"  X: 0 - {self.max_x} mm")
        print(f"  Y: 0 - {self.max_y} mm")
        print(f"  Z: 0 - {self.max_z} mm")
        print("-" * 40)

    async def listen(self):
        try:
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
        except websockets.exceptions.ConnectionClosed:
            self.is_connected = False
            print("\nConnection lost.")

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

    async def send_gcode(self, cmd):
        if self.ws and self.is_connected:
            await self.ws.send(cmd)
            # Short delay to ensure it enters the buffer
            await asyncio.sleep(0.1)

    async def wait_for_idle(self):
        # Give GRBL a moment to switch from Idle -> Run
        await asyncio.sleep(0.5)
        # Wait until GRBL reports Idle again
        while self.machine_state != "Idle":
            await asyncio.sleep(0.1)


async def run_test(tester, test_type, num_trials=100):
    filename = f"results_{test_type}_{int(time.time())}.csv"
    
    with open(filename, mode='w', newline='') as file:
        writer = csv.writer(file)
        if test_type == "XY":
            writer.writerow(["Trial", "Target_X", "Target_Y", "Actual_X", "Actual_Y"])
        else:
            writer.writerow(["Trial", f"Target_{test_type}", f"Actual_{test_type}"])
            
        print(f"\n[!] Starting {test_type} test for {num_trials} trials.")
        
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
        await tester.send_gcode("G0 Z0")
        await tester.wait_for_idle()

        for trial in range(1, num_trials + 1):
            target_x = tester.current_x
            target_y = tester.current_y
            target_z = tester.current_z
            
            if test_type == "X":
                target_x = random.randint(0, int(tester.max_x))
            elif test_type == "Y":
                target_y = random.randint(0, int(tester.max_y))
            elif test_type == "XY":
                target_x = random.randint(0, int(tester.max_x))
                target_y = random.randint(0, int(tester.max_y))
            elif test_type == "Z":
                target_z = random.randint(0, int(tester.max_z))

            # --- RETURN TO ORIGIN (0) BEFORE EACH TRIAL ---
            print(f"Trial {trial}/{num_trials}: Returning to origin (0) before testing...")
            if test_type == "Z":
                await tester.send_gcode("G0 Z0 F1000")
            else:
                await tester.send_gcode("G0 Z0") # Retract Z first
                await tester.wait_for_idle()
                
                if test_type == "X":
                    await tester.send_gcode("G0 X0 F1000")
                elif test_type == "Y":
                    await tester.send_gcode("G0 Y0 F1000")
                elif test_type == "XY":
                    await tester.send_gcode("G0 X0 Y0 F1000")
            await tester.wait_for_idle()

            # --- MOVE TO TARGET ---
            if test_type == "Z":
                cmd = f"G0 Z{target_z} F1000"
                print(f"Trial {trial}/{num_trials}: Moving Z to target {target_z} mm")
            else:
                cmd = f"G0 X{target_x} Y{target_y} F1000"
                if test_type == "X":
                    print(f"Trial {trial}/{num_trials}: Moving X to target {target_x} mm")
                elif test_type == "Y":
                    print(f"Trial {trial}/{num_trials}: Moving Y to target {target_y} mm")
                else:
                    print(f"Trial {trial}/{num_trials}: Moving to target X={target_x}, Y={target_y}")

            # Send Command and Wait
            await tester.send_gcode(cmd)
            await tester.wait_for_idle()

            # Wait for user to measure
            print("\033[92mMovement Complete.\033[0m")
            if test_type == "XY":
                actual_x = input(f"  > Measure physical X (Target was {target_x}): ")
                actual_y = input(f"  > Measure physical Y (Target was {target_y}): ")
                writer.writerow([trial, target_x, target_y, actual_x, actual_y])
            else:
                target_val = target_x if test_type == "X" else target_y if test_type == "Y" else target_z
                actual_val = input(f"  > Measure physical {test_type} (Target was {target_val}): ")
                writer.writerow([trial, target_val, actual_val])
            
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

        await run_test(tester, test_type, trials)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nTest aborted by user.")
