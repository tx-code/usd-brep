# STEP 模型库画像与测量工具

原生 import 探针可显式传 `--integration-relative-accuracy 1e-6`，配合通用质量配置。
仅接受有限的 `[1e-8,1e-4]`，其他引擎／模式及 P0 在输入预算跳过前拒绝该选项。
实际请求进入 trial 的 `settings.integration_relative_accuracy`，执行观察进入
`numerical_integration`；失败时不把未运行的积分记为完成。默认输出保持原样。
该参数是无量纲数值停止控制，不是几何容差或连续误差界；性能组须同时匹配这项配置。

原生探针可另选 --carrier-interpretation Level1AsCorrectedEncodingWithLowercaseHex，
与显式通用质量及 PreserveNonGeometric 配合。它只作用于已解释声明 1 的 DATA 转义，
完成结果保持单列解释资格；旧策略及其他声明不变，其他引擎／模式拒绝该原生参数。
读取、字符、值、诊断、原生限额及报告／提交条件继续适用。

原生探针可另传 `--schema-interpretation Ap214NameOnlyAs2010`，与显式通用质量及
`PreserveNonGeometric` 配合，仅为裸 `AUTOMOTIVE_DESIGN` 选择固定源解释。源版次未验证，
完成模型仍为 `AcceptedUnderInterpretation`。旧八段标识策略与它的适用身份互不替代；
其他引擎／模式、错误配置在输入预算跳过前拒绝。两类解释结果按实际策略单列。

原生 import 探针可显式传 `--carrier-interpretation Level1AsCorrectedEncoding`，并同时
选择通用质量与 `--source-policy PreserveNonGeometric`。它与 schema 解释独立，可显式组合；
实际采用载体假设时为 `AcceptedUnderInterpretation`，记录请求、应用及
`source_carrier_verified=false`。未应用的请求不改变普通成功资格。其他引擎／模式即使
值为 `DeclaredOnly` 或输入超限，也拒绝这个原生专用选项。Python 批测命令未增加该参数；
可直接调用探针或 C++ API。开发解释结果必须单列，不并入普通成功性能组。

原生探针可显式传 `--restrict-unused-support-domains 1`，并选择通用质量；其他引擎／模式
即使值为 0 或将因输入预算跳过，也拒绝该原生专用选项。
`--quality Q-AP214-work-tolerance-study-mm-01` 是另一个单列研究配置：几何/UV 预算仍
0.01 mm，原生初始工作容差 0.01 mm、上限 0.02 mm、增长预算 0.01 mm。它不自动开启
规范化，不修改旧配置，也不进入不同配置的普通成功性能组。实际选项及原生工作预算
写入 trial；限制域的能力与数值／区域条件见 [STEP_IMPORT](../../source/STEP_IMPORT/README.md)。

独立原生探针可显式传 `--schema-interpretation UnregisteredAp214As2010`，并同时选择
通用质量和 `PreserveNonGeometric`。该选项只适用于 `self-native/import`，其他引擎
在输入预算跳过前也明确拒绝。实际采用假设时返回 `AcceptedUnderInterpretation`，
记录请求、实际应用和 `source_binding_verified=false`；模型可用性与普通验收分开。
这类开发观察不得并入普通 `Accepted` 性能或验收组，也不能用独立读取成功认证有争议的 OID。
Python 批测命令目前未增加该参数；可直接调用独立探针或 C++ API。

原生 import 探针可显式传 `--header-name-policy PreserveOpaqueBytes`，与通用质量和
`--source-policy PreserveNonGeometric` 同时使用，保留原始 HEADER 文件名字节。
错误组合在执行前拒绝；不会把此选项传给其他读取器。

`--native-boundary-timing 1` 仅供原生 import 剖析，记录现有 progress 回调之间的区间，
不增加导入检查点。区间包含对应收费操作及其后的只读工作，不能当作纯内核调用耗时。
数据在 `native_boundary_profile`，每个收费序号从 1 起；`complete` 表示到达 NativeReady。
该选项含额外计时和存储开销，正式耗时／内存对比应关闭；默认不生成这项观察。

独立 C++20 probes 和 Python 批测入口，复用公共 `InspectFile` / `ImportFile`，
并通过本机 OCCT `STEPControl_Reader` 读取同一原始文件。产品代码和读取规则不因测量而改变。
模型库、绝对路径、日志、原始结果和 HTML 报告全部保留本地；仓库只存可复用工具和公共合同测试。

## 构建

开发迭代成本与导入运行性能同时记录。优先构建受影响的库、实际调用程序和对应合同测试，
按[测试选择规则](../../.agents/skills/usd-brep-test-selection/SKILL.md)扩大验证；公共接口或 ABI
变化时重建受影响消费者。固定工具链、配置、并行度和内核 SDK，区分私有实现、登记表、
私有头和公共头修改，记录实际重编译文件、编译／链接耗时及得到可测试程序的总耗时。
复用[编译成本测量与依赖核对](../step_catalog_validation/README.md#构建与导入成本比较)，
原始日志和收据仅留本地。无改动构建只作环境检查，不能计作修改后的增量提速；
重复验证需要新的改动、失败或未解决风险作为依据。

原生 import 探针可显式选择 `--quality Q-AP214-fine-work-tolerance-study-mm-01`。
该配置保持普通 development 的几何、UV、查询及积分预算，固定原生初始／最大工作容差
为 0.0001 mm、增长为零，不自动重试或修复。trial 保留实际质量身份与工作预算。
性能比较须按该配置单列，并匹配已有数值积分控制；不得与普通配置或其他研究配置混算，
也不能从耗时或导入状态推导连续误差与生产资格。完整契约见 STEP_IMPORT README。

当前本机入口为 Windows / MSVC，使用匹配的 SMLib SDK 和现有 OCCT build tree。
OCCT 工具只链接外部对照进程；不通过 `.brep` 或 USD 中转。

```powershell
cmake -S tools/step_import_benchmark -B <build> `
  -DSTEP_IMPORT_NATIVE_DIR=<smlib-sdk> -DSTEP_BENCH_OCCT_BUILD=<occt-build>
cmake --build <build> --config Release --target step_source_probe step_import_probe step_occt_probe
$env:PATH = '<occt-build>/win64/vc14/bin;' + $env:PATH
```

探针接受 `--mode`、`--input`、`--output`、`--repeat`、`--warmup` 和 `--max-input-bytes`。
结果写入指定 JSON，stdout/stderr 独立保存 OCCT 消息。完成一次测量返回0，
具体 Inspected / Accepted / Unsupported / Rejected / ResourceLimit / PartialTransfer 等在结果中区分；
进程异常或无法发布结果返回非0。超出工具输入预算为 `NotRunInputBudget`，不是源文件非法或引擎失败。

`self-native/import` 还接受显式 `--quality Q-AP214-development-mm-01`（1 μm）或
`Q-AP214-tolerance-study-mm-01`（10 μm），以测量已实现的通用开发路径。
互操作必须另传 `--source-policy PreserveNonGeometric`，质量配置本身不会放宽源策略。
不传参数仍为严格 P0；错误配置以及其他模式收到这些原生参数时直接拒绝。
每次 trial 记录实际质量、策略、预算和计时范围。通用路径的面积／体积请求相对精度
为 1e-4，包含在公共导入操作内；导入后的 `Info()` 查询另记时间。

## 画像与批测

```powershell
python -m tools.step_import_benchmark census --root <model-root> --output <local>/census.json
python -m tools.step_import_benchmark run --census <local>/census.json --cohorts <local>/cohorts.json `
  --source-probe <build>/Release/step_source_probe.exe --native-probe <build>/Release/step_import_probe.exe `
  --occt-probe <build>/Release/step_occt_probe.exe --occt-bin <occt-build>/win64/vc14/bin `
  --scope paired --repeat 3 --warmup 1 --output <local>/paired
python -m tools.step_import_benchmark report --census <local>/census.json --cohorts <local>/cohorts.json `
  --run <local>/paired --output <local>/report.html
```

画像排除字符串和注释中的伪实体，统计简单记录和复合记录的外部组件，
HEADER schema 从受限前缀提取；保存输入 SHA256、大小、mtime 和目录分层。
这些是词法画像，不是解析、schema 符合性或几何验收。
同内容文件由 SHA256 去重选择60开发、20保留盲测、8大型；盲测内容及其重复路径都不进入批测。
`source-scan` 读取除盲测外的每个路径，`paired` 运行开发集的四种模式，`large` 单独记录大型样本。
不足以填满分组时保留实际数量，不补重复文件。

每个文件/模式启动新进程，warmup 和各次实测分别保留；文件先校验 SHA256，因此输入缓存被预热。
重复先取每文件中位数，汇总再取文件间中位数/P95；失败和 Unsupported 不进入成功耗时统计。
批次顺序执行，`--timeout` 和 `--maximum-private-bytes` 显式指定。
内存监管是20ms采样后的结束操作，可超调，不是操作系统分配硬上限。
进程失败、超时、内存/日志停止、输出不完整、输入变化分别保存；部分数据不计为成功。
新测量的输入变化或消失记为该文件的 `InputIdentityFailure`，其余文件仍可继续；
续跑已完成记录时若输入身份不符，则拒绝续跑原批次。
批次中断后可用 `--resume`：只有输入、参数、二进制和运行库身份匹配才保留完成结果；
未完成尝试留在自己的目录，新尝试另存。

## 测量含义

| 模式 | 计时范围 | 模型 |
|---|---|---|
| self-source / source | `InspectFile`：读取、捕获/SHA、词法、记录与引用索引；不输出完整源图 JSON | 无 |
| self-native / import | 公共 `ImportFile`：严格检查、构建、后验、必需报告与发布；另记公开阶段时间里程碑 | 仅 Accepted |
| OCCT / read | Reader 初始化＋`ReadFile`；读取诊断提取单独计时 | 无 |
| OCCT / transfer | Reader 初始化＋`ReadFile`＋`NbRootsForTransfer/TransferRoots/OneShape` | 另记根数、部分转换和检查失败 |

OCCT 使用默认 FixShape 路径，系统长度单位设为毫米，实际精度、surface-curve、assembly 等设置写入结果。
读取/转换失败和警告数另存；完整转根也不代替源完整性或独立几何验收。
`BRepCheck_Analyzer` 与面积/体积/包围盒量测单独计时，不计入 OCCT `operation_ms`。
体积对照使用 `BRepGProp::VolumeProperties` 的显式自适应 `1e-4` 重载，并记录请求与
原生误差估计。Boolean 重载的固定积分可在曲面模型上产生明显差异，不能把该差异
直接归因于导入几何；自适应停止条件及估计同样不是全局连续误差证书。
混合结果的体积只汇总唯一且保留实例位置的闭合 SOLID，排除独立面与开放壳；
没有 SOLID 或整体自检失败时体积为空，不对混合根直接做体积积分。
两边的工作与修复/精度契约不同，工具始终不给加速比；只并列相同文件的阶段结果和实测。
公开合成盒体和带孔面用于确认量测器的拓扑、面积、体积与孔行为，不能替代真实库覆盖。

`operation_ms` 排除探针的小 JSON 输出和清理；`process_wall_ms` 还包含进程/DLL启动、检查量测、
清理及输出。自研内部只记录公共 progress 的时间里程碑，不将其冒充未暴露的细分构建/后验耗时。
内存是进程绝对提交量和已观测的生命周期峰值，含运行库、warmup、缓存及多次测量，不能当单模型净分配。
当前自研源层64MiB安全上限保持，大型组 OCCT 上限512MiB；这些不同预算的结果单列。

本地 OCCT 可为开发版本，必须以结果中的完整版本号、实际加载 DLL 与其 SHA256 为准。
`build_id` 是 configure 时的仓库提交；源码与二进制的构建绑定仍需本机构建日志及源身份记录。

原生 import 探针可显式传入 `--data-string-policy PreserveOpaqueBytes`，并须同时
选择通用质量及 `--source-policy PreserveNonGeometric`；该选项单独记录在 trial 中。
HEADER 原字节策略仍由独立的 `--header-name-policy` 控制。其他引擎或模式不会忽略
这些选项，即使输入超过字节预算也会先拒绝不适用的配置。未知 DATA 文本的可用字段和
资格限制由 [STEP_IMPORT](../../source/STEP_IMPORT/README.md) 规定。

## 公共入口检查

设置 `STEP_BENCH_SOURCE`、`STEP_BENCH_NATIVE`、`STEP_BENCH_OCCT` 为三个 Release 程序，
`STEP_BENCH_OCCT_BIN` 为 OCCT DLL 目录，再运行：

```powershell
python -m unittest discover -s tools/step_import_benchmark/tests -v
```

测试调用真实程序，核对解析图、独立盒体/孔面积、Unicode路径、warmup/实测分离、
输入预算不执行、盲测排除、匹配续跑、输入变更拒绝、超时和异常进程结果。
