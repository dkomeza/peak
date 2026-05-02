import socket
import threading

ESP_IP = "192.168.4.1"
UDP_PORT = 8888

# Setup UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# Send a 1-byte dummy payload to wake up the ESP32 and lock the IP
sock.sendto(b'\x00', (ESP_IP, UDP_PORT))


def listen_for_esp():
    while True:
        data, addr = sock.recvfrom(1024)
        print(f"Received {len(data)} bytes from ESP: {data}")


threading.Thread(target=listen_for_esp, daemon=True).start()

# Main thread to send binary to ESP32
while True:
    msg = input("Press Enter to send a binary payload (or type 'quit')...")
    if msg == 'quit':
        break

    # Send some raw hex bytes (e.g., 0xDE 0xAD 0xBE 0xEF)
    binary_payload = bytes([0xDE, 0xAD, 0xBE, 0xEF])
    sock.sendto(binary_payload, (ESP_IP, UDP_PORT))
