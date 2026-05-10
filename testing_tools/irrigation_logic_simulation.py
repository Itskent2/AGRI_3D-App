import asyncio
import websockets
import json
import random
import time
import socket
import uuid
import sys
import csv
import os
import msvcrt # For non-blocking keyboard input on Windows
import urllib.request # To fetch Open-Meteo data without extra libraries

# ANSI Colors for a premium CLI experience
CLR_RESET = "\033[0m"
CLR_HEADER = "\033[95m"
CLR_SUCCESS = "\033[92m"
CLR_WARNING = "\033[93m"
CLR_INFO = "\033[96m"
CLR_BOLD = "\033[1m"
CLR_VARIABLE = "\033[94m"

# Configuration from your ESP32-AGRI3D code
LAT = 10.3157
LON = 123.8854

def get_live_weather():
    """Fetches real-time hourly data (Ppop, Temp, Humidity) from Open-Meteo."""
    try:
        # Fetching hourly Precipitation Probability, Temperature, and Humidity
        url = f"https://api.open-meteo.com/v1/forecast?latitude={LAT}&longitude={LON}&hourly=precipitation_probability,temperature_2m,relative_humidity_2m&forecast_days=1"
        with urllib.request.urlopen(url) as response:
            data = json.loads(response.read().decode())
            current_hour = time.localtime().tm_hour
            
            weather_data = {
                "ppop": data['hourly']['precipitation_probability'][current_hour],
                "temp": data['hourly']['temperature_2m'][current_hour],
                "humidity": data['hourly']['relative_humidity_2m'][current_hour]
            }
            return weather_data
    except Exception as e:
        print(f"{CLR_WARNING}[!] Could not fetch live API data: {e}. Using simulation fallback.{CLR_RESET}")
        return None

def discover_esp32_ip(timeout=4):
    """Listens for AGRI-3D UDP discovery broadcast to find the ESP32 IP."""
    print(f"{CLR_INFO}Listening for AGRI-3D UDP discovery broadcast (port 4210)...{CLR_RESET}")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        sock.bind(('', 4210))
    except Exception as e:
        print(f"{CLR_WARNING}Could not bind to UDP port 4210: {e}{CLR_RESET}")
        return None
        
    sock.settimeout(timeout)
    try:
        data, server = sock.recvfrom(1024)
        msg = data.decode('utf-8')
        if msg.startswith("AGRI3D_DISCOVERY:"):
            ip = msg.split(":")[1].strip()
            print(f"{CLR_SUCCESS}[✓] Auto-detected ESP32 at {ip}{CLR_RESET}")
            return ip
    except socket.timeout:
        print(f"{CLR_WARNING}Timeout waiting for discovery broadcast.{CLR_RESET}")
    finally:
        sock.close()
    return None

class IrrigationSimulator:
    def __init__(self, uri):
        self.uri = uri
        self.ws = None
        self.is_connected = False
        self.token = "AGRI3D_SECURE_TOKEN_V1"
        self.sid = str(uuid.uuid4())[:8]
        self.real_rain_state = "DRY"
        self.live_weather = {"ppop": 0, "temp": 0.0, "humidity": 0}
        self.results_file = f"irrigation_results_{int(time.time())}.csv"
        
        # Initialize CSV with extra environmental columns
        with open(self.results_file, mode='w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow([
                "Timestamp", "Trial", "Scenario", 
                "Soil_Moisture_Pct", "API_Ppop_Pct", 
                "Temp_C", "Humidity_Pct", 
                "API_Source", "Rain_Sensor_FINAL", "System_Action"
            ])

    async def connect(self):
        auth_uri = f"{self.uri}?key={self.token}&sid={self.sid}&gen=1"
        print(f"{CLR_INFO}Connecting to WebSocket: {auth_uri}...{CLR_RESET}")
        try:
            self.ws = await websockets.connect(auth_uri)
            self.is_connected = True
            print(f"{CLR_SUCCESS}[✓] Connected to ESP32 WebSocket Server.{CLR_RESET}")
            asyncio.create_task(self.listen_for_updates())
            return True
        except Exception as e:
            print(f"{CLR_WARNING}[ERROR] Connection failed: {e}{CLR_RESET}")
            return False

    async def listen_for_updates(self):
        try:
            async for message in self.ws:
                if "RAIN:WET" in message or "RAIN:1" in message:
                    self.real_rain_state = "WET"
                elif "RAIN:DRY" in message or "RAIN:0" in message:
                    self.real_rain_state = "DRY"
        except Exception:
            self.is_connected = False

    def log_to_csv(self, trial, scenario, soil, ppop, temp, hum, api_source, rain, action):
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        with open(self.results_file, mode='a', newline='') as f:
            writer = csv.writer(f)
            writer.writerow([timestamp, trial, scenario, soil, ppop, temp, hum, api_source, rain, action])

    def print_trial_header(self, trial_num, scenario, soil, ppop, temp, hum, source):
        print("\n" + "="*80)
        print(f"{CLR_BOLD}TRIAL #{trial_num:03d} | {CLR_HEADER}{scenario}{CLR_RESET}")
        print("="*80)
        print(f"  {CLR_VARIABLE}INPUTS:{CLR_RESET}")
        print(f"    - Simulated Soil: {soil}% (Dry Threshold)")
        print(f"    - API Weather ({source}):")
        print(f"      > Rain Chance (Ppop): {ppop}%")
        print(f"      > Temperature: {temp}°C")
        print(f"      > Relative Humidity: {hum}%")

    async def wait_for_user_live(self, trial_num, scenario, soil, ppop):
        print(f"\n  {CLR_BOLD}{CLR_INFO}>>> HARDWARE TEST PHASE (Live Monitoring) <<<{CLR_RESET}")
        print(f"  {CLR_INFO}(Test sensor; press ENTER to log and continue.){CLR_RESET}")
        
        last_state = ""
        while True:
            if msvcrt.kbhit():
                key = msvcrt.getch()
                if key in [b'\r', b'\n']: break
            
            if self.real_rain_state != last_state:
                last_state = self.real_rain_state
                rain_color = CLR_SUCCESS if last_state == "DRY" else CLR_WARNING
                
                # Logic Evaluation (Matching ESP32 Routine logic)
                if ppop > 70.0:
                    action = "ABORT: High Rain Probability (>70%)"
                elif last_state == "WET":
                    action = "ABORT: Rain Sensor Triggered"
                elif ppop >= 40.0:
                    action = "SCALED: Weather Gating Active (50% Water)"
                else:
                    action = "NORMAL: Irrigation Initiated"
                
                action_color = CLR_SUCCESS if "Initiated" in action else (CLR_INFO if "SCALED" in action else CLR_WARNING)
                sys.stdout.write(f"\r    - LIVE SENSOR: {rain_color}{last_state:<6}{CLR_RESET} | {CLR_BOLD}ACTION: {action_color}{action:<42}{CLR_RESET} | {CLR_INFO}[TESTING...]{CLR_RESET}   ")
                sys.stdout.flush()
            
            await asyncio.sleep(0.05)
        
        print(f"\n\n  {CLR_SUCCESS}{CLR_BOLD}[LOGGED] -> {last_state} State Captured.{CLR_RESET}")
        return last_state, action

    async def run_simulation(self):
        # Refresh real API data once at start of simulation
        live_weather = get_live_weather()
        if live_weather:
            self.live_weather = live_weather
        
        # Display Menu like Pump Tester
        print(f"\n{CLR_BOLD}Select Simulation Mode:{CLR_RESET}")
        print(f"  1. Comprehensive 100-Trial Suite (All Scenarios)")
        print(f"  2. Scenario A: Predictive Override (High Ppop)")
        print(f"  3. Scenario B: Baseline Irrigation (Low Ppop)")
        print(f"  4. Scenario C: Live Environment (Open-Meteo)")
        
        choice = await asyncio.get_event_loop().run_in_executor(None, input, "Choice (1-4): ")
        
        all_scenarios = [
            (range(1, 35), "Predictive Override Test", "HIGH_PPOP"),
            (range(35, 68), "Baseline Irrigation Test", "LOW_PPOP"),
            (range(68, 101), "Live Environment Test", "LIVE")
        ]
        
        active_scenarios = []
        if choice == "1": active_scenarios = all_scenarios
        elif choice == "2": active_scenarios = [all_scenarios[0]]
        elif choice == "3": active_scenarios = [all_scenarios[1]]
        elif choice == "4": active_scenarios = [all_scenarios[2]]
        else: active_scenarios = all_scenarios

        for trials, scenario_name, mode in active_scenarios:
            print(f"\n{CLR_BOLD}{CLR_INFO}*** STARTING SCENARIO: {scenario_name.upper()} ({len(trials)} Trials) ***{CLR_RESET}")
            
            for i in trials:
                soil = random.randint(10, 29)
                temp = self.live_weather['temp']
                hum = self.live_weather['humidity']
                
                if mode == "HIGH_PPOP":
                    ppop = random.randint(71, 100)
                    source = "Simulated (High Ppop)"
                elif mode == "LOW_PPOP":
                    ppop = random.randint(0, 39)
                    source = "Simulated (Low Ppop)"
                else:
                    ppop = self.live_weather['ppop']
                    source = "LIVE Open-Meteo Hourly"
                
                self.print_trial_header(i, scenario_name, soil, ppop, temp, hum, source)
                
                final_rain, final_action = await self.wait_for_user_live(i, scenario_name, soil, ppop)
                self.log_to_csv(i, scenario_name, soil, ppop, temp, hum, source, final_rain, final_action)
                await asyncio.sleep(0.5)

        print(f"\n{CLR_SUCCESS}{CLR_BOLD}{'='*80}")
        print(f"   [SUCCESS] SIMULATION COMPLETE. RESULTS LOGGED TO: {self.results_file}")
        print(f"   (Includes Hourly Temperature, Humidity, and Rain Probability)")
        print(f"{'='*80}{CLR_RESET}")

async def main():
    print(f"\n{CLR_HEADER}{'='*80}")
    print(f"   AGRI-3D IRRIGATION LOGIC TESTER (Hourly API + Sensor)")
    print(f"   Location: Cebu City (10.31, 123.88)")
    print(f"{'='*80}{CLR_RESET}")

    ip = discover_esp32_ip()
    if not ip:
        ip = input("Enter ESP32 IP manually (default: 192.168.0.115): ")
        if not ip.strip(): ip = "192.168.0.115"
    
    uri = f"ws://{ip}/ws"
    simulator = IrrigationSimulator(uri)
    if await simulator.connect():
        await simulator.run_simulation()
    else:
        print(f"{CLR_WARNING}[!] Connection aborted.{CLR_RESET}")

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print(f"\n\n{CLR_WARNING}[EXIT] Simulation terminated by user.{CLR_RESET}")

