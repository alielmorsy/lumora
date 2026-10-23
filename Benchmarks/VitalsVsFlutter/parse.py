"""Summarize bench_<tag>.txt files produced by bench.sh. Usage: python parse.py bench_lumora.txt bench_flutter.txt"""
import re
import statistics
import sys
from collections import defaultdict

INF = 9223372036854775807


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(p / 100 * (len(xs) - 1))))] if xs else float("nan")


def parse(path):
    starts, phases, lat, thr, mem = [], {}, defaultdict(set), defaultdict(list), defaultdict(list)
    period = None
    for line in open(path, encoding="utf-8", errors="replace"):
        f = line.split()
        if not f:
            continue
        if f[0] == "start" and "TotalTime:" in line:
            starts.append(int(line.split("TotalTime:")[1]))
        elif f[0] in ("begin", "end"):
            phases.setdefault(f[1], {})[f[0]] = (float(f[2]), int(f[3]), int(f[4]), int(f[5]))
        elif f[0] == "lat":
            if len(f) == 3:
                period = int(f[2])
            elif len(f) == 5 and f[3] not in ("0", str(INF)):
                lat[f[1]].add(int(f[3]))
        elif len(f) >= 4 and f[1] == "thr":
            thr[f[0]].append((" ".join(f[2:-1]), int(f[-1])))
        elif f[0] == "mem":
            mem[f[1]].append(line.split(None, 2)[2] if len(f) > 2 else "")
    return starts, phases, lat, thr, mem, period


def mem_summary(lines):
    text = "".join(lines)
    out = {}
    for key, pat in [("PSS", r"TOTAL PSS:\s+(\d+)"), ("RSS", r"TOTAL RSS:\s+(\d+)"), ("Java", r"Java Heap:\s+(\d+)"),
                     ("Native", r"Native Heap:\s+(\d+)"), ("Code", r"Code:\s+(\d+)"), ("Graphics", r"Graphics:\s+(\d+)"),
                     ("Private Other", r"Private Other:\s+(\d+)"), ("System", r"System:\s+(\d+)")]:
        m = re.search(pat, text)
        out[key] = int(m.group(1)) / 1024 if m else float("nan")
    m = re.search(r"Threads:\s+(\d+)|Activities", text)
    return out


def report(path):
    starts, phases, lat, thr, mem, period = parse(path)
    vsync = (period or 11_111_111) / 1e6
    print(f"== {path}")
    print(f"cold start TotalTime ms: {starts}  median(excl. 1st)={statistics.median(starts[1:]) if len(starts) > 1 else 'n/a'}")
    rows = {}
    for name, ph in phases.items():
        if "begin" not in ph or "end" not in ph:
            continue
        (u0, p0, b0, t0), (u1, p1, b1, t1) = ph["begin"], ph["end"]
        dur = u1 - u0
        app_cpu = (p1 - p0) / 100 / dur * 100
        sys_cpu = (b1 - b0) / (t1 - t0) * 100
        # SF stamps use another clock than /proc/uptime; the ring is cleared at phase start so all samples belong here.
        pres = sorted(lat[name])
        iv = [(b - a) / 1e6 for a, b in zip(pres, pres[1:])]
        fps = (len(pres) - 1) / ((pres[-1] - pres[0]) / 1e9) if len(pres) > 1 else 0
        jank = sum(1 for x in iv if x > vsync * 1.5)
        big = sum(1 for x in iv if x > 50)
        print(f"  [{name}] {dur:.1f}s  appCPU={app_cpu:.1f}% (of 1 core)  sysCPU={sys_cpu:.1f}% (all 8 cores)  "
              f"frames={len(pres)} fps={fps:.1f}  interval p50={pct(iv, 50):.1f} p95={pct(iv, 95):.1f} p99={pct(iv, 99):.1f} max={max(iv) if iv else 0:.1f}ms  "
              f"jank(>{vsync * 1.5:.1f}ms)={jank} ({(jank / len(iv) * 100 if iv else 0):.1f}%)  >50ms={big}")
        snaps = thr.get(name, [])
        if snaps:
            n = len(snaps) // 2
            before, after = defaultdict(int), defaultdict(int)
            for k, v in snaps[:n]:
                before[k] += v
            for k, v in snaps[n:]:
                after[k] += v
            d = sorted(((after[k] - before.get(k, 0), k) for k in after), reverse=True)[:6]
            print("     threads: " + ", ".join(f"{k}={v / 100 / dur * 100:.1f}%" for v, k in d if v > 0))
        ms = mem_summary(mem.get(name, []))
        print("     mem MB: " + "  ".join(f"{k}={v:.1f}" for k, v in ms.items()))
        rows[name] = (app_cpu, sys_cpu, fps, ms)
    return rows


for p in sys.argv[1:]:
    report(p)
