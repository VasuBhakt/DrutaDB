import socket
import time
import sys

def benchmark(host='127.0.0.1', port=6379, total_requests=10000):
    # Prepare standard Redis SET command in RESP format
    # *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
    cmd = b"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((host, port))
    except ConnectionRefusedError:
        print("Error: DrutaDB server is not running on 127.0.0.1:6379")
        sys.exit(1)

    print(f"Starting benchmark: {total_requests} SET requests...")
    
    start_time = time.time()
    
    for i in range(total_requests):
        s.sendall(cmd)
        # Wait for response to ensure we measure round-trip
        # The response for SET is "+OK\r\n" (5 bytes)
        resp = s.recv(5)
        
    end_time = time.time()
    duration = end_time - start_time
    rps = total_requests / duration
    
    print("\n" + "="*30)
    print(f"Benchmark Results:")
    print(f"Total Requests: {total_requests}")
    print(f"Total Time:     {duration:.4f} seconds")
    print(f"Throughput:     {rps:.2f} requests/sec")
    print("="*30)
    
    s.close()

if __name__ == "__main__":
    benchmark()
