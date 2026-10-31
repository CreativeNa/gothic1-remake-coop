"""UDP impairment proxy for two coop copies on one PC (bad-network tests).

The copies are configured (net_proxy.txt, see couchlink_seat.ps1) to send to the proxy instead of to each other:
    seat 0 -> proxy port P   -> forwarded to seat 1 (127.0.0.1:BASE+1)
    seat 1 -> proxy port P+1 -> forwarded to seat 0 (127.0.0.1:BASE)
Impairments: packet loss (%), one-way delay (ms) with jitter (ms, packets may overtake each other = reordering), duplicates (%).

    python netproxy.py --port 41001 --base 40001 --loss 10 --delay 150 --jitter 60 --dup 2 --seconds 3600
"""
import argparse, heapq, random, socket, threading, time

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=41001)
ap.add_argument("--base", type=int, default=40001)
ap.add_argument("--loss", type=float, default=0)
ap.add_argument("--delay", type=float, default=0)
ap.add_argument("--jitter", type=float, default=0)
ap.add_argument("--dup", type=float, default=0)
ap.add_argument("--seconds", type=float, default=3600)
ap.add_argument("--stats", default="")
a = ap.parse_args()

stats = {"in": 0, "dropped": 0, "dup": 0, "out": 0}
lock = threading.Lock()
queue = []                                           # (due time, counter, data, socket, target)
cnt = 0
stop = time.time() + a.seconds

def make(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
    s.bind(("127.0.0.1", port))
    s.settimeout(0.05)
    return s

socks = [make(a.port), make(a.port + 1)]            # [0]: from seat 0, [1]: from seat 1
targets = [("127.0.0.1", a.base + 1), ("127.0.0.1", a.base)]
out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def reader(i):
    global cnt
    while time.time() < stop:
        try:
            data, _ = socks[i].recvfrom(65535)
        except socket.timeout:
            continue
        except OSError:
            continue
        with lock:
            stats["in"] += 1
            if random.random() * 100 < a.loss:
                stats["dropped"] += 1
                continue
            copies = 2 if random.random() * 100 < a.dup else 1
            if copies == 2: stats["dup"] += 1
            for _ in range(copies):
                d = max(0.0, (a.delay + random.uniform(-a.jitter, a.jitter)) / 1000.0)
                cnt += 1
                heapq.heappush(queue, (time.time() + d, cnt, data, i))

for i in (0, 1):
    threading.Thread(target=reader, args=(i,), daemon=True).start()

last = time.time()
while time.time() < stop:
    now = time.time()
    with lock:
        while queue and queue[0][0] <= now:
            _, _, data, i = heapq.heappop(queue)
            try:
                out.sendto(data, targets[i]); stats["out"] += 1
            except OSError:
                pass
    if a.stats and now - last > 2:
        last = now
        open(a.stats, "w").write("in=%d dropped=%d dup=%d out=%d\n" % (stats["in"], stats["dropped"], stats["dup"], stats["out"]))
    time.sleep(0.002)
