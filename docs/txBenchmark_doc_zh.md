# txBenchmark

面向 [vidb](../vidbsvc/vidb.go) 的全随机、区块链转账风格流式负载。它覆盖了 KV 引擎的「加载 + 事务更新」完整路径。

## 源码

[cmd/txBenchmark.go](../cmd/txBenchmark.go)（Cobra 命令 `txBenchmark`）。

## 设计目标

- 度量「区块链转账」工作负载的 **端到端吞吐**：读两个余额、做余额校验、写两个新余额，整个过程都在批量事务提交下进行。
- 支持 **十亿级账户和交易数**，且工作集常驻内存可控。
- 通过 `--seed` 保证可复现。
- 将 load 阶段（纯写）与事务阶段（读-改-写）**分开计时**，便于独立分析。

## 数据模型

| 维度 | 取值 |
|------|------|
| 账户数量 | `--num-accounts`（默认 10000） |
| Key 编码 | 16 位十进制零填充序号 + 固定前缀；总长度由 `--key-size` 控制（默认 32，范围 1..1024） |
| Key 示例 | `-account0000000000000042`（key-size=24 时） |
| Value 编码 | `uint64` little-endian，8 字节 |
| 初始余额 | 在 `[0, --initial-balance-max]` 内均匀采样 |

## 参数（Flags）

| Flag | 默认值 | 说明 |
|------|--------|------|
| `--num-accounts` | 10000 | 生成的账户总数 |
| `--num-transactions` | 100000 | 生成的交易总数 |
| `--tx-per-block` | 1000 | 每个 block 包含的交易数 |
| `--initial-balance-max` | 1000000 | 初始余额上限（uint64，在 `[0, val]` 内均匀） |
| `--max-value` | 100 | 单笔转账金额上限（uint64，在 `[1, val]` 内均匀） |
| `--batch-size` | 5000 | 单个事务最多包含的 put 数 |
| `--commit-every` | 100 | 每 N 个事务调用一次 `db.Commit`（0 关闭） |
| `--max-blocks` | 0 | 跑满多少个 block 后停止（0 表示不限制） |
| `--seed` | 42 | RNG 种子，用于复现 |
| `--result-path` | `<dataPath>/txBenchmark.csv` | 结果 CSV 输出路径（不存在会自动创建） |
| `--key-size` | 32 | 每个账户 key 的字节数，1..1024 |
| `--dataPath` | `testdata/paper/txBenchmark` | vidb 数据目录 |
| `--cacheCost` | 1 GiB | 全局 cache 内存预算 |
| `--VlogSize` | 1 | Vlog 文件大小（GB） |

## 处理流程

```
+----------------------+      +-------------------------+
| 1) 流式加载           |      | 2) 流式运行              |
|  - numAccounts 个     | ---> |  - numTransactions 笔  |
|  - 每个账户 1 次 Put  |      |  - 按 txPerBlock 聚合   |
|  - 按 batch-size 切   |      |  - 每 block: 2 次读     |
|    为多个事务          |      |    + 0..2 次写          |
|                      |      |  - 每 batch-size 一次提交|
+----------------------+      +-------------------------+
                                          |
                                          v
                                +-------------------------+
                                | 3) 结果 CSV             |
                                | TotalTx, ExecutedTx,    |
                                | Skipped{NotFound,Low-   |
                                | Balance}, BlockCount,   |
                                | Elapsed(s), Throughput  |
                                +-------------------------+
```

### 1. 流式加载阶段（`streamLoadAccounts`）

- 一次生成一个账户，累积到一个容量不超过 `batch-size` 的 `[][2][]byte` 批次中。
- 每当批次填满，就通过 `vidbsvc.UpdateBatchKeyValue(ins, batch, seq, commitEvery)` 一次性事务写入。
- `UpdateBatchKeyValue` 返回的耗时累加到 `stats.loadElapsed`（load 阶段唯一的计时来源）。
- 最后一批不足 `batch-size` 的尾部数据会被一并 flush。
- **内存上限**：1 个批次。

### 2. 流式运行阶段（`streamRunRandomBlocks`）

- 通过 `generateRandomBlock` 一次只构造 `--tx-per-block` 条交易组成的单个 block，然后立即调用 `executeBlock`；永不把全部 tx 列表物化到内存。
- 每个 block：
  1. 对每条 entry 读 `from` 和 `to` 的当前余额（每条 tx 2 次 `ins.Get`）。
  2. 套用 skip 规则；若双方都存在且 `from >= value`，计算 `from - value` / `to + value`，并入 2 个 `Put`。
  3. 将累积的 `Put` 按 `batch-size` 拆成多个事务；每个事务通过 `vidbsvc.UpdateBatchKeyValue(ins, kvs, seq, commitEvery)` 提交。
- `stats.txElapsed` 累加 `UpdateBatchKeyValue` 返回的时间，加上每条 entry 的读耗时。
- block 切片通过 `block[:0]` 复用，避免反复分配。

#### Skip 规则

满足以下任一条件时跳过该笔交易（会被计入统计，不会被静默丢弃）：

- `from` 在 DB 中不存在（计入 `skippedNotFound`）；或
- `to` 在 DB 中不存在（计入 `skippedNotFound`）；或
- `from_balance < value`（计入 `skippedLowBalance`）。

### 3. 结果 CSV

默认输出到 `<dataPath>/txBenchmark.csv`；通过 `--result-path` 可指定其他路径（不存在会自动创建父目录）。列定义：

```
TotalTx, ExecutedTx, SkippedNotFound, SkippedLowBalance, BlockCount, Elapsed(s), Throughput(tx/sec)
```

## 账户选择：80/20 热冷分布

由 [`pickAccount8020`](../cmd/txBenchmark.go#L323-L344) 实现：

- 80% 概率：从账户索引的**前 20%** 中均匀采样（热区）。
- 20% 概率：从剩余 **80%** 索引中均匀采样（冷区）。

这是对 Zipf-like 分布的一种粗粒度、前缀聚集式近似。在给定 seed 时完全确定，但**并不等价于均匀分布**。

> 注意：热点账户集中在低索引段；如果你希望热点访问在 key 空间中**均匀散布**，请参考 [Future work](#future-work) 章节。



## 常用命令

冒烟测试（1e5 笔交易）：

```bash
./letus-vidb txBenchmark \
  --num-accounts 10000 \
  --num-transactions 100000 \
  --dataPath testdata/paper/txBenchmark \
  --result-path results/tx_smoke.csv
```

扩展规模（1e8 账户，1e6 交易）：

```bash
./letus-vidb txBenchmark \
  --num-accounts 100000000 \
  --num-transactions 1000000 \
  --dataPath testdata/paper/txScaling_1e8 \
  --result-path results/tx_1e8.csv
```

调整 key 大小：

```bash
./letus-vidb txBenchmark --key-size 64 --num-accounts 1000000
```

也可以直接使用脚本 [tx_benchmark_scaling.sh](../tx_benchmark_scaling.sh) 一次性扫描 `[5e7, 1e8, 5e8, 1e9]` 多个规模。

## 扩展性特征

- **加载阶段内存**：O(`batch-size`)。
- **运行阶段内存**：O(`tx-per-block`)（block 本身）+ O(`batch-size`)（put 队列）。
- **在 1e9 量级下仍能稳定运行**，前提是宿主机磁盘足够（默认 `VlogSize=1 GB` 对 1e9 条目太小，需相应调大，比如 `32 GB`）。

## 与其他 benchmark 的关系

| Benchmark | 对比维度 |
|-----------|----------|
| `microBenchmark`（cache / point / range） | 隔离的微操作 vs. 混合事务负载 |
| `multiTree` | 多棵 LSM-tree 共享同一 cache 时的吞吐 |
| `ercBenchmark` | 复合 32 字节 key（contract, holder）的 ERC-20 风格转账 |

`txBenchmark` 是这套测试中**最简单的事务型负载**：单一命名空间、uniform value 更新。`ercBenchmark` 引入了 16B+16B 复合 key，并按合约做热点偏置，更贴近真实的代币工作负载。

## Future work

- **散布式热冷分布**：将连续前缀的 `pickAccount8020` 替换为基于位图（bitmap）的实现，让热点账户在 key 空间中**均匀散布**。这能避免热点 key 共享 LSM block / B-tree page 带来的局部性陷阱。
- **可重放性**：将生成的 tx 流落盘，以便跨 DB 引擎直接对比同一份工作负载。
- **延迟直方图**：为每个 block 增加 p50 / p99 / p999 延迟统计。
