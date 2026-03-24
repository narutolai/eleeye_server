#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
eleeye_server 基准测试脚本

通过 TCP 连接代理，发送 UCCI 协议命令，测量：
- 连接建立时间
- 单次 "go nodes N" 往返延迟（发出命令到收到 bestmove）
- 多连接并发下的吞吐与延迟分布

用法:
  python3 benchmark_eleeye_server.py [选项]

  示例:
    python3 benchmark_eleeye_server.py                          # 默认 localhost:6000, 5 轮, nodes=216
    python3 benchmark_eleeye_server.py -p 6000 -r 20           # 20 轮
    python3 benchmark_eleeye_server.py --connections 4 -r 10   # 4 个并发连接，每连接 10 轮
    python3 benchmark_eleeye_server.py --host 192.168.1.1 -n 500 # nodes=500，远程主机
    python3 benchmark_eleeye_server.py --log my_bench.log        # 结果同时追加到 my_bench.log

  寻找最佳 (连接数 c, 轮数 r) 组合（网格搜索）:
    c=客户端连接数, r=每客户端请求轮数
    python3 benchmark_eleeye_server.py --sweep --sweep-r 5,10,20,30 --sweep-c 1,2,5,10
    python3 benchmark_eleeye_server.py --sweep --sweep-r 10,20 --sweep-c 1,5,10 --sweep-runs 3 --reuse
"""

import argparse
import socket
import time
import sys
import statistics
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime

# 默认 FEN（开局）
DEFAULT_FEN = "rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w"
# 读超时（秒）
RECV_TIMEOUT = 120.0
# 单次 recv 最大字节
BUF_SIZE = 65536


def recv_until(sock, end_marker, timeout=RECV_TIMEOUT):
    """从 sock 读取直到某行以 end_marker 开头，返回收到的完整数据。"""
    sock.settimeout(timeout)
    buf = b""
    while True:
        try:
            chunk = sock.recv(BUF_SIZE)
        except socket.timeout:
            return None
        if not chunk:
            return None
        buf += chunk
        lines = buf.decode("utf-8", errors="ignore").split("\n")
        for line in lines:
            line = line.strip()
            if line.startswith(end_marker):
                return buf.decode("utf-8", errors="ignore")
    return None


def send_cmd(sock, cmd):
    """发送一行命令（自动加换行）。"""
    if not cmd.endswith("\n"):
        cmd += "\n"
    sock.sendall(cmd.encode("utf-8"))


def one_round_latency(host, port, fen, nodes, warmup_only=False):
    """
    单连接上：连接 -> ucci -> position fen -> go nodes N -> 收 bestmove。
    返回 (连接耗时秒, [go 延迟秒列表]，是否成功)。
    warmup_only=True 时只做一次 go 用于暖机，不记入延迟。
    """
    conn_t0 = time.perf_counter()
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10.0)
        sock.connect((host, port))
    except Exception as e:
        return None, [], False
    conn_t1 = time.perf_counter()
    conn_time = conn_t1 - conn_t0

    try:
        send_cmd(sock, "ucci")
        r = recv_until(sock, "ucciok")
        if r is None:
            r = recv_until(sock, "id name")  # 有的引擎先回 id
        if r is None:
            sock.close()
            return conn_time, [], False

        send_cmd(sock, "position fen " + fen)
        send_cmd(sock, "go nodes " + str(nodes))
        t0 = time.perf_counter()
        r = recv_until(sock, "bestmove")
        t1 = time.perf_counter()
        if r is None:
            sock.close()
            return conn_time, [], False

        latencies = [] if warmup_only else [t1 - t0]

        send_cmd(sock, "quit")
        sock.close()
        return conn_time, latencies, True
    except Exception:
        try:
            sock.close()
        except Exception:
            pass
        return conn_time, [], False


def run_single_connection_reuse(host, port, fen, nodes, rounds, warmup_rounds):
    """单连接复用：连接一次，然后多轮 position+go，返回 (连接时间, 延迟列表, 成功轮数)。"""
    conn_t0 = time.perf_counter()
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10.0)
        sock.connect((host, port))
    except Exception:
        return None, [], 0
    conn_t1 = time.perf_counter()
    conn_time = conn_t1 - conn_t0
    latencies = []
    try:
        send_cmd(sock, "ucci")
        if recv_until(sock, "ucciok") is None and recv_until(sock, "id name") is None:
            sock.close()
            return conn_time, [], 0
        for i in range(warmup_rounds + rounds):
            send_cmd(sock, "positiongo fen " + fen + " nodes " + str(nodes))
            t0 = time.perf_counter()
            r = recv_until(sock, "bestmove")
            t1 = time.perf_counter()
            if r is None:
                break
            if i >= warmup_rounds:
                latencies.append(t1 - t0)
        send_cmd(sock, "quit")
        sock.close()
        return conn_time, latencies, len(latencies)
    except Exception:
        try:
            sock.close()
        except Exception:
            pass
        return conn_time, latencies, len(latencies)


def run_single_connection(host, port, fen, nodes, rounds, warmup_rounds, reuse):
    """单连接：reuse 时同一连接多轮 go，否则每轮新建连接。返回 (连接时间, 延迟列表, 成功轮数)。"""
    if reuse:
        return run_single_connection_reuse(host, port, fen, nodes, rounds, warmup_rounds)
    conn_time, latencies, ok = one_round_latency(host, port, fen, nodes, warmup_only=(warmup_rounds > 0))
    if not ok:
        return None, [], 0
    all_latencies = list(latencies)
    for _ in range(rounds - 1):
        _, latencies2, ok2 = one_round_latency(host, port, fen, nodes, warmup_only=False)
        if not ok2:
            break
        all_latencies.extend(latencies2)
    return conn_time, all_latencies, len(all_latencies)


def run_benchmark(host, port, fen, nodes, rounds, connections, warmup_rounds, reuse):
    """多连接并发：connections 个连接，每个 rounds 轮。"""
    conn_times = []
    all_latencies = []
    total_ok_rounds = 0

    def task(_):
        return run_single_connection(host, port, fen, nodes, rounds, warmup_rounds, reuse)

    with ThreadPoolExecutor(max_workers=connections) as ex:
        futures = [ex.submit(task, i) for i in range(connections)]
        for f in as_completed(futures, timeout=rounds * RECV_TIMEOUT + 60):
            try:
                ct, lats, n = f.result()
                if ct is not None:
                    conn_times.append(ct)
                if lats:
                    all_latencies.extend(lats)
                    total_ok_rounds += n
            except Exception as e:
                print("Worker error:", e, file=sys.stderr)

    return conn_times, all_latencies, total_ok_rounds


def run_sweep(host, port, fen, nodes, rounds_list, connections_list, warmup_rounds, reuse, sweep_runs, best_by):
    """
    对 (rounds, connections) 做网格搜索；每个组合跑 sweep_runs 次，取中位数。
    best_by: "throughput" | "latency" | "both"
    返回: list of (r, c, throughput_median, p99_median, raw_results_list)
    """
    results = []  # (r, c, [ (throughput, p99), ... ])
    for r in rounds_list:
        for c in connections_list:
            throughputs = []
            p99s = []
            for _ in range(sweep_runs):
                conn_times, latencies, total_ok = run_benchmark(
                    host, port, fen, nodes, r, c, warmup_rounds, reuse)
                if not latencies:
                    continue
                n = len(latencies)
                p99 = latencies[int(n * 0.99)] if n > 1 else latencies[0]
                mean_lat = statistics.mean(latencies)
                throughput = total_ok / mean_lat if mean_lat > 0 else 0
                throughputs.append(throughput)
                p99s.append(p99)
            if throughputs and p99s:
                results.append((
                    r, c,
                    statistics.median(throughputs),
                    statistics.median(p99s),
                    list(zip(throughputs, p99s)),
                ))
    # 找出最佳
    if not results:
        return results
    by_throughput = max(results, key=lambda x: x[2])
    by_latency = min(results, key=lambda x: x[3])
    return results, by_throughput, by_latency, best_by


def main():
    parser = argparse.ArgumentParser(
        description="eleeye_server 基准测试（UCCI 协议）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--host", default="127.0.0.1", help="服务器地址 (默认 127.0.0.1)")
    parser.add_argument("-p", "--port", type=int, default=6000, help="端口 (默认 6000)")
    parser.add_argument("-n", "--nodes", type=int, default=216, help="go nodes 节点数 (默认 216)")
    parser.add_argument("-r", "--rounds", type=int, default=5, help="每客户端请求轮数 r (默认 5)")
    parser.add_argument("-c", "--connections", type=int, default=1, help="客户端连接数 c (默认 1)")
    parser.add_argument("--warmup", type=int, default=0, help="每连接暖机轮数，不记入延迟 (默认 0)")
    parser.add_argument("--reuse", action="store_true", help="同一连接上多轮 go（测热延迟）")
    parser.add_argument("--fen", default=DEFAULT_FEN, help="position fen 使用的 FEN 串")
    parser.add_argument("--log", default="benchmark_eleeye_server.log", help="结果追加写入的日志文件 (默认 benchmark_eleeye_server.log)")
    # 网格搜索：找最佳 r/c 组合
    parser.add_argument("--sweep", action="store_true", help="网格搜索：对多组 (连接数c, 轮数r) 跑基准，输出表格并标出最佳")
    parser.add_argument("--sweep-r", default="5,10,20,30", help="sweep 时轮数 r 列表，逗号分隔 (默认 5,10,20,30)")
    parser.add_argument("--sweep-c", default="1,2,5,10", help="sweep 时连接数 c 列表，逗号分隔 (默认 1,2,5,10)")
    parser.add_argument("--sweep-runs", type=int, default=2, help="每个 (r,c) 组合重复次数，取中位数 (默认 2)")
    parser.add_argument("--best-by", choices=("throughput", "latency", "both"), default="both",
                        help="最佳组合按什么选: throughput=吞吐最高, latency=P99最低, both=都输出 (默认 both)")
    args = parser.parse_args()

    log_file = None
    try:
        log_file = open(args.log, "a", encoding="utf-8")
    except Exception as e:
        print("警告: 无法打开日志文件 {}: {}，仅输出到控制台".format(args.log, e), file=sys.stderr)

    def tee_print(*args, **kwargs):
        msg = " ".join(str(x) for x in args)
        print(*args, **kwargs)
        if log_file is not None:
            try:
                log_file.write(msg + "\n")
                log_file.flush()
            except Exception:
                pass

    if log_file is not None:
        tee_print("")
        tee_print("--- {} ---".format(datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    tee_print("eleeye_server 基准测试")
    tee_print("  目标: {}:{}".format(args.host, args.port))

    # 网格搜索模式：找最佳 r/c 组合
    if args.sweep:
        try:
            rounds_list = [int(x.strip()) for x in args.sweep_r.split(",") if x.strip()]
            connections_list = [int(x.strip()) for x in args.sweep_c.split(",") if x.strip()]
        except ValueError:
            tee_print("错误: --sweep-r 和 --sweep-c 应为逗号分隔的整数", file=sys.stderr)
            return 1
        if not rounds_list or not connections_list:
            tee_print("错误: --sweep-r 和 --sweep-c 至少各有一个整数", file=sys.stderr)
            return 1
        tee_print("  模式: 网格搜索 (sweep)")
        tee_print("  轮数 r={}, 连接数 c={}, 每组合跑 {} 次, nodes={}, reuse={}".format(
            rounds_list, connections_list, args.sweep_runs, args.nodes, args.reuse))
        tee_print("")
        ret = run_sweep(
            args.host, args.port, args.fen, args.nodes,
            rounds_list, connections_list, args.warmup, args.reuse,
            args.sweep_runs, args.best_by,
        )
        if len(ret) == 4:
            results, by_throughput, by_latency, best_by = ret
        else:
            results = ret
            by_throughput = by_latency = None
        if not results:
            tee_print("网格搜索: 无有效数据（请确认服务已启动且可连接）")
            return 1
        # 按 (r, c) 排序输出表格。c=连接数, r=每客户端轮数
        tee_print("(c=连接数, r=每客户端轮数 | 每格: 吞吐(go/秒) / P99(秒)，均为中位数)")
        tee_print("")
        header = "     c= " + "  ".join("{:>6}".format(c) for c in connections_list)
        tee_print(header)
        by_r = {}
        for r, c, tp, p99, _ in results:
            by_r.setdefault(r, {})[c] = (tp, p99)
        for r in sorted(by_r.keys()):
            row = "r={:2} ".format(r)
            for c in connections_list:
                if c in by_r[r]:
                    tp, p99 = by_r[r][c]
                    row += " {:5.1f}/{:.2f}".format(tp, p99)
                else:
                    row += "   -/-  "
            tee_print(row)
        tee_print("")
        if by_throughput is not None and best_by in ("throughput", "both"):
            r, c, tp, p99, _ = by_throughput
            tee_print("【按吞吐最高】最佳: 连接数 c={}, 轮数 r={} -> 吞吐 {:.2f} go/秒, P99 {:.3f}s".format(c, r, tp, p99))
        if by_latency is not None and best_by in ("latency", "both"):
            r, c, tp, p99, _ = by_latency
            tee_print("【按P99最低】最佳: 连接数 c={}, 轮数 r={} -> 吞吐 {:.2f} go/秒, P99 {:.3f}s".format(c, r, tp, p99))
        tee_print("")
        if log_file is not None:
            try:
                log_file.close()
            except Exception:
                pass
        return 0

    tee_print("  nodes={}, rounds={}, connections={}, warmup={}, reuse={}".format(
        args.nodes, args.rounds, args.connections, args.warmup, args.reuse))
    tee_print("")

    conn_times, latencies, total_ok = run_benchmark(
        args.host, args.port, args.fen, args.nodes,
        args.rounds, args.connections, args.warmup, args.reuse,
    )

    if conn_times:
        tee_print("连接时间 (秒):")
        tee_print("  次数: {}, 最小: {:.3f}, 最大: {:.3f}, 平均: {:.3f}".format(
            len(conn_times), min(conn_times), max(conn_times), statistics.mean(conn_times)))
    else:
        tee_print("连接: 无成功连接")

    if not latencies:
        tee_print("go 延迟: 无有效数据")
        if log_file is not None:
            try:
                log_file.close()
            except Exception:
                pass
        return 1

    latencies.sort()
    n = len(latencies)
    p50 = latencies[n // 2] if n else 0
    p99 = latencies[int(n * 0.99)] if n > 1 else latencies[0]
    tee_print("go nodes {} 往返延迟 (秒):".format(args.nodes))
    tee_print("  样本数: {}, 最小: {:.3f}, 最大: {:.3f}, 平均: {:.3f}, 中位数: {:.3f}, P99: {:.3f}".format(
        n, min(latencies), max(latencies), statistics.mean(latencies), p50, p99))
    if total_ok > 0:
        tee_print("  总成功轮数: {}, 估算吞吐: {:.2f} go/秒 (按平均延迟)".format(
            total_ok, total_ok / statistics.mean(latencies) if latencies else 0))
    tee_print("")
    if log_file is not None:
        try:
            log_file.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
