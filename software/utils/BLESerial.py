import asyncio
from bleak import BleakClient, BleakScanner
from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout

# --- Configuration ---
SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"  # UART service UUID
CHAR_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # Notify characteristic (from ESP32)
CHAR_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # Write characteristic (to ESP32)

TARGET_NAME = "PEAK"

# --- Globals ---
session = PromptSession()

def notification_handler(sender, data):
    """Handles incoming data notifications from the BLE device."""
    print(f"{data.decode(errors='ignore')}", end="")

async def send_loop(client: BleakClient):
    """Main loop to handle user input and send it to the BLE device."""
    while True:
        try:
            msg = await session.prompt_async(">> ", enable_history_search=True)
            if msg.lower() == "exit":
                break
            

            if msg: # Only send if the message is not empty
                await client.write_gatt_char(CHAR_RX_UUID, msg.encode())

        except (EOFError, KeyboardInterrupt):
            # Gracefully exit on Ctrl+D or Ctrl+C
            break

async def run():
    """Scans for the BLE device and runs the main communication loop."""
    print("🔍 Scanning for BLE devices...")
    target = await BleakScanner.find_device_by_name(TARGET_NAME)

    if not target:
        print(f"❌ Device '{TARGET_NAME}' not found.")
        return

    print(f"✅ Found device: {target.name} ({target.address})")

    try:
        async with BleakClient(target.address) as client:
            if not client.is_connected:
                print(f"Could not connect to {target.address}")
                return
                
            print(f"🤝 Connected to {TARGET_NAME}. Type 'exit' to quit.")
            await client.start_notify(CHAR_TX_UUID, notification_handler)
            
            # Use patch_stdout to prevent background prints from corrupting the prompt
            with patch_stdout():
                await send_loop(client)
            
            await client.stop_notify(CHAR_TX_UUID)
    except Exception as e:
        print(f"ERROR: An exception occurred: {e}")

if __name__ == "__main__":
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass  # User-initiated exit
    finally:
        print("\nExiting...")