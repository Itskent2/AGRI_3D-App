import asyncio
import websockets
import socket
import uuid
import sys

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

class DirectCommander:
    def __init__(self, uri):
        self.base_uri = uri
        self.ws = None
        self.is_connected = False
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
            print("Connected! Type commands and press ENTER. Type 'exit' to quit.\n")
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
                    print("\n[\u26A0] Connection lost!")
                    self.is_connected = False
                    break
                
                message = await self.ws.recv()
                print(f"\n\033[92m<- RX: {message}\033[0m")
                print("CMD> ", end="", flush=True) # Reprint prompt after message
            except Exception as e:
                print(f"Listen error: {e}")
                self.is_connected = False
                break

    async def heartbeat(self):
        while True:
            try:
                if self.is_connected and self.ws and not self.ws.closed:
                    await self.ws.send("PING")
            except: pass
            await asyncio.sleep(5)

    async def send_command(self, cmd):
        if self.is_connected:
            print(f"\033[90m-> TX: {cmd}\033[0m")
            await self.ws.send(cmd)

async def main():
    print("="*50)
    print(" AGRI-3D DIRECT COMMANDER ")
    print("="*50)

    ip = discover_esp32_ip()
    if not ip:
        ip = input("Enter Agri3D IP manually (default: 192.168.0.143): ")
        if not ip.strip():
            ip = "192.168.0.143"
    
    uri = f"ws://{ip}/ws"
    
    commander = DirectCommander(uri)
    if not await commander.connect():
        return

    while True:
        try:
            # Use run_in_executor to avoid blocking the event loop on input()
            cmd = await asyncio.get_event_loop().run_in_executor(
                None, input, "CMD> "
            )
            
            if cmd.lower() == 'exit':
                break
                
            if cmd.strip():
                await commander.send_command(cmd)
                
        except KeyboardInterrupt:
            print("\nExiting...")
            break
        except Exception as e:
            print(f"Error: {e}")
            break

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nExiting...")
