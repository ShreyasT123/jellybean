import argparse
import socket
import struct
import statistics
import time


def parse_shape(shape_str: str):
    dims = [int(x.strip()) for x in shape_str.split(",") if x.strip()]
    if not dims or any(d <= 0 for d in dims):
        raise ValueError("invalid shape")
    return dims


def num_elems(shape):
    n = 1
    for d in shape:
        n *= d
    return n


def send_all(sock: socket.socket, data: bytes):
    view = memoryview(data)
    while view:
        n = sock.send(view)
        if n <= 0:
            raise RuntimeError("socket send failed")
        view = view[n:]


def recv_all(sock: socket.socket, nbytes: int) -> bytes:
    chunks = []
    got = 0
    while got < nbytes:
        c = sock.recv(nbytes - got)
        if not c:
            raise RuntimeError("socket recv failed")
        chunks.append(c)
        got += len(c)
    return b"".join(chunks)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9000)
    p.add_argument("--shape", default="1,128,512")
    p.add_argument("--requests", type=int, default=40)
    args = p.parse_args()

    shape = parse_shape(args.shape)
    input_elems = num_elems(shape)
    payload = [((i % 113) * 0.01) for i in range(input_elems)]
    payload_bytes = struct.pack(f"<{input_elems}f", *payload)

    lat_ns = []
    ok = 0
    fail = 0
    t0 = time.perf_counter_ns()
    with socket.create_connection((args.host, args.port), timeout=10) as s:
        for _ in range(args.requests):
            send_all(s, struct.pack("<I", input_elems))
            send_all(s, payload_bytes)

            status = recv_all(s, 1)
            latency = struct.unpack("<Q", recv_all(s, 8))[0]
            out_elems = struct.unpack("<I", recv_all(s, 4))[0]
            if out_elems > 0:
                _ = recv_all(s, out_elems * 4)

            if status == b"\x01":
                ok += 1
                lat_ns.append(latency)
            else:
                fail += 1
    t1 = time.perf_counter_ns()

    wall_s = (t1 - t0) / 1e9
    rps = ok / wall_s if wall_s > 0 else 0.0
    print("python client done")
    print(f"requests={args.requests} ok={ok} fail={fail} throughput_req_s={rps:.2f}")
    if lat_ns:
        lat_ns_sorted = sorted(lat_ns)
        p50 = lat_ns_sorted[int(0.50 * (len(lat_ns_sorted) - 1))]
        p99 = lat_ns_sorted[int(0.99 * (len(lat_ns_sorted) - 1))]
        print(
            f"latency_ns avg={int(statistics.mean(lat_ns))} p50={p50} p99={p99}"
        )


if __name__ == "__main__":
    main()
