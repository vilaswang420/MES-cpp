#!/usr/bin/env python3
"""5.6 GA 2h 压测 — 后端进程内存/连接泄漏趋势采集 (配合 perf/k6/m3_ga.js).

在压测目标服务器上运行 (Linux, 读 /proc, 无第三方依赖):
  python3 perf/k6/ga_leak_monitor.py --pid $(pidof mes-backend) --port 8088 \
      --duration 7200 --out ga_leak.csv

采样 (默认 30s 一次) 并写 CSV:
  ts, uptime_s, rss_kb, vsz_kb, threads, fd_count, tcp_established(目标端口)

结束时输出趋势判定 (验收: "内存/连接无泄漏趋势"):
  - RSS/fd/TCP 各做一次最小二乘线性回归, 报 slope/小时;
  - 并对比 前1/4 段均值 vs 后1/4 段均值 (排除爬坡期抖动);
  - 判定线 (可用 --leak-rss-mb-h 等覆盖):
      RSS  斜率 > 20 MB/h 且后段均值 > 前段均值 * 1.10  => 疑似泄漏
      fd   斜率 > 100 /h  且后段 > 前段 + 50            => 疑似泄漏
      TCP  斜率 > 50  /h  且后段 > 前段 + 20            => 疑似泄漏
      (注意: WS 1000 连接稳态本身持有 ~1000 ESTABLISHED + ~1000 fd,
       判定看的是"持续增长斜率"而非绝对值。)
"""
import argparse
import csv
import glob
import os
import time


def find_pid(pid_arg: str, name: str) -> int:
    if pid_arg:
        return int(pid_arg)
    for d in glob.glob("/proc/[0-9]*"):
        try:
            with open(os.path.join(d, "comm")) as f:
                if f.read().strip() == name:
                    return int(d.rsplit("/", 1)[1])
        except OSError:
            continue
    raise SystemExit(f"未找到进程 {name}, 请用 --pid 指定 (pidof {name})")


def read_status(pid: int):
    rss = vsz = threads = 0
    with open(f"/proc/{pid}/status") as f:
        for line in f:
            if line.startswith("VmRSS:"):
                rss = int(line.split()[1])
            elif line.startswith("VmSize:"):
                vsz = int(line.split()[1])
            elif line.startswith("Threads:"):
                threads = int(line.split()[1])
    return rss, vsz, threads


def count_fds(pid: int) -> int:
    try:
        return len(os.listdir(f"/proc/{pid}/fd"))
    except OSError:
        return -1


# /proc/net/tcp established 中目标端口 (hex) 的连接数 (含 v6)
def count_tcp(port: int) -> int:
    want = f":{port:04X}"
    n = 0
    for path in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            with open(path) as f:
                next(f)
                for line in f:
                    parts = line.split()
                    if parts[3] == "01" and parts[1].endswith(want):
                        n += 1
        except OSError:
            continue
    return n


def slope_per_hour(xs, ys):
    """最小二乘斜率, x 秒 -> 每小时变化量"""
    n = len(xs)
    if n < 4:
        return 0.0
    mx, my = sum(xs) / n, sum(ys) / n
    denom = sum((x - mx) ** 2 for x in xs)
    if denom == 0:
        return 0.0
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / denom * 3600.0


def verdict(name, vals, slope_h, leak_slope, growth_ratio, min_growth):
    q = max(1, len(vals) // 4)
    first, last = sum(vals[:q]) / q, sum(vals[-q:]) / q
    leak = slope_h > leak_slope and last > first * growth_ratio and (last - first) > min_growth
    trend = "LEAK-SUSPECT" if leak else "OK"
    print(f"  {name:<6} slope={slope_h:>12.1f}/h  前1/4均值={first:>12.1f}  "
          f"后1/4均值={last:>12.1f}  => {trend}")
    return leak


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", default="", help="后端进程 PID (缺省按 comm=mes-backend 查找)")
    ap.add_argument("--name", default="mes-backend")
    ap.add_argument("--port", type=int, default=8088, help="后端监听端口 (TCP established 统计)")
    ap.add_argument("--interval", type=float, default=30.0)
    ap.add_argument("--duration", type=float, default=7200.0)
    ap.add_argument("--out", default="ga_leak.csv")
    ap.add_argument("--leak-rss-mb-h", type=float, default=20.0)
    ap.add_argument("--leak-fd-h", type=float, default=100.0)
    ap.add_argument("--leak-tcp-h", type=float, default=50.0)
    args = ap.parse_args()

    pid = find_pid(args.pid, args.name)
    print(f"监控 pid={pid} port={args.port} duration={args.duration:.0f}s "
          f"interval={args.interval:.0f}s out={args.out}")

    rows = []
    t0 = time.time()
    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["ts", "uptime_s", "rss_kb", "vsz_kb", "threads", "fd_count", "tcp_established"])
        while time.time() - t0 < args.duration:
            rss, vsz, threads = read_status(pid)
            fds = count_fds(pid)
            tcp = count_tcp(args.port)
            up = round(time.time() - t0, 1)
            w.writerow([int(time.time()), up, rss, vsz, threads, fds, tcp])
            f.flush()
            rows.append((up, rss, fds, tcp))
            if int(up) % 600 < args.interval:  # 每 10 分钟打一行进度
                print(f"  [{up:>7.0f}s] rss={rss/1024:.0f}MB fd={fds} tcp={tcp}")
            time.sleep(args.interval)

    print("\n=== 泄漏趋势判定 (GA 验收: 无泄漏趋势) ===")
    xs = [r[0] for r in rows]
    rss = [r[1] for r in rows]
    fds = [r[2] if r[2] >= 0 else 0 for r in rows]
    tcps = [r[3] for r in rows]
    leak_rss = verdict("RSS(KB)", rss, slope_per_hour(xs, rss),
                       args.leak_rss_mb_h * 1024, 1.10, 10 * 1024)
    leak_fd = verdict("FD", fds, slope_per_hour(xs, fds), args.leak_fd_h, 1.05, 50)
    leak_tcp = verdict("TCP", tcps, slope_per_hour(xs, tcps), args.leak_tcp_h, 1.05, 20)
    ok = not (leak_rss or leak_fd or leak_tcp)
    print(f"\n结论: {'PASS - 无泄漏趋势' if ok else 'FAIL - 疑似泄漏, 结合 CSV 曲线复核'}")
    raise SystemExit(0 if ok else 2)


if __name__ == "__main__":
    main()
