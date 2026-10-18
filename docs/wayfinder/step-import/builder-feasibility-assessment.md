# SMLib 构建路径与失败清理：合成验证

状态：2026-10-15 研究结果。生产代码未改；没有实现 STEP loader、修复内核或调用网格化。后续构建计划与执行/所有权政策已由[完整规格](../../step-import-repair-spec.md)及接口决策确认；本文只作研究事实输入，不代表生产实现已完成或获得资格。

对应研究：[哪条 SMLib 构建路径能满足保真和失败清理要求](tickets/11-builder-feasibility.md)。

## 结论

SMLib 已有可用的精确拓扑构建基础。两个入口都能保留已测的显式共享边与反向 occurrence；top-down 还验证了闭合实体和单空腔。它们都需要独立的构建适配器，任何裸入口均未满足完整的保真、不可变输入和失败原子性契约。

建议以受控 `SmBrepConstructor` 作为首个纵向切片的候选，利用显式身份、面域和区域控制；`MakeTopologyFromData` 仍是可封装的数据构建候选，当前证据没有证明必须弃用它。默认入口的最终选择属于下一张设计决策，不根据某个函数返回成功直接确定。

普通返回错误和调用边界取消已有有限清理证据；任意分配故障、异步取消和内核崩溃的完整进程内清理尚未证明。专属 context 解决生命周期与状态归属，不能代替对象所有权和进程隔离。

## 证据基线

- 当前源码审计 HEAD：`f0d3897388d99a652dc3e1c0f3f52eccdbc1db6a`。
- Windows x64，MSVC 19.50，C++17 Release `/MD`；探针全部位于仓库外。
- 使用既有 `planar-regions/runtime/SMLib.dll`，每个进程记录实际加载路径，SHA256：`8cd99a41d961734baf2925879e5f46382d83097dd568b6924e75dbaf351068b2`。
- 本轮没有重建内核。固定源码的解释与固定 DLL 的运行结果分开，不把探针头文件宏当作 DLL 的运行时配置查询。
- 全部使用合成几何。没有读取真实模型、STEP 文件或测量导入覆盖率；没有任何生产默认容差数值的批准。

本机原始报告、源代码、命令和 JSON 根目录：
`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/builder-feasibility/`。

## 两条路径的实际差别

| 项目 | MakeTopologyFromData | 受控 top-down |
|---|---|---|
| 共享边与反向使用 | 手写两三角，2F/5E/4V，公共边4个运行时EU，方向/端点/抽样lift正确 | 手写两方片，2F/7E/6V，显式复用同一边/顶点，反向使用正确 |
| 输入生命周期 | 改写 mapping/owner；目标活着时复用暂存数据曾成功，目标销毁后复用返回1006。只能按一次性暂存设计 | builder存裸状态指针；从StartBrep起外部持有候选，原中立数据保持独立；失败废弃整个builder |
| 面UV域 | 即使关闭healer/analytics，也会与surface自然域求交。超域对照被裁剪后仍通过验证 | 传入域原样保存；超域对照有pointer report11，但返回码仍1000 |
| 直接UV安装 | 反向use的UV可能存OPPOSITE成员 | SetEdgeCurve(uv,TRUE)也有相同存储约定冲突 |
| 可验证的UV适配 | 不在批量建图时装UV；完成后fresh copy，经SetUVTrimCurve安装到SAME成员，实测保持共享/方向和抽样lift | 先装精确3D与显式参数，完成后逐occurrence装UV，同样有效 |
| 容差 | F/V及较小E从1e-6提高到BRep下限1e-5，较大E 2e-5保留；forceSameTol不表示逐实体不变 | 已测显式1e-5在验证前后不变；modelSizeEstimate参数在当前OLDTOL路径未使用 |
| 区域/空腔 | 清理探针回建原生盒体通过，不构成独立空腔映射证据 | 四面体和含内四面体空腔已通过独立点分类/体积对照；void标志必须显式设置 |
| 隐式连接操作 | 批量建图仍有不可忽略的输入与域/tol副作用 | EndShell(FALSE)禁用stitch；复用edge的某些路径会按几何找radial sector，不能未经核对相信fallback |
| 普通失败清理 | 已测前期shell错误、后期空loop错误和完整候选丢弃 | 已测shell/face阶段取消及未关闭face的EndBrep错误 |
| 周期和闭合边 | 原生圆柱/球数据回建保留同面seam双occurrence；圆柱两条非零闭合圆边保留，deferred UV后观测快照与源一致 | 本轮没有对周期曲面执行独立top-down重建 |

上述数值均为探针模型单位，既不表达内核固有毫米单位，也不替代应用质量配置。各组UV/几何抽样只针对对应有限合成输入，不能推广为一般曲线全域最大误差证明。

## 周期缝和原生极点表示的补充证据

圆柱`R=10,H=20`和标准球`R=10`各做原样UV及延后UV两组回建，全部在独立进程自然结束，源/目标pointer与LEVEL2零报告。圆柱3F/3E/2V，含两个端点重合但非零长度的圆边；球1F/1E/2V。两种形状各有一个edge在同一face内出现两次，未按`(face,edge)`去重。

延后UV路径先逐occurrence复制UV，并将staging UV index明确设为缺席，然后构建、通过SAME成员安装fresh copy。**仅把bMakeUVTrimCurves设FALSE不会阻止入口安装已提供的UV。** 完成后，记录的几何采样、域、loop、方向、UV归属/分支和逐实体tol/approx/xsect与源一致，验证前后这些字段也不变。圆柱两侧缝UV中点为`(0,10)`与`(62.83185307179586,10)`；球为`(0,0.5)`与`(1,0.5)`，三维重合没有让UV分支合并。

解析轨迹抽样最坏偏差约`3.56e-15`模型单位，UV lift约`9.98e-15`；这不是一般Hausdorff或源STEP转换证书。原样UV回建仍有OPPOSITE成员存UV的问题，validator没有阻止。deferred路径把该成员约定恢复，未观察到tol增长。

标准球原生表示用缝边和两个极点顶点，没有vertex-loop，也没有本轮需保留的type3极点EU。因此证据证明该原生表示可以承载和回建，**尚未证明STEP显式退化边的UV行程如何无损归入该表示**。圆柱/球的native参数也不等于STEP参数，源角参数到target UV的映射仍需独立契约。

本例solid-cylinder API实际通过等上下半径cone构造，侧面运行时类型为SmCone；不能由此推广为SmCylinder及全部解析参数路径都已验证。

## 必须增加的源语义验收

1. **UV存储和分支。** 两条直接安装路径都可能违反UV应存SAME成员的公开约定，pointer/LEVEL2仍全通过。已验证的绕开方式是每个occurrence安装fresh copy；把同一指针再次传setter会提前返回，不能修正归属。源master pcurve仍须独立保留。
2. **顶点与方向。** top-down新建反向edge时，现有顶点参数须按实测的curve min/max顺序传入；按注释中的loop order给会产生端点错误report61。不能让接口注释代替有向端点断言。
3. **空腔。** 显式void标志的外/内四面体体积为`(1000-8)/6≈165.33333333333317`；省略标志时通用验证仍通过，但体积为`1000/6≈166.66666666666652`。必须单独检查源区域含义、目标flags、点归属及属性。
4. **报告内容。** 本轮多个负例的ValidatePointers返回SM_SUCCESS=1000，却产生reports。必须同时检查状态、报告和前置条件；图不可信时停止深验。
5. **验证副作用。** 缺UV时LEVEL2会生成UV。检查前后需逐实体tol/关键阈值和派生状态对账；已有UV时本轮记录字段保持，并不证明所有validator都是只读。
6. **奇异表示。** 先有外边环再建内vertex-loop可通过；把vertex-loop作为首环产生37/38报告。该结果只证明内vertex-loop承载能力，不证明任意球/锥极点、源退化边UV行程或新增seam的正确映射。

UV setter探针在同构精确复制场景传入max-distance 0；生产适配器必须按实际测得并验证的偏差与质量配置处理，不能照抄零值掩盖误差。

## 清理与隔离的实测边界

七种生命周期路径分别预热后运行800次，并以更长预热复测。Windows HeapWalk观测中，成功构建、后验丢弃、前/后期普通错误和受控取消均未出现随批次数持续增加的活跃堆分配。多数为0；部分进程出现4292B/9块或8584B/18块增加后平台，原因未归属，不能声称全链路零增长或已证明是缓存。PrivateUsage也会变化。

阳性对照的64个smos_Calloc分配被观测为8192B/64块，释放后恢复原值；这使计数有实际对照，但不构成所有分配器和资源的完备覆盖。现有smos_HeapLeakCheck恒成功、GetSizeUsed/TotalCount恒0、FreeAllMemory空实现，不能用作本次证据。

数据构建错误均恢复context DoingBoolean状态。候选BRep、暂存数据、源几何、context按明确顺序销毁。SmBrepConstructor没有自动事务析构，SmContext也没有统一回收其所有对象的清单。

另有两项人工隔离控制：候选建好尚未发布时，父进程terminate worker，以及worker自行FailFast；两者均退出且未输出完整结果，之后新worker成功。这证明探针提交门和进程边界可用，**没有实现或证明生产模型传回、原子发布、父侧句柄/临时文件清理或真实内核crash恢复**。

## 进入后续设计的条件

已测范围支持“独立中立模型 → 受控一次性候选 → 定义/拓扑/区域/UV与有效tol验收 → 成功后发布”的结构。目前不需要为已通过的平面/四面体案例改内核。

仍需明确：首个构建后端、源到目标身份映射、几何所有权移交、发生错误/调用边界取消时的废弃规则、硬取消/崩溃是否采用worker进程，以及生产结果的提交协议。尚未建立可靠分配故障注入，因此不能承诺进程内任意失败点都完整清理。

一般NURBS权重/周期参数、STEP角参数与pcurve的非线性转换、完整源master一致性、任意奇异规范化及模型库验收仍未执行。这些没有因为本轮构建探针而成为已支持能力。最终规格就绪票必须保留这些进入/退出条件，不提前宣称读取器可交付。

## 原始证据索引

- `data-path/findings.zh.md`、8模式JSON、54项局部核对及20次fresh staging：批量构建与deferred UV适配。
- `topdown/findings.zh.md`、11模式JSON：独立平面片/四面体/空腔、方向和验证负例。
- `cleanup/findings.zh.md`、result.first.json/result.json/warm250_all.json、isolation_control.json：普通返回、调用边界取消、活跃堆对账及人工隔离控制。
- `periodic/findings.zh.md`、4模式JSON和summary.json：原生圆柱/球回建、缝双occurrence、闭合非零边、UV归属和分支。

源敏感判断的逐行引用、实际运行库路径与哈希、可复现命令保存在对应报告；完整源码证据应以固定提交重新定位。研究核对数量不等于STEP能力数或真实模型导入成功数。
