# ercBenchmark

面向 [vidb](../vidbsvc/vidb.go) 的 ERC-20 风格转账 benchmark。它建模了一个真实的代币工作流：固定的合约账户集合为大量 holder 账户持有余额，转账操作在同一个合约下把价值在两个 holder 之间搬动。

## 源码

[cmd/ercBenchmark.go](../cmd/ercBenchmark.go)（Cobra 命令 `ercBenchmark`）。

## 设计目标

- 用 **复合 32 字节 key**（16 字节合约地址 + 16 字节 holder 地址，加 `-account` 前缀后总长 39 字节）压测 vidb —— 比单一命名空间的 key 更贴近真实代币 DB。
- 支持 **大规模账户空间**（最多约 1e9 个合约 + holder），**不在内存中缓存地址表**。地址由 `(seed, idx)` 通过 big-endian 编码按需派生，因此内存占用与 `num-accounts` **解耦**，为 O(1)。
- 通过 `--seed` 保证可复现。
- 将 load 阶段（为每个 (contract, holder) 对写入初始余额）与事务阶段（读-改-写转账）**分开计时**。

## 数据模型

| 维度 | 取值 |
|------|------|
| 账户数量 | `--num-accounts`（默认 10000） |
| 合约占比 | `--contract-ratio`（默认 0.1）—— 作为合约的账户比例 |
| 地址编码 | 16 字节：`[0,0,0,0,0,0,0,0, idx_be8]` —— 通过 `deriveAddress(idx)` 派生 |
| 地址派生 | `big-endian uint64(idx)` 零填充到 16 字节（无哈希、完全确定性） |
| Key 布局 | 39 字节：`"-account"`（7）+ contract（16）+ holder（16） |
| Key 示例 | `-account` + 16 字节 0 + 16 字节 contract + 16 字节 holder |
| Value 编码 | `uint64` little-endian，8 字节 |
| 初始余额 | 每个 (contract, holder) 对在 `[0, --initial-balance-max]` 内均匀采样 |
| 每个合约的 holder 数（load） | `ercHoldersPer = floor(--base-data / numContracts)`。若 `--base-data=0` 则退化为 `--num-accounts`，所以默认 `contract-ratio=0.1` 下每个合约播种 10 个 holder |

### 地址空间划分

整数索引空间 `[0, numAccounts)` 被分成两段**连续**区间（按 `--contract-ratio` 比例）：

```
[0, numContracts)                   [numContracts, numAccounts)
   合约前缀                            holder 后缀
```

索引 `i` 的真实 16 字节地址就是 `deriveAddress(i)`。因为编码直接是 `idx`，同一段内的账户地址在 key 空间上会聚集 —— 这是有意的，匹配真实链上代币合约地址通常集中在一段的特点。

> 这里**不**沿用 txBenchmark 的 80/20 热冷分布；每条 tx 的合约与 holder 都是均匀抽样。合约集合使用连续前缀是出于**内存效率**考虑，**不是**热区偏置。

## 处理流程

```
+-------------------+      +------------------------+
| 1) 流式加载        |      | 2) 流式运行             |
|  - numContracts 个 | ---> |  - numTransactions 笔 |
|  - 每个合约:       |      |  - 按 txPerBlock 聚合  |
|    选 ercHoldersPer|     |  - 每 tx 选 1 合约 +   |
|    个 holder, 写入 |      |    2 个不同 holder      |
|    余额            |      |                        |
|  - 按 batch-size  |      |  - 2 次读 + 0..2 次写 |
|    切事务          |      |  - 每 batch-size 一次  |
|                   |      |    提交                 |
+-------------------+      +------------------------+
                                       |
                                       v
                              +------------------------+
                              | 3) 结果 CSV            |
                              | TotalTx, ExecutedTx,   |
                              | Skipped{NotFound,Low-  |
                              | Balance}, BlockCount,  |
                              | Elapsed(s), Throughput |
                              +------------------------+
```

### 1. 流式加载阶段（`streamLoadErcBalances`）

- 对每个合约索引 `ci ∈ [0, numContracts)`：
  1. `contract = deriveAddress(ci)`（16 字节，按需派生）。
  2. 循环 `ercHoldersPer` 次：每次从 holder 区间 `[numContracts, numContracts+numHolders)` 均匀抽一个 holder 索引。
  3. 对每个抽到的 holder：`holder = deriveAddress(hi)`，构造 key `makeErcKey(contract, holder)`，写入随机初始余额。
- 每当一个 batch（≤ `batch-size`）填满就通过 `vidbsvc.UpdateBatchKeyValue(ins, batch, seq, commitEvery)` 提交；返回的耗时累加到 `stats.loadElapsed`。
- 最后一批不足 `batch-size` 的尾部数据会被一并 flush。
- **内存上限**：1 个批次 + 少量 scratch 地址字节。

### 2. 流式运行阶段（`streamRunErcBlocks`）

- 通过 `generateErcBlock` 一次只构造 `--tx-per-block` 条交易组成的单个 block，然后立即调用 `executeErcBlock`；永不把全部 tx 列表物化到内存。
- 每个 block：
  1. 对每条 tx 随机抽：
     - 1 个合约索引 `ci ∈ [0, numContracts)`
     - 2 个不同 holder 索引 `hiFrom, hiTo ∈ [numContracts, numContracts+numHolders)`
  2. 按需派生 `contract`、`fromAddr`、`toAddr`。
  3. 读 `(contract, fromAddr)` 和 `(contract, toAddr)` 的当前余额。
  4. 套用 skip 规则；若双方都存在且 `from >= value`，计算 `from - value` / `to + value`，并入 2 个 `Put`。
  5. 将累积的 `Put` 按 `batch-size` 拆成多个事务；每个事务通过 `vidbsvc.UpdateBatchKeyValue(ins, kvs, seq, commitEvery)` 提交。
- `stats.txElapsed` 累加 `UpdateBatchKeyValue` 返回的时间。
- block 切片通过 `block[:0]` 复用，避免反复分配。

#### Skip 规则

满足以下任一条件时跳过该笔交易（会被计入统计，不会被静默丢弃）：

- `(contract, fromAddr)` 在 DB 中不存在（计入 `skippedNotFound`）；或
- `(contract, toAddr)` 在 DB 中不存在（计入 `skippedNotFound`）；或
- `from_balance < value`（计入 `skippedLowBalance`）。

默认 `--base-data=0`（Go 端退化为 `--num-accounts`）配合 `--contract-ratio=0.1` 时，每个合约播种 10 个 holder，因此运行期大多数 `from` key 都能在第一轮就命中。若想**有意压测 skip 处理路径**，可以减小 `--base-data`（例如设为 `--num-accounts / 10`，让 ercHoldersPer=1）。

### 3. 结果 CSV

默认输出到 `<dataPath>/ercBenchmark.csv`；通过 `--result-path` 可指定其他路径（不存在会自动创建父目录）。列定义：

```
TotalTx, ExecutedTx, SkippedNotFound, SkippedLowBalance, BlockCount, Elapsed(s), Throughput(tx/sec)
```

## 账户选择

| 维度 | 分布 | 来源 |
|------|------|------|
| 每 tx 选合约 | 在 `[0, numContracts)` 上均匀 | `rng.Intn(numContracts)` |
| 每 tx 选 from / to holder | 在 `[numContracts, numContracts+numHolders)` 上均匀 | `holderBase + rng.Intn(numHolders)` |
| from ≠ to | 由拒绝采样保证 | 重试循环 |
| 转账金额 | 在 `[1, --max-value]` 上均匀 | `rng.Int63n(maxValue) + 1` |

合约和 holder 区间是地址索引空间的**连续前缀**，由同一个 `deriveAddress` 派生。同一段内的两个账户地址在 key 空间上聚集——这是为了内存效率，也是真实链上代币合约地址的典型分布。

## 参数（Flags）

| Flag | 默认值 | 说明 |
|------|--------|------|
| `--num-accounts` | 10000 | 账户总数（合约 + holder） |
| `--num-transactions` | 100000 | 交易总数 |
| `--contract-ratio` | 0.1 | 合约账户比例（0 < ratio < 1） |
| `--tx-per-block` | 1000 | 每个 block 的交易数 |
| `--initial-balance-max` | 1000000 | (contract, holder) 对的初始余额上限（uint64） |
| `--max-value` | 100 | 单笔转账金额上限（uint64） |
| `--batch-size` | 5000 | 单个事务最多包含的 put 数 |
| `--commit-every` | 100 | 每 N 个事务调用一次 `db.Commit`（0 关闭） |
| `--max-blocks` | 0 | 跑满多少个 block 后停止（0 表示不限制） |
| `--seed` | 42 | RNG 种子，用于复现 |
| `--base-data` | 0 | load 阶段播种的总基础数据条数；为 0 时退化为 `--num-accounts`。每个合约播种的 holder 数为 `floor(--base-data / numContracts)` |
| `--result-path` | `<dataPath>/ercBenchmark.csv` | 结果 CSV 输出路径（不存在会自动创建） |
| `--dataPath` | `testdata/paper/ercBenchmark` | vidb 数据目录 |
| `--cacheCost` | 1 GiB | 全局 cache 内存预算 |
| `--VlogSize` | 1 | Vlog 文件大小（GB） |

## 常用命令

冒烟测试（1万账户，10万笔交易）：

```bash
./letus-vidb ercBenchmark \
  --num-accounts 10000 \
  --num-transactions 100000 \
  --dataPath testdata/paper/ercBenchmark
```

规模测试（100 万账户、1000 万笔交易、5% 合约）：

```bash
./letus-vidb ercBenchmark \
  --num-accounts 1000000 \
  --num-transactions 10000000 \
  --contract-ratio 0.05 \
  --dataPath testdata/paper/ercScaling_1e6
```

超大规模（1亿账户、100万笔交易）：

```bash
./letus-vidb ercBenchmark \
  --num-accounts 100000000 \
  --num-transactions 1000000 \
  --dataPath testdata/paper/ercScaling_1e8
```

## 扩展性特征

- **加载阶段内存**：O(`batch-size`)。
- **运行阶段内存**：O(`tx-per-block`)（block 本身）+ O(`batch-size`)（put 队列）。
- **地址表总内存**：O(1)。所有 1e9 个地址都通过 `deriveAddress(idx)` 按需派生；每次调用只分配 16 字节 scratch，函数返回后立即释放。
- **在 1e9 量级下仍能稳定运行**，前提是宿主机磁盘足够（默认 `VlogSize=1 GB` 对 1e9 条目太小，需相应调大，比如 `32 GB`）。

## 与其他 benchmark 的关系

| Benchmark | 对比维度 |
|-----------|----------|
| `txBenchmark` | 单一命名空间 key + 80/20 热冷分布 |
| `microBenchmark`（cache / point / range） | 隔离的微操作 |
| `multiTree` | 多棵 LSM-tree 共享同一 cache 时的吞吐 |

`ercBenchmark` 是这套测试中**最贴近真实代币 DB** 的负载：32 字节复合 payload（contract, holder）、十亿级账户无须 16 GB 常驻地址表、key 空间天然聚集。

## Future work

- **按合约做热冷偏置**：让部分合约（top 20%）承载大部分流量，模拟真实 DeFi 活动。
- **可重放性**：将生成的 tx 流落盘，以便跨 DB 引擎直接对比同一份工作负载。
- **延迟直方图**：为每个 block 增加 p50 / p99 / p999 延迟统计。
