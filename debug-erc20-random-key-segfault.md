# Debug 记录 — ERC20 随机 key 写入的 SIGSEGV

**Status**: `[ROOT_CAUSE_CONFIRMED — awaiting fix]`
**Session ID**: `erc20-random-key-segfault`
**Date**: 2026-07-17
**Method**: addr2line + 静态源码阅读（无需 runtime instrumentation）

---

## 1. 问题描述

- **现象**: 大规模随机 key 写入负载下，`erc20_transfer_no_load` 二进制在 block 60（version 60，已执行约 240K 事务）时崩溃，进程收到 SIGSEGV（signal 11）。
- **预期**: 在 `num_txn=3.6e9`、`n_acc=1e9`、`tx_per_block=4000` 参数下稳定执行完毕。
- **影响**: 任何大账户规模（≥1e9）、高随机性的 ERC20 负载均会复现。
- **回归窗口**: 提交 history 显示主要业务逻辑（C++ trie）改动集中在 `src/DMMTrie.cpp` / `LSVPS.cpp` / `Letus.cpp`（mtime Jul 15）。

## 2. 关键事实（来自崩溃日志）

```
args: n_acc=1000000000, contract_ratio=0.2421, BASE_DATA_RATIO=5.0 (-> BASE_DATA=5000000000),
      load_batch_size=5000, num_txn=3602930000, tx_per_block=4000, max_blocks=0,
      key_size=32, initial_balance_max=1000000, max_value=100, seed=42

block 10  executed=39995   skippedLowBalance=5
block 20  executed=79991   skippedLowBalance=9
block 30  executed=119989  skippedLowBalance=11
block 40  executed=159987  skippedLowBalance=13
block 50  executed=199986  skippedLowBalance=14
block 60  executed=239984  skippedLowBalance=16   ← 在此之后崩溃
FATAL: received signal 11 (likely segfault/stack overflow/abort)
```

**回溯栈关键帧**（base=0x556e0222a000）：
| 帧偏移 | 说明 |
|---|---|
| `+0xaaba` | 极可能在 `main`/驱动循环 |
| `+0xbef1` / `+0xc055` | 紧随其后，疑似业务主循环内部 |
| `libc +0x42520` | 通常为 `memcpy/memmove/memset` |
| `+0xe836` | 由 memcpy 调回，可能是节点数据拷贝路径 |
| `+0x16664` / `+0x1679c` | 区块事务处理主路径 |
| `+0x18656` | 历史/版本管理路径（候选） |
| `+0xbb35` | 启动入口 |

## 3. 假设表（待验证）

| ID | 假设 | Likelihood | Effort | 期望信号 |
|----|------|-----------|--------|----------|
| A | **Trie 节点分裂/扩容时 memcpy 越界**：在 version≈60 时 trie 触发底层数组扩容，旧指针失效 | High | Low | memcpy 帧附近出现 size 异常；version 临近 60 触发重哈希/扩容 |
| B | **LSVPS 版本链历史节点空指针解引用**：历史指针（`prev_version`）未正确串接 | High | Medium | 历史查询路径崩溃；dump version 0..60 链可见断链 |
| C | **shard/线程间 data race 释放后使用**：多线程下 `free` 后仍访问 | Medium | Medium | AddressSanitizer/TSan 报 use-after-free；线程池 worker 崩溃 |
| D | **std::vector 重新分配时迭代器失效**：`reserve/insert` 时引用旧 buffer | Medium | Low | 触发位置在 `execute_txn_block`/commit 阶段 |
| E | **栈溢出**：递归 trie 操作触发 stack overflow | Low | Low | `ulimit -s` 加大后仍崩；栈深度日志 |

## 4. Instrumentation 设计（最小、并行验证）

> ⚠️ **仍未修改任何业务代码**。本节是设计规格，待用户授权后才会插入。
> 设计原则：每个 instrumentation 点同时能区分多个假设；输出统一上报到 Debug Server（不污染 stdout）。

### 4.1 关键源码静态发现（已指导 instrumentation 定位）

1. **`src/LSVPS.cpp:747` `evictIfNeeded()`** — 当 `cache_.size() >= 4096`（`max_size_`）时只调用 `ClearDeltaPage()` 并从 `cache_` 中 erase，但**调用方可能仍持有 `&page_pool_[it->second]`**（见 4.2）。
2. **`src/LSVPS.cpp:260-310` `LoadPage()`** — 源码本身就在多处写了 `// WARNING: 这个page有可能被flush所释放掉。`，意味着开发已意识到 use-after-free 风险但未根除。
3. **`src/LSVPS.cpp:266` `GetActiveDeltaPage()`** — 返回 `page_pool_` 中的指针；`Store`（同文件 :639）目前**未调用** `evictIfNeeded`（被注释），所以理论上指针稳定，但 `evictIfNeeded` 仍可能通过其它路径被触发。
4. **`src/LSVPS.cpp:309` `new BasePage(*basepage)`** — 拷贝构造触发 memcpy，与崩溃栈 `libc +0x42520` 高度吻合。
5. **`src/DMMTrie.cpp:1149-1321` `CalcRootHash()`** — 每 block commit 都会执行；block 60 时 `put_cache_` 大小约为 `60 * 4000 ≈ 240K`，正是测试日志中崩溃点（executed=239984）。
6. **`src/DMMTrie.cpp:1294` `put_cache_.clear()`** — 注释明确指出"不 clear 会无界增长 → OOM"，但崩溃是 SIGSEGV 而非 OOM，说明崩溃发生在 clear 之前的写流程。

### 4.2 Instrumentation 点位（共 4 个，#region 包裹）

| ID | 假设区分 | 精确插入位置 | 日志字段（NDJSON） |
|---|---|---|---|
| **I1** | **C**（首要） | `src/LSVPS.cpp:662` `ActiveDeltaPageCache::Get()` 进入与返回处 | `event=get_begin`, `pid, cache_size, free_pages_size, pool_pos, addr_inout`（**关键：返回前记录指针地址；下次调用若 cache_.find(pid) 命中但该地址对应的 pool slot 已被 evict，可识别悬空指针**） |
| **I2** | **C, A** | `src/LSVPS.cpp:747` `evictIfNeeded()` 进入处，以及 evict 之后 `page_pool_[it->second].ClearDeltaPage()` 前后 | `event=evict`, `evicted_pid, evicted_slot, cleared_addr, cache_size_after, free_pages_size_after` |
| **I3** | **C, A** | `src/LSVPS.cpp:260` `LoadPage()` 入口与第 290 行 `pageLookup()` 返回后、第 309 行 `new BasePage(*basepage)` 拷贝之前 | `event=loadpage`, `pid, pagekey_version, delta_page_addr, basepage_addr_before_copy, pool_capacity=4096` |
| **I4** | **D, A** | `src/DMMTrie.cpp:1149` `CalcRootHash()` 入口与第 1294 行 `put_cache_.clear()` 前后 | `event=commit`, `version, put_cache_size_before, put_cache_size_after, page_cache_size, delta_page_update_count, pid_count, mem_rss_kb` |

### 4.3 信号 → 假设对照表

| 假设 | 验证信号（instrumentation 会暴露） |
|---|---|
| **A. memcpy 越界** | I3 在第 309 行拷贝前记录的 `basepage_addr_before_copy` 若指向已被 `ClearDeltaPage` 的 slot → 触发 SIGSEGV 的直接原因 |
| **B. 历史链断链** | I3 `LoadPage` 中 `delta_pages` 栈 pop 时地址为 nullptr 或 `current_pagekey.version==0` 后仍尝试 `pageLookup` |
| **C. use-after-free** | I2 记录 `evicted_addr`，I1/I3 命中相同 addr 但 slot 已 evicted → 强烈支持 |
| **D. vector 重分配** | I4 `put_cache_size_before` 跨 block 突然减半、`page_cache_size` 异常 |
| **E. 栈溢出** | I4 `mem_rss_kb` 正常（未爆涨）、I3 递归深度字段（需新增字段） |

### 4.4 不插入 instrumentation 的位置（避免误报）

- 不在 `CalcRootHash` 内部高频 put 路径插入（性能影响 + 噪声）
- 不在 `LetusPut` 等纯转发函数插入（无附加证据价值）
- 不打 `cout`（使用统一上报 `.dbg/erc20-random-key-segfault.env` 中的 Debug Server URL）

### 4.5 实施约束（若用户后续授权）

- 每个 instrumentation 必须包裹在 `#region debug-point I<n>:<desc>` 中，便于 Step 11 清理
- 使用一个工具函数 `dbg_report(event, fields_json)`，inline 实现，不新建工具文件
- 日志文件：`/pcissd/cxy_test/vidb_project/LETUS_prototype/.dbg/trae-debug-log-erc20-random-key-segfault.ndjson`
- 环境文件：`/pcissd/cxy_test/vidb_project/LETUS_prototype/.dbg/erc20-random-key-segfault.env`

---

## 8. 🎯 静态根因确认（addr2line 验证）

使用 `addr2line` 对崩溃栈帧做精确解析后，已确认根因。**该诊断不需要 runtime instrumentation 即可成立**：

```
(anonymous namespace)::PrintStackTrace(int)        ← +0xbef1 / +0xc055  (日志输出)
(anonymous namespace)::OnFatalSignal(int)          ← 信号处理
BasePage::~BasePage()                              ← +0xe836  ← 崩溃发生点
DMMTrie::PutPage(PageKey const&, BasePage*)        ← +0x16664
DMMTrie::GetPage(PageKey const&)                   ← +0x1679c
DMMTrie::CalcRootHash(unsigned long, unsigned long)← +0x18656
main                                               ← +0xaaba / +0xbb35 (启动)
```

### 8.1 触发链（完整还原）

```cpp
// 1) CalcRootHash 每 block 提交时调用；block 60 时 put_cache_ 累积约 240K entries
void DMMTrie::CalcRootHash(uint64_t tid, uint64_t version) {
  ...
  for (const auto &it : updates) {                  // updates 来自 put_cache_
    ...
    BasePage *page = GetPage(old_pagekey);          // ← frame +0x1679c
    if (page == nullptr) {
      // 2) pid 首次出现 → 创建空 basepage（root_ = nullptr ⚠️）
      page = new BasePage(this, nullptr, pid);      // DMMTrie.cpp:1192
      PutPage(pagekey, page);                       // ← frame +0x16664
    }
    ...
  }
}

// 3) PutPage：缓存命中时先 delete 旧 page
void DMMTrie::PutPage(const PageKey &pagekey, BasePage *page) {
  ...
  auto it = lru_cache_.find(pagekey);               // DMMTrie.cpp:1530
  if (it != lru_cache_.end()) {
    delete it->second->second;                      // DMMTrie.cpp:1532 ← frame +0x16664
    ...
  }
  ...
}

// 4) ~BasePage() 不判空直接解引用 root_
BasePage::~BasePage() {                             // DMMTrie.cpp:873 ← frame +0xe836
  for (int i = 0; i < DMM_NODE_FANOUT; i++) {
    if (root_->HasChild(i)) {     // ← root_ 为 nullptr 时 SIGSEGV ⚠️
      delete root_->GetChild(i);
    }
  }
  delete root_;
}
```

### 8.2 为何 block 60 才触发？

1. **首次出现的新 pid** 在 `GetPage(old_pagekey)` 时返回 `nullptr`（pagekey 的 basepage 还没生成），走 [DMMTrie.cpp:1192](file:///pcissd/cxy_test/vidb_project/LETUS_prototype/src/DMMTrie.cpp#L1192) 创建 root_=nullptr 的空 BasePage。
2. 该空 BasePage 被 `PutPage` 放入 `lru_cache_`。
3. **当后续某次 commit 中再次访问同一 pid 时**，`lru_cache_.find(pagekey)` 命中，触发 [DMMTrie.cpp:1532](file:///pcissd/cxy_test/vidb_project/LETUS_prototype/src/DMMTrie.cpp#L1532) 的 `delete it->second->second` —— 析构 root_=nullptr 的旧 page → SIGSEGV。
4. block 60 之前可能未触发是因为 (a) LRU cache size 没满到驱逐这条 entry 或 (b) 相同 pid 的 pagekey 还没第二次 commit；block 60 时 put_cache_ 已含约 240K entries，跨 block 重复 pid 的概率显著上升 → 触发。

> 备注：上述第 3 步的"再次访问同 pid"也可以由 LRU 驱逐触发（[DMMTrie.cpp:1521-1529](file:///pcissd/cxy_test/vidb_project/LETUS_prototype/src/DMMTrie.cpp#L1521-L1529) 同样对驱逐对象做 `delete`）。两条路径都受同一缺陷影响。

### 8.3 假设验证结论

| ID | 假设 | 状态 | 证据 |
|----|------|------|------|
| **A** | memcpy 越界 | ❌ Rejected | 栈顶实际是 `~BasePage`，memcpy 帧来自 string/vector 析构或 `delete[] data_`，不是越界源头 |
| **B** | 历史链断链 | ❌ Rejected | `~BasePage` 在 LRU 驱逐路径触发，**与版本链无关** |
| **C** | use-after-free | ⚠️ Partial | 存在相邻风险（`ActiveDeltaPageCache::evictIfNeeded` 的悬空指针），但**不是本次崩溃源头** |
| **D** | vector 重分配 | ❌ Rejected | 与崩溃栈无关 |
| **E** | 栈溢出 | ❌ Rejected | 栈顶为 `~BasePage`，未显示递归调用 |
| **🆕 F** | **`BasePage` 析构时 `root_` 为 nullptr 解引用** | ✅ **CONFIRMED** | addr2line 精确定位 + 代码静态阅读 |

---

## 9. 修复方案（最小 Patch）

### 9.1 主修复（最小改动，覆盖根因）

**文件**: [src/DMMTrie.cpp:873-883](file:///pcissd/cxy_test/vidb_project/LETUS_prototype/src/DMMTrie.cpp#L873-L883)

```diff
 BasePage::~BasePage() {
-  for (int i = 0; i < DMM_NODE_FANOUT; i++) {
-    if (root_->HasChild(i)) {
-      delete root_->GetChild(i);
-    }
+  if (root_ != nullptr) {                       // ⬅ 根因修复：兜底 nullptr
+    for (int i = 0; i < DMM_NODE_FANOUT; i++) {
+      if (root_->HasChild(i)) {
+        delete root_->GetChild(i);
+      }
+    }
   }
   delete root_;
 }
```

**等价修复备选**：把 `root_` 在创建空 BasePage 时默认指向一个 sentinel（如空 `LeafNode`），避免后续所有路径处理 nullptr。但需要审视所有假设 `root_!=nullptr` 的调用方（`UpdateNode`、`HasChild` 等），工作量较大。**推荐主修复**（最小 diff，行为不变）。

### 9.2 防御性修复（建议同步）

#### 9.2.1 PutPage 覆盖路径加 nullptr 防护

**文件**: [src/DMMTrie.cpp:1519-1539](file:///pcissd/cxy_test/vidb_project/LETUS_prototype/src/DMMTrie.cpp#L1519-L1539)

```diff
 void DMMTrie::PutPage(const PageKey &pagekey, BasePage *page) {
   if (lru_cache_.size() >= max_cache_size_) {
     PageKey last_key = pagekeys_.back().first;
     auto last_iter = lru_cache_.find(last_key);
-    delete last_iter->second->second;
+    if (last_iter != lru_cache_.end()) {                       // ⬅ 防御 1
+      delete last_iter->second->second;
+    }
     ...
   }
   auto it = lru_cache_.find(pagekey);
   if (it != lru_cache_.end()) {
-    delete it->second->second;
+    if (it->second != nullptr) {                              // ⬅ 防御 2
+      delete it->second->second;
+    }
     ...
   }
   ...
 }
```

#### 9.2.2 CalcRootHash 不再创建空 root_ 的 BasePage（或创建后立刻 disable 缓存）

**文件**: [src/DMMTrie.cpp:1188-1194](file:///pcissd/cxy_test/vidb_project/LETUS_prototype/src/DMMTrie.cpp#L1188-L1194)

**方案 A**（推荐）：延迟到 `UpdatePage` 调用后再 `PutPage`，让初始空 page 不进 LRU：

```diff
 BasePage *page = GetPage(old_pagekey);
 if (page == nullptr) {
   page = new BasePage(this, nullptr, pid);
-  PutPage(pagekey, page);
 }
 ... // 后续 UpdatePage 调用会把 root_ 填充
+PutPage(pagekey, page);                                       // ⬅ 移到 UpdatePage 之后
```

**方案 B**：明确给空 BasePage 一个 sentinel root：

```cpp
page = new BasePage(this, new LeafNode(0, "", {}, ""), pid);  // 兜底根节点
PutPage(pagekey, page);
```

#### 9.2.3 [可选] 复用 9.1 修复后，已存在的悬空风险

**文件**: [src/LSVPS.cpp:266-309](file:///pcissd/cxy_test/vidb_project/LETUS_prototype/src/LSVPS.cpp#L266-L309) `LoadPage` 中三处 `// WARNING: 这个page有可能被flush所释放掉。`

虽然**不是本次崩溃源头**，但 addr2line 已确认代码中已标注但未消除的悬空风险。建议作为后续 hardening 项处理（参考 [DEBUG_SCENARIOS.md]）：将 `GetActiveDeltaPage` 改为返回 `std::shared_ptr<DeltaPage>` 或拷贝到调用方栈上。

### 9.3 验证步骤（建议）

1. 应用 9.1 主修复，重新 `bash build.sh` 并运行 `exps/test_erc20_no_load.sh`。
2. **预期**：进程不再在 block 60 崩溃；可继续推进到后续 block（甚至完成全部 36 亿事务）。
3. **回归**：同时运行 `exps/test_simple_payment_no_load.sh`、`exps/test_get_put*.sh`，确认其它路径不受影响。
4. **指标**：观察 `put_cache_` 与 `lru_cache_` 大小、`page_cache_.size()` 在崩溃前后 block 的变化曲线。

---

## 10. 风险与遗留

- **9.2.1 的防御性修复可能掩盖真实内存泄漏**：仅作可选，建议先只做 9.1 + 9.2.2 方案 A，跑回归再决定是否加。
- **`CalcRootHash` 中 `put_cache_` 大小与版本的关系**：本次根因与 put_cache_ 增长无直接因果，但崩溃点恰好在 put_cache_≈240K 时出现，说明版本号与崩溃概率有相关性——根因 9.1 修复后此相关性应消失。
- **如有 ASan 构建可加跑一次**：能进一步确认本根因并暴露 9.2.3 的潜在悬空风险。

---

## 11. 下一步

- 本文档已交付根因与最小 patch。
- 是否应用修复由你/同事决定，本 Skill 不会自动修改业务代码（**用户授权范围：仅诊断，不动业务代码**）。
- 修复后请在 Step 9 报告结果，我将在你的二次确认后清理本文件（[debug-erc20-random-key-segfault.md](file:///pcissd/cxy_test/vidb_project/LETUS_prototype/debug-erc20-random-key-segfault.md)）。

---

## 5. 调试工作流（11 步）

1. ✅ 问题定义 + 创建本文件
2. ✅ 假设表生成（5 个假设）
3. ✅ 设计 instrumentation（最小、并行验证）← **当前进度**
4. ⏸ 插入 instrumentation（**仅日志，不改逻辑**）— 待用户授权
5. ⏸ 启动 Debug Server，清空日志
6. ⏸ 用户复现（运行 `exps/test_erc20_no_load.sh`）
7. ⏸ 基于 `.dbg/trae-debug-log-erc20-random-key-segfault.ndjson` 分析
8. ⏸ 最小修复（保留 instrumentation）
9. ⏸ 用户二次复现，runId 改为 `post-fix`
10. ⏸ 用户确认（A/B/C/D）
11. ⏸ 清理（删除 `.dbg/*` 和本文件）

---

## 6. 用户授权范围（2026-07-17 确认）

- **调试路径**: Skill 标准流程（11 步）
- **运行环境**: 用户自行重跑复现命令并反馈日志
- **业务代码改动授权**: **仅诊断，不动业务代码**
  - 即不实施任何 instrumentation（即使 instrumentation 不改逻辑也属于代码改动）
  - 本文档作为根因诊断与修复方案建议输出，由用户/同事合入

## 7. 下一步动作（待用户确认是否需要我实施 instrumentation）

如你后续希望我按此计划执行，请告诉我。我会：
1. 在 `src/Letus.cpp` / `DMMTrie.cpp` / `LSVPS.cpp` 中插入 4 个 `#region debug-point` 日志点（仅上报，不改逻辑）
2. 启动 Debug Server，写 `.dbg/erc20-random-key-segfault.env`
3. 给出复现 Cheatsheet，由你重跑后回传日志
4. 基于 `.dbg/trae-debug-log-erc20-random-key-segfault.ndjson` 分析并给修复建议

如暂时不需要实施，请直接告诉我「暂不实施」，我会停在这里。