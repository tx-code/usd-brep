# STEP 固定目录重构验收

此工具为 STEP 固定目录编译期迁移提供第一批基线与验收入口。
它比较真实 Catalog 的全部已登记事实，不通过输入样例推测未出现字段。
固定快照是重构等价依据，不是独立 EXPRESS 见证、完整 schema 验证或导入资格。

## 构建与检查

在仓库根目录执行，CMake 需要 C++20 工具链和 Python 3.10+；验收工具仅在
`STEP_SEMANTICS_BUILD_TESTS=ON` 时构建，不成为产品运行依赖：

```powershell
cmake -S source/STEP_SEMANTICS -B _build/catalog-validation -A x64
cmake --build _build/catalog-validation --config Release --parallel 4
ctest --test-dir _build/catalog-validation -C Release --output-on-failure
python -m tools.step_catalog_validation check --snapshot _build/catalog-validation/Release/step_catalog_snapshot.exe --baseline source/STEP_SEMANTICS/tests/catalog/baseline
```

`step_catalog_snapshot` 输出四个 profile 的身份/artifact、原声明顺序、父序、本地字段的全部属性及嵌套维度、
祖先位、展开字段的原声明/本地索引、name→ID、SELECT、枚举、complex signature、derived 槽、
measure 分支、互斥组和 SI 前缀。只有独立名称索引按名称排序；其余数组次序参与比较。
快照通过 STEP_SOURCE 的 JSON writer 完整发布，固定事实从产品目录读取，前缀与互斥组不抄到测试中。

`check` 验证冻结载荷 hash，然后比较完整 JSON 树。它不更新期望值，报告具体路径差异，
不忽略数组次序、bool/整数差异或任何目录字段。`registered_types` 与声明次序也做结构核对。

## 新建基线

只在确实需要新的重构比较起点时显式捕获到**不存在**的目录；检查失败不能通过重新捕获原路径消除。
例如，下面命令会构建真实工具，再执行两次完整快照及两轮各四个新进程的分配测量：

```powershell
python -m tools.step_catalog_validation capture --snapshot _build/catalog-validation/Release/step_catalog_snapshot.exe --allocations _build/catalog-validation/Release/step_catalog_allocations.exe --build-dir _build/catalog-validation --build-info _build/catalog-validation/catalog-build-Release.txt --config Release --output D:/Temp/new-catalog-baseline
```

仅当完整数据一致、计数器正对照成功、直接源文件在构建/测量期间未变，才发布新目录。
提交的 `catalog.json` 保存全部固定事实，`manifest.json` 绑定载荷 SHA256，二者是可重复使用的比较输入。
两轮原始分配计数、源清单、工具二进制指纹、构建命令和工具链信息保存在构建目录中新建的
`catalog-capture-*.json`；工具输出实际路径，不把机器运行记录提交到 Git。
Git HEAD 不代表未提交源内容，本地收据中的实际输入以源指纹为准；这仍不是完整传递依赖审计。

分配 probe 仅在独立、单线程可执行文件中替换 new/new[]，不影响产品库。
普通/数组、aligned、nothrow 共八条路径分别以已知 17 字节请求验证计数器。
首次 Schema、1000 次重复 Schema、短名/长名/未知名 Find 分开计数，窗口外准备和输出。
计数是成功请求和累计请求字节，包含已释放临时量，不是峰值/存活内存；不覆盖直接 malloc。
快照 owner/pointer 地址不跨二进制比较，源 Value 身份继续由原语义合同在进程内检查。

## 正反编译驱动

```powershell
python -m tools.step_catalog_validation.compile_contracts --build-dir D:/Temp/catalog-compile-controls --config Release --generator "Visual Studio 18 2026" --architecture x64
```

驱动按[case manifest](compile/cases.json)编译真实 object target，保存每例日志和 results.json；
独立 owner/consumer 的正例另链接并运行可执行文件，确认静态视图可以跨翻译单元使用。
负例必须有同环境通过的正对照、非零退出码，以及实际 compiler error 行上的指定标记。
命令行/文件名/警告中的标记、缺头文件、错误期望和失效正对照不能算通过。
每次重新配置/构建，不复用过去的退出码；独立 object target 避免把缺链接依赖算作编译拒绝。

编译矩阵使用生产 consteval 构建器，覆盖四个真实 profile、菱形继承/字段原序、derived override、
嵌套 aggregate、membership 字边界及 256 类型正例；负例逐项注入重复登记、未知引用、继承环、257 类型、
非法 complex/derived/聚合范围/前缀/覆盖目标。人为 static_assert 控制仅验证驱动机制，不算产品校验保障。
生产头文件的两处公开 span 表示也有编译检查；没有维护测试专用 validator。

分配 probe 对首次/重复目录取得和短名/长名/未知名查询强制零分配，仍要求八条已知分配正对照成功。
正常库中的四份存储及视图均为 constexpr，不能靠把初始化移动到 main 前绕过该运行期检查。

CTest 注册全量基线比较、工具破坏控制、编译驱动、四个分配 probe，以及既有源/语义合同。
工具单测覆盖顺序、未被样例使用的字段、嵌套维度、祖先高位、字段来源、每类登记表、篡改 hash、
缺失载荷、非法 JSON 和错误编译标记。需要测试新的候选基线时，可临时设置 `STEP_CATALOG_TEST_BASELINE`；
正常 CTest 使用仓库内冻结目录。

完整源语义/CLI 诊断、预算和独立 N2635 witness 仍由
[语义验证工具](../step_semantic_validation/README.md)负责。本工具不替代它们，
也不因固定目录等价就授予数值、几何、native 导入或跨平台资格。

## 构建与导入成本比较

固定迁移前后两个源码版本，排除不属于该迁移的提交；保持工具链、架构、C++20、配置、CRT、
宏、优化/LTO 和并行度相同。比较 native 导入时，两侧使用指纹相同且 ABI 匹配的内核库、
相同输入字节与显式选项。源码、二进制和输入指纹、完整命令及原始结果保存在本地，不能仅凭当前 HEAD
声称某个运行属于某个版本。构建或测量失败必须保留为失败，不能进入成功耗时汇总。

Windows 可用以下包装器测量一次构建命令；输出目录中每次新建独立 JSON，不覆盖历史记录，
被测命令的标准输入/输出/错误和退出码原样传递，超时返回 124：

```powershell
python -m tools.step_catalog_validation.process_measure --output-directory D:/Temp/catalog-costs --timeout 600 -- cmake --build _build/catalog-validation --config Release --target STEP_SEMANTICS --parallel 1
```

需要每个翻译单元的测量时，在 Ninja 配置中把包装器设为编译器 launcher；使用绝对路径，
不要把包含工作目录假设的 `python -m` 命令直接交给另一目录中的构建：

```powershell
$measure = (Resolve-Path tools/step_catalog_validation/process_measure.py).Path
$python = (Get-Command python).Source
cmake -S source/STEP_SEMANTICS -B _build/catalog-costs -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER_LAUNCHER=$python;$measure;--output-directory;D:/Temp/catalog-compiler-costs;--"
cmake --build _build/catalog-costs --target STEP_SEMANTICS --parallel 1
```

从已配置匹配 MSVC 的开发者终端执行。本地化 `/showIncludes` 前缀的编码不匹配可能让 Ninja
漏记头文件依赖。使用 `ninja -C <build> -t deps <catalog-object>` 核对真实头文件依赖，
再核对私有头和公开头修改后的重编译集合。已安装英文编译器资源时，可从首次配置起统一
`VSLANG=1033`；只有中文资源时，这个变量不能替代实际前缀核对。不能把 `no work to do`
或缺失依赖导致的零编译计作增量性能收益。

包装器在恢复进程执行前把它加入 Windows Job Object，内核保留整个命令树的峰值，包含短命编译器子进程；
没有以轮询内存估计峰值。`peak_process_commit_bytes` 是树中单个进程的最大提交内存，
`peak_job_commit_bytes` 是整棵树同时提交内存的峰值，两者都不是驻留 RAM、累计请求字节或
导入净内存。`tree_user_seconds` / `tree_kernel_seconds` 累计树中进程 CPU 时间；
`wall_seconds` 从恢复挂起进程前计至整棵树结束，包含执行和子进程结束，不包括最初的
CreateProcess 调用、包装器启动及结果写出。包装器本身在该 Job 外。
无效启动/测量错误不给出成功收据；超时会终止整棵树。其他平台不输出伪造的零内存结果。
工具单测用已知提交量、短命子进程、失败退出、超时及根进程先退出的子进程验证这些边界。

先完成预热构建，再交替测量两侧至少三轮。至少区分：整个语义库重新编译、全部目录源文件重新编译、
私有登记变化、公开头变化、普通消费者变化。固定使用一个构建目标，记录实际重编译文件集合；
基于时间戳的增量触发必须确实被构建系统识别。旧版目录分散在六个翻译单元，不能只拿旧
`StepCatalog.cpp` 与新 owner 比较。单元耗时总和、整个目标 wall time、峰值和并行构建成本分别报告；
串行结果不外推为并行结果。记录样本范围，避免把文件缓存、测量包装器或其他构建竞争当成产品收益。

产物体积同时报告全部目录 object 合计、完整语义静态库和实际链接的消费者；object/lib 大小
不等同于最终程序体积。完整导入复用[导入基准探针](../step_import_benchmark/README.md)，
关闭 native-boundary timing，分别比较新进程首次导入和进程内预热后的导入。说明文件缓存条件、
ImportFile 操作时间和进程 wall time 的边界；核对接受状态、诊断、模型事实、原生调用数与选项相同。
每个样件分别统计，再说明合并方法。合成样件的时间差不能直接外推为真实大模型速度收益，
也不能用零目录分配代替完整导入提速证据。每次运行的观测值与分析收据仅保留在 `.local/` 或外部构建目录。
