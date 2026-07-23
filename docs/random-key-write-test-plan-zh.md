# LETUS 大量随机 Key 写入测试计划

## 目标与通过标准

目标是稳定复现并防止随机 key 写入在 `Commit/CalcRootHash`、页面替换和
LRU 淘汰路径上的崩溃，同时验证提交后的值没有静默损坏。

每个用例必须满足：

1. 进程正常退出，无 SIGSEGV、abort、ASan/UBSan 报告。
2. 每个版本均成功提交。
3. 固定 seed 下抽样读回值与最后一次写入一致。
4. 压力用例至少越过已知故障窗口：80 个版本、每版本 4000 次写入
   （总计 32 万次，历史故障约在 24 万次后出现）。

## 测试矩阵

| 层级 | 场景 | 参数建议 | 目的 |
|---|---|---|---|
| 冒烟 | 少量全随机 key | `--versions 3 --writes-per-version 100` | CI 快速检查接口和读回 |
| 回归 | 已知故障窗口 | 默认参数（80 × 4000，seed 42） | 覆盖空 BasePage 被替换/析构路径 |
| 重复写 | 小热集随机更新 | `--hot-key-count 10000` | 覆盖同 key、同 pid 跨版本反复更新 |
| 边界 | key 长度 | 分别使用 `--key-bytes 1/16/32/64` | 覆盖 trie 深度和页边界 |
| 边界 | value 长度 | 分别使用 `--value-bytes 1/64/1024` | 排除 value log 大小相关问题 |
| 稳定性 | 多 seed | seed `1, 42, 20260723, 1844674407370955161` | 避免只对单一键分布有效 |
| 内存安全 | ASan + UBSan | 回归场景，必要时先降到 20 × 4000 | 检测空指针、越界和 UAF |
| 长稳 | 1000 × 4000 | 独占机器运行 | 检查缓存增长、长期提交稳定性 |

## 执行方式

构建并运行默认回归：

```bash
./build.sh --build-type release --cxx g++
./build_release_letus/bin/random_key_write_test
```

运行快速 CTest：

```bash
ctest --test-dir build_release_letus -R random_key_write_smoke \
  --output-on-failure
```

运行热 key 和保留现场数据：

```bash
./build_release_letus/bin/random_key_write_test \
  --seed 42 --versions 80 --writes-per-version 4000 \
  --hot-key-count 10000 --keep-data
```

内存安全构建：

```bash
./build.sh --build-type debug --cxx clang++ \
  --add-flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
./build_debug_letus/bin/random_key_write_test
```

失败报告至少保留命令行、seed、最后成功提交的版本、退出码、栈回溯以及
测试输出中的 data/index 路径。修复验收时先跑固定 seed 回归，再跑多 seed
和 sanitizer，最后执行现有 `simple_payment_no_load`、`erc20_transfer_no_load`
及 `get_put*` 回归。
