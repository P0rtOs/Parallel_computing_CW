import socket
import time
import random
import argparse
import statistics
from concurrent.futures import ThreadPoolExecutor, as_completed

HOST_DEFAULT = "127.0.0.1"
PORT_DEFAULT = 8080


def send_request(host: str, port: int, command: str, timeout: float = 10.0) -> str:
    data = (command.strip() + "\n").encode("utf-8")
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(data)

        chunks = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk.decode("utf-8", errors="replace"))
    return "".join(chunks)


def timed_request(host: str, port: int, command: str, timeout: float = 10.0):
    start = time.perf_counter()
    ok = True
    resp = ""
    err = ""
    try:
        resp = send_request(host, port, command, timeout=timeout)
        if not resp.strip().startswith("OK"):
            ok = False
            err = resp.strip()[:200]
    except Exception as e:
        ok = False
        err = str(e)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    return elapsed_ms, ok, err


def percentile(values, p: float):
    if not values:
        return 0.0
    values_sorted = sorted(values)
    k = (len(values_sorted) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(values_sorted) - 1)
    if f == c:
        return float(values_sorted[f])
    return values_sorted[f] + (values_sorted[c] - values_sorted[f]) * (k - f)


def print_stats(name: str, latencies, errors_count: int):
    if not latencies:
        print(f"{name}: no samples")
        return

    avg = statistics.mean(latencies)
    p50 = percentile(latencies, 50)
    p95 = percentile(latencies, 95)
    mx = max(latencies)

    print(
        f"{name}: count={len(latencies)} errors={errors_count} "
        f"avg={avg:.2f}ms p50={p50:.2f}ms p95={p95:.2f}ms max={mx:.2f}ms"
    )


def main():
    parser = argparse.ArgumentParser(description="TCP benchmark client for your server")
    parser.add_argument("--host", default=HOST_DEFAULT)
    parser.add_argument("--port", type=int, default=PORT_DEFAULT)
    parser.add_argument("--timeout", type=float, default=10.0)

    parser.add_argument("--iterations", type=int, default=200, help="Total requests")
    parser.add_argument("--threads", type=int, default=1, help="Concurrent workers")
    parser.add_argument("--seed", type=int, default=42)

    parser.add_argument("--words", nargs="*", default=["good", "bad", "movie", "film", "great", "love", "hate"])
    parser.add_argument("--and_queries", nargs="*", default=["good movie", "great film", "love movie", "bad film"])
    parser.add_argument("--or_queries", nargs="*", default=["good bad", "love hate", "great bad", "movie film"])

    parser.add_argument("--sample_file", default="", help="Relative path inside baseDir, e.g. train/pos/0_9.txt")
    parser.add_argument("--include_file_ops", action="store_true", help="Include HAS_FILE/REINDEX_FILE/ADD/REMOVE in mix")

    args = parser.parse_args()
    random.seed(args.seed)

    def random_command():
        r = random.random()

        if r < 0.40:
            w = random.choice(args.words)
            return f"SEARCH_ONE {w}", "SEARCH_ONE"
        if r < 0.55:
            q = random.choice(args.and_queries)
            return f"SEARCH_ALL {q}", "SEARCH_ALL"
        if r < 0.70:
            q = random.choice(args.or_queries)
            return f"SEARCH_ANY {q}", "SEARCH_ANY"

        if not args.include_file_ops or not args.sample_file:
            w = random.choice(args.words)
            return f"SEARCH_ONE {w}", "SEARCH_ONE"

        if r < 0.85:
            return f"HAS_FILE {args.sample_file}", "HAS_FILE"
        if r < 0.95:
            return f"REINDEX_FILE {args.sample_file}", "REINDEX_FILE"
        if r < 0.975:
            return f"ADD_FILE {args.sample_file}", "ADD_FILE"
        return f"REMOVE_FILE {args.sample_file}", "REMOVE_FILE"

    lat_by_type = {}
    err_by_type = {}
    err_examples = {}

    def run_one(_):
        cmd, kind = random_command()
        ms, ok, err = timed_request(args.host, args.port, cmd, timeout=args.timeout)
        return kind, ms, ok, err, cmd

    start_all = time.perf_counter()

    if args.threads <= 1:
        for i in range(args.iterations):
            kind, ms, ok, err, cmd = run_one(i)
            lat_by_type.setdefault(kind, []).append(ms)
            if not ok:
                err_by_type[kind] = err_by_type.get(kind, 0) + 1
                err_examples.setdefault(kind, (cmd, err))
    else:
        with ThreadPoolExecutor(max_workers=args.threads) as ex:
            futures = [ex.submit(run_one, i) for i in range(args.iterations)]
            for f in as_completed(futures):
                kind, ms, ok, err, cmd = f.result()
                lat_by_type.setdefault(kind, []).append(ms)
                if not ok:
                    err_by_type[kind] = err_by_type.get(kind, 0) + 1
                    err_examples.setdefault(kind, (cmd, err))

    total_ms = (time.perf_counter() - start_all) * 1000.0
    total_reqs = args.iterations
    rps = (total_reqs / (total_ms / 1000.0)) if total_ms > 0 else 0.0

    print("\n=== SUMMARY ===")
    print(f"total_requests={total_reqs} threads={args.threads} total_time={total_ms:.2f}ms rps={rps:.2f}\n")

    for kind in sorted(lat_by_type.keys()):
        print_stats(kind, lat_by_type[kind], err_by_type.get(kind, 0))

    if err_examples:
        print("\n=== ERROR EXAMPLES ===")
        for kind, (cmd, err) in err_examples.items():
            print(f"{kind}: cmd={cmd!r} err={err!r}")


if __name__ == "__main__":
    main()
