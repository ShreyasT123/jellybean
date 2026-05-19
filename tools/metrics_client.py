import socket
import sys

def main():
    host = "localhost"
    port = 9001
    if len(sys.argv) > 1:
        host = sys.argv[1]
    if len(sys.argv) > 2:
        port = int(sys.argv[2])

    try:
        with socket.create_connection((host, port), timeout=5) as s:
            data = b""
            while True:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
            print("Metrics response:")
            print(data.decode("utf-8"))
    except Exception as e:
        print(f"Failed to fetch metrics: {e}")

if __name__ == "__main__":
    main()
