# eleeye_server 基准测试说明

## 1. 准备

- 先启动 eleeye_server（默认端口 6000）：
  ```bash
  ./eleeye_server              # 前台
  # 或
  ./eleeye_server -d -p 6000  # 守护进程
  ```
- 本机需有 Python 3（脚本仅用标准库）。

## 2. 运行基准测试

脚本：`benchmark_eleeye_server.py`，通过 TCP 连到代理，按 UCCI 协议发 `ucci`、`position fen`、`go nodes N`，统计**连接时间**和 **go 往返延迟**（发出命令到收到 `bestmove`）。

```bash
cd /path/to/3rd/eleeye/eleeye

# 默认：1 连接，5 轮，nodes=216，每轮新建连接
python3 benchmark_eleeye_server.py

# 指定端口、轮数、节点数
python3 benchmark_eleeye_server.py -p 6000 -r 20 -n 500

# 同一连接上多轮 go（测“热”延迟，不含建连）
python3 benchmark_eleeye_server.py --reuse -r 10 -n 216

# 多连接并发（压测）
python3 benchmark_eleeye_server.py -c 4 -r 10 -n 216

# 远程主机
python3 benchmark_eleeye_server.py --host 192.168.1.1 -p 6000 -r 5

# 网格搜索：自动找最佳 rounds(r) 与 connections(c) 组合
python3 benchmark_eleeye_server.py --sweep --sweep-r 5,10,20,30 --sweep-c 1,2,5,10 --reuse
python3 benchmark_eleeye_server.py --sweep --sweep-r 10,20 --sweep-c 1,5,10 --sweep-runs 3 --best-by throughput
```

## 3. 参数说明

| 参数 | 含义 | 默认 |
|------|------|------|
| `--host` | 服务器地址 | 127.0.0.1 |
| `-p / --port` | 端口 | 6000 |
| `-n / --nodes` | `go nodes` 节点数 | 216 |
| `-r / --rounds` | 每客户端请求轮数 r | 5 |
| `-c / --connections` | 客户端连接数 c | 1 |
| `--warmup` | 每连接暖机轮数（不记入延迟） | 0 |
| `--reuse` | 同一连接多轮 go（热延迟） | 否 |
| `--fen` | position fen 使用的 FEN | 默认开局 |
| `--sweep` | 开启网格搜索，找最佳 r/c 组合 | 否 |
| `--sweep-r` | 网格搜索时「轮数 r」列表（逗号分隔） | 5,10,20,30 |
| `--sweep-c` | 网格搜索时「连接数 c」列表（逗号分隔） | 1,2,5,10 |
| `--sweep-runs` | 每个 (r,c) 组合重复次数（取中位数） | 2 |
| `--best-by` | 最佳依据：throughput / latency / both | both |

## 4. 如何找到最佳「连接数 c」和「轮数 r」组合

- **c** = 客户端连接数（多少个客户端连上来）
- **r** = 轮数（每个客户端发几轮请求）

两种方式任选：

**方式一：网格搜索（推荐）**  
使用 `--sweep` 一次跑多组 (连接数 c, 轮数 r)，脚本会输出表格并标出「按吞吐最高」和「按 P99 最低」的最佳组合：

```bash
# 默认 sweep 范围：轮数 r=5,10,20,30，连接数 c=1,2,5,10；每组合跑 2 次取中位数
python3 benchmark_eleeye_server.py --sweep --reuse

# 自定义范围和重复次数（轮数 r、连接数 c）
python3 benchmark_eleeye_server.py --sweep --sweep-r 10,20,30 --sweep-c 1,5,10,20 --sweep-runs 3 --reuse

# 只关心吞吐最高的组合
python3 benchmark_eleeye_server.py --sweep --best-by throughput --reuse
```

输出示例：表格中每格为「吞吐/P99」；文末会打印【按吞吐最高】和【按P99最低】的最佳 (连接数 c, 轮数 r)。

**方式二：手动多组对比**  
见下文 6.3，固定 nodes 与 reuse，只改 `-c` 或 `-r`，多次运行记录「估算吞吐」和 P99，自己填表对比。

## 5. 输出含义

- **连接时间**：从 `connect()` 到建立成功的时间（秒）；多连接时统计多次建连。
- **go nodes N 往返延迟**：从发出 `go nodes N` 到收到含 `bestmove` 的响应的时间（秒）；给出样本数、最小/最大/平均/中位数/P99。
- **总成功轮数 / 估算吞吐**：按平均延迟估算的 go/秒（请求/秒）。

## 6. 从结果推算「单进程能撑多少并发、多少 QPS」

基准测试连的是**一个** eleeye_server 进程，所以同一组 `-c / -r / -n` 的结果就是**该单进程**的表现。按下面做即可得到「能撑多少并发」和「多少 QPS」。

### 6.1 看「多少 QPS」

- 脚本输出的 **「估算吞吐: X.XX go/秒」** 就是：在**当前并发连接数**下，这个进程**每秒能完成的 go 请求数**，即 **QPS**。
- 单连接时：`-c 1 --reuse -r 50` 得到的吞吐 ≈ 单连接下的 QPS（基线）。
- 多连接时：例如 `-c 10 --reuse -r 30` 得到的吞吐 = 10 个并发连接时，该**单进程**的总 QPS。

**结论**：  
- **单进程 QPS** = 某次压测输出的「估算吞吐」数值（对应那次用的 `-c`）。  
- 想得到「单进程能撑的大致最大 QPS」：逐步加大 `-c`（见下），看总吞吐何时不再明显上升，那时的吞吐就是该进程在该负载下的**饱和 QPS**。

### 6.2 同一组参数要不要多跑几次？

**建议：同一组参数跑 3～5 次**，再取「估算吞吐」和「P99 延迟」的**中位数或平均值**作为该参数下的代表值。单次结果可能受冷启动、系统负载、调度等影响，多跑几次更稳。

示例：对 `-c 5` 跑 5 次，只保留关键行便于抄表：
```bash
for i in 1 2 3 4 5; do
  echo "=== run $i ==="
  python3 benchmark_eleeye_server.py --reuse -r 30 -n 216 -c 5 | grep -E "估算吞吐|P99"
done
```
记录下 5 次的「估算吞吐」和 P99，取中位数填进表即可。

### 6.3 看「能撑多少并发」

1. **固定负载**：用同一批参数（例如 `--reuse -r 30 -n 216`），只改并发数 `-c`，做多组测试；**每组建议跑 3～5 次取中位数**。例如：
   ```bash
   python3 benchmark_eleeye_server.py --reuse -r 30 -n 216 -c 1
   python3 benchmark_eleeye_server.py --reuse -r 30 -n 216 -c 5
   python3 benchmark_eleeye_server.py --reuse -r 30 -n 216 -c 10
   python3 benchmark_eleeye_server.py --reuse -r 30 -n 216 -c 20
   python3 benchmark_eleeye_server.py --reuse -r 30 -n 216 -c 50
   # 视情况继续 -c 100 等
   ```
2. **每次记录三样**：
   - **估算吞吐**（go/秒）→ 即该并发下的 QPS；
   - **P99 延迟**（秒）→ 看延迟是否还能接受；
   - 是否有报错或「无有效数据」→ 是否有连接/超时失败。
3. **如何定「能撑多少并发」**（任选一种或组合使用）：
   - **按延迟**：例如要求 P99 &lt; 3 秒，则「能撑的并发」= 满足该条件的最大 `-c`。
   - **按饱和**：当再加大 `-c` 时，总 QPS 几乎不涨甚至下降，说明已到瓶颈，可取「QPS 最大时」对应的 `-c` 作为可用的高并发数。
   - **按稳定**：出现大量超时/失败时的 `-c` 已经超标，取上一个没问题的 `-c` 作为「能撑的并发」。

把上述几组 `-c` 对应的「估算吞吐」和 P99 记成表，就能直接回答：**一台机上一个进程能撑多少并发、多少 QPS**（在相同 nodes 和 rounds 条件下）。

### 6.4 简单记录表示例

| 并发 -c | 估算吞吐 (go/秒) | P99 延迟 (秒) | 备注     |
|---------|------------------|----------------|----------|
| 1       | 2.5              | 0.40           | 基线     |
| 5       | 11.2             | 0.45           |          |
| 10      | 20.1             | 0.50           |          |
| 20      | 35.0             | 0.57           |          |
| 50      | 38.2             | 1.30           | 接近饱和 |
| 100     | 35.0             | 2.85           | 延迟变差 |

解读示例：若要求 P99 &lt; 2 秒，则「能撑的并发」约 50；该进程在该负载下「能撑的 QPS」约 38 go/秒。

---

## 7. 建议测试场景

- **单连接热延迟**：`python3 benchmark_eleeye_server.py --reuse -r 20 -n 216`
- **单连接冷延迟**（含建连）：`python3 benchmark_eleeye_server.py -r 20 -n 216`
- **并发能力**：`python3 benchmark_eleeye_server.py -c 10 -r 5 -n 216`
- **不同节点数**：`python3 benchmark_eleeye_server.py --reuse -r 10 -n 500` 与 `-n 1000` 对比

## 8. 手动快速验证（无 Python 时）

```bash
# 单次：连接 -> ucci -> position -> go nodes 216 -> 看响应时间
( echo ucci; sleep 0.5; echo "position fen rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1"; echo "go nodes 216"; sleep 10; echo quit ) | nc -v localhost 6000
```

用 `time` 包一层可粗略看总耗时：`time ( echo ucci; ... ) | nc localhost 6000`。
