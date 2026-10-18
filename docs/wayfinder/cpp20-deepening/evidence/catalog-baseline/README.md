# 固定目录分配基线

2026-10-18，Windows x64 Release / MSVC 19.50.35728.0 / Visual Studio 18 2026 / C++20 / CMake 4.3.2。
这是[迁移方案](../../catalog-migration-plan.md)的当前实现测量，不是产品迁移或导入性能验收。

| Profile | 类型 | 本地字段 | 展开字段绑定 | 首次 Schema 分配请求 | 累计请求字节 |
|---|---:|---:|---:|---:|---:|
| Ap203e2Planar | 72 | 80 | 172 | 1,185 | 99,821 |
| Ap203e2GeneralSource | 132 | 186 | 346 | 2,222 | 204,813 |
| Ap214e3Source | 114 | 158 | 307 | 1,963 | 185,140 |
| Ap214MbxSource | 120 | 169 | 327 | 2,069 | 193,434 |

四个 profile 的 1000 次重复 Schema 均为零分配，1000 次短名 LINE 查询也为零分配。
1000 次最长有效名称查询均有 1000 次分配、累计请求 64,000 字节；名称长度分别为 50、55、55、55。
两轮各以四个新进程执行，结果一致。SI_PREFIX 单独登记，不包含在输出的 enumerations 数量内。
基线在 `713bf00` 上刷新，包含调查期间并行合入的 AP214 曲线样式；不能用新增登记导致的差额评价迁移。

## 复现

从仓库根目录执行，构建输出保持在另一个目录：

```powershell
cmake -S docs/wayfinder/cpp20-deepening/evidence/catalog-baseline -B D:/Temp/cpp20-catalog-baseline-saved-20261018 -A x64
cmake --build D:/Temp/cpp20-catalog-baseline-saved-20261018 --config Release --parallel 4
0..3 | ForEach-Object { & D:/Temp/cpp20-catalog-baseline-saved-20261018/Release/catalog_baseline.exe $_ }
```

本 harness 使用当前目录的内部表示，后续产品迁移时只调整读取表示的适配部分，保留被测窗口、输入和计数定义。
它直接编译目录与五个扩展，另编译 StepSource.cpp 以满足 AttributeAt 的源访问链接；不构建几何内核。
全局 new/new[] 替换仅用于此独立可执行文件，不加入产品库。

## 证据与限制

- [results.ndjson](results.ndjson)与[results-repeat.ndjson](results-repeat.ndjson)保存原始两轮输出；
  [manifest](baseline-manifest.json)记录工具链、观测时 HEAD 和直接产品源 SHA256。
- 计数窗口仅含首次 Schema、重复 Schema、短名/长名 Find；准备、结构统计和输出均在窗口外。
  每次查询校验结果，重复 Schema 校验身份；不同 TU 且无 LTCG，另以 volatile 结果防止调用被消除。
- 计数为成功的全局 new/new[] 请求及其请求字节，覆盖 aligned 形式，包含随后释放的临时对象。
  不包含直接 malloc 或窗口外分配；不是存活内存、峰值内存或最终目录大小。
- 替换分配器为单线程测量，不用于产品并发调用；实际读取对象及字段顺序没有被 harness 改写。
- manifest 的直接源指纹在清洁构建前后和两轮执行后均一致；另保存 harness、CMake 和两轮输出的 SHA256。
  这不是原子仓库快照或完整传递依赖清单。保存的 harness 经过格式化并已在独立目录重新清洁构建和复测。
- 未测耗时、编译器内存、体积或完整导入。目标是移除具体分配及固定推导，不能据此声明导入提速。
