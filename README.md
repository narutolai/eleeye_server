# 象眼引擎与网络代理（ElephantEye + TCP 代理）

本目录在 **象眼 ElephantEye**（UCCI 中国象棋引擎，xqbase）源码基础上，做了三类工作：

1. **引擎侧改造**：协议扩展与揭棋模式。  
2. **网络代理服务**：把原本「标准输入 / 标准输出」的引擎进程，暴露为 **TCP 服务**，供局域网或服务器上的客户端（如游戏服、脚本）远程调用。  
3. **构建脚本**：`makefile.sh` 中汇总了引擎与代理的编译命令。

---

## 项目组成

| 部分 | 说明 |
|------|------|
| `eleeye.cpp`、`ucci.*`、`search.*`、`position.*` 等 | 象眼引擎本体：解析 UCCI 指令、搜索、输出着法等。 |
| `eleeye_server.c` | **象眼引擎网络代理**：预拉起多路引擎子进程（引擎池），用 **epoll** 转发 TCP 客户端与引擎之间的数据。 |

代理与引擎通过 **管道** 连接：客户端发来的文本行写入引擎 `stdin`，引擎 `stdout` 原样写回客户端。

---

## 相对原生象眼的引擎改造

### 1. `positiongo` 指令

将 **`position` 与 `go` 合并为一条指令**，减少往返，适合网络或批处理场景。解析与执行逻辑在 `ucci` 与 `eleeye.cpp` 主循环中（`UCCI_COMM_POSITIONGO`）。

### 2. `eleeyeok` 同步行

引擎在处理完 **每一条** 已识别的 UCCI 指令后，会在 stdout 额外输出一行：

```text
eleeyeok
```

**用途**：

- 本地脚本可按行解析，明确「本条命令已处理完」。  
- **代理服务器**用「输入的行数（以 `\n` 计）」与「输出中 `eleeyeok` 行数」对齐，判断一轮请求是否结束，从而在引擎池里 **释放槽位**、供其他连接复用。

> 注意：若某条分支未打印 `eleeyeok` 或与输入行数不一致，会导致代理占槽不释放或提前解绑，修改引擎时需保持约定。

### 3. 揭棋（暗棋）模式

使用编译宏 **`DJIEQIMODE`** 编译出揭棋用引擎可执行文件（见下方 `makefile.sh` 中的 `ELEEYE_JIEQI.EXE`）。规则与局面与标准象棋不同，需配合揭棋版代理（`-DJIEQI`）及对应端口部署。

---

## 网络代理行为概要

- 启动时 **fork** 多个引擎子进程（默认 **16** 路，`ENGINE_POOL_SIZE`），每路独占一对管道。  
- 客户端 **先连 TCP**，有 **完整一行**（以 `\n` 结尾）再 **绑定** 空闲引擎槽；池满时数据暂存，定时重试分配。  
- 仅转发 **到最后一个换行符为止** 的完整行块给引擎，并统计本次发送的行数。  
- 从引擎输出中统计以 `eleeyeok` 开头的行；当 **`eleeyeok` 数量 ≥ 已发送命令行数** 时，解除该客户端与槽位的绑定（引擎进程不退出，继续服务其他连接）。  
- 支持 **PID 文件防重复启动**、**`-d` 守护进程**、日志写入文件（路径随编译宏变化）。

默认 **象眼** 代理：`ELEEYE.EXE`、默认端口 **6000**，日志 `xiangyan_server.log`，PID `eleeye_server.pid`。  
**揭棋** 代理：`ELEEYE_JIEQI.EXE`、默认端口 **6001**，日志 `xiangyan_jieqi_server.log`。  

源码中还可通过 **`PIKAFISH`** 宏编译「皮卡鱼」路径与端口的变体（`makefile.sh` 里可按需启用）。

---

## 编译

在 **`eleeye` 目录**下执行（或参考其中命令自行调整优化选项）：

```bash
bash makefile.sh
```

脚本大致会做：

- 编译标准象眼：`ELEEYE.EXE`  
- 编译揭棋引擎：`ELEEYE_JIEQI.EXE`（`-DJIEQIMODE`）  
- 编译代理：`eleeye_server`（默认象眼）、`eleeye_jieqiserver`（`-DJIEQI`）

请保证 **`BOOK.DAT` 等引擎依赖文件** 与可执行文件的工作目录一致（或按象眼原有规则配置 `bookfiles` 等选项）。

代理通过 **`execl(ENGINE_PATH, ...)`** 启动引擎，可执行文件名需与 `eleeye_server.c` 中宏一致（如 `ELEEYE.EXE` / `ELEEYE_JIEQI.EXE`），或修改宏后重新编译代理。

---

## 运行代理

```bash
./eleeye_server              # 前台，默认端口 6000
./eleeye_server -p 8080      # 指定端口
./eleeye_server -d           # 守护进程
./eleeye_server -h           # 帮助
```

揭棋服务使用编译生成的 `eleeye_jieqiserver`（端口默认 6001）。  

查看日志示例：

```bash
tail -f xiangyan_server.log
```

停止：对 PID 文件中的进程发送 `SIGTERM`，或 `kill` 对应进程（代理退出时会清理子引擎）。

---

## 客户端交互示例（Telnet / 自写 TCP）

连接后按 **UCCI** 发送文本行，例如：

```text
ucci
isready
position fen rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1
go depth 3
quit
```

每行应以 `\n` 结束；引擎会输出 `info`、`bestmove` 等，并在合适时机输出 `eleeyeok`。具体指令格式以 **UCCI 协议**及本仓库对 `positiongo` 的扩展为准。

本地子进程调用示例可参考 `test.lua`（直接 `popen` 引擎可执行文件，不经过 TCP）。

---

## 上游与许可

象眼引擎主体来自 **ElephantEye**（Morning Yellow / xqbase），以 **LGPL** 等许可发布；`ucci.h` / `ucci.cpp` 部分声明可与 LGPL 部分区分，使用前请阅读源码内版权与许可说明。本仓库中的 **代理与引擎改动** 为项目维护者在此基础上增加的功能，部署与再分发时请同时遵守上游许可。

---

## 文件速查

| 文件 | 作用 |
|------|------|
| `eleeye.cpp` | 引擎主循环：UCCI 指令分发、`eleeyeok`、`positiongo` 等 |
| `ucci.h` / `ucci.cpp` | UCCI 解析与指令结构 |
| `eleeye_server.c` | TCP 代理、引擎池、epoll |
| `makefile.sh` | 一键编译引擎与代理 |
| `test.lua` | 本地管道调用引擎的示例 |

如有文档与代码不一致，以源码为准。
