import socket
import struct
import sys
import time
import threading

def send_cmd(sock, *args):
    encoded_args = [a.encode() for a in args]
    payload = struct.pack("!I", len(encoded_args))
    for a in encoded_args:
        payload += struct.pack("!I", len(a)) + a
    frame = struct.pack("!I", len(payload)) + payload
    sock.sendall(frame)

    hdr = sock.recv(4)
    if not hdr:
        return None
    resp_len = struct.unpack("!I", hdr)[0]
    body = b""
    while len(body) < resp_len:
        body += sock.recv(resp_len - len(body))
    status = body[0]
    msg = body[1:].decode()
    return status, msg

def run_benchmark(clients=20, requests_per_client=1000):
    start = time.perf_counter()
    def worker(tid):
        s = socket.create_connection(("127.0.0.1", 6379))
        for i in range(requests_per_client):
            send_cmd(s, "SET", f"key:{tid}:{i}", f"val_{i}")
            send_cmd(s, "GET", f"key:{tid}:{i}")
        s.close()

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(clients)]
    for t in threads: t.start()
    for t in threads: t.join()
    elapsed = time.perf_counter() - start
    total_ops = clients * requests_per_client * 2
    print(f"Completed {total_ops} ops across {clients} concurrent clients in {elapsed:.2f}s ({total_ops/elapsed:,.0f} ops/sec)")

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "bench":
        run_benchmark()
    elif len(sys.argv) > 1:
        s = socket.create_connection(("127.0.0.1", 6379))
        print(send_cmd(s, *sys.argv[1:]))
        s.close()
    else:
        print("Usage: python3 client.py SET foo bar | GET foo | EXPIRE foo 5 | DEL foo | bench")