# 08 验收：同形闭合表面的 surface-model 角色

2026-10-16（Asia/Shanghai）。固定审查基点 `79f8b77`，产品实现 `148dc22`，资源观察器修正 `81eba48`。
08 三条原验收完成，09—12保持on-hold。仅本地提交，未推送。

## 本次交付

P05 从原始STEP经自研源检查与真实SMLib构建，交付 `ClosedSurfaceModel` owning只读快照。
保留 MSSR→SBSM→CLOSED_SHELL 的身份，不从闭合边界、流形拓扑或可算的有向积分推导材料资格。
源表示、形状、壳的ID/类型与角色可从 `Report::sourceShape` / JSON `source_shape` 查询；
`ModelSnapshot::Source()` 同时保留完整不可变源语义。

中立计划把闭合边界与材料资格分开。闭合表面与材料盒体使用同一精确矩形边界证明、
唯一源边与实际native共享边、逐面SAME UV、两侧shell/owner/radial核验和全域几何证据。
范围与07一致，支持已验证的有理刚体、参数和源顺序变化；不按ID或文件名派发。

SMLib的构造器用外/内空间region及两个shell表示闭合边界。P05两个region均显式为void，
其中有界void只是表面内部的空间单元，不表示被材料包围的空腔。私有material指针为空，
材料volume/mass及native有向体积积分路径均不执行，所有材料分类均返回不适用。
面积、面积形心、bbox、边界最近点与每面查询继续可用。

## P04/P05独立对照

使用01原始P04/P05和各自冻结truth/Q，先分别独立核验，再比较两模型的逐边、逐面和逐use观测。
下表数值来自最终Release实际输出；Debug对同一独立合同也通过。

| 项目 | P04材料盒体 | P05闭合表面 |
|---|---|---|
| 源representation | ADVANCED_BREP_SHAPE_REPRESENTATION | MANIFOLD_SURFACE_SHAPE_REPRESENTATION |
| 源shape | MANIFOLD_SOLID_BREP | SHELL_BASED_SURFACE_MODEL |
| 源shell | CLOSED_SHELL | CLOSED_SHELL |
| 已交付角色 | MaterialSolid | ClosedSurfaceModel |
| 拓扑 | 6F/12共享E/8V | 同左 |
| 源有向使用 / native edgeuses | 24 / 48 | 24 / 48 |
| 面积 mm² | 2200.0000000000337 | 2200.0000000000337 |
| 唯一边长总和 mm | 240 | 240 |
| free boundary edges | 0 | 0 |
| bbox mm | (2,-3,5)→(12,17,35) | 同左 |
| 形心 mm | (7,7,20)，材料形心 | (7,7,20)，面积形心，符合1e-7 mm预算 |
| 材料region数 | 1 | 0 |
| 有界void空间region数 | 0 | 1 |
| native拓扑IsManifoldSolid谓词 | true | true |
| 材料体积 mm³ | 6000.0000000000928 | NotApplicable |
| 中心点材料分类 | Inside | NotApplicable |
| 材料census / 体积积分证据 | 实测 / 受预算约束 | NotApplicable / 不执行 |

原生 `SmBrep::IsManifoldSolid()` 在当前源码只检查region数量及边的流形性，不检查 `IsVoid`。
因此该谓词在P05为true是实际拓扑观察，不是材料资格；新 `native_role` 分别报告实际region/shell、
材料/有界void数量及该谓词。P05的材料census字段保持null，不能把原生有界void计数误报为材料空腔。
材料体积和内外分类接口返回空值，CLI明确标为NotApplicable，不能用0或Outside假装查询成功。

P05定义误差上界 1.6763426529215947e-13 mm，最终一致性和全段UV上界均为 1.0251056588688367e-13 mm，
分别符合1e-8/1e-7/1e-7 mm预算。五阶段共135条工作容差保持1e-5 mm，干净增长0。
每面支撑面、normal/window、源plane、bound/loop/use身份及实际UV分别核验；
与P04的edge mappings、edge adjacency、loop mappings和face mappings逐项相同。

## 验收与回归

- Debug/Release各11个CTest目标通过：三个旧组件合同（45组）、F1/孔/材料盒体/闭合表面四个原生接口合同，
  以及四组同进程资源检查。每配置23组真实CLI/独立Python检查通过，无跳过。
  观察器修正不改变产品EXE；最终核对其SHA未变后，明确复用这23组结果，未伪称再次运行。
- 原49样件为5 Accepted、25 Rejected、13 Unsupported、6 Deferred。
  P01—P05成功；普通FACE_SURFACE的N18等既有源阻断保持原诊断，native未启动。
  不由P05通过推广一般闭合多面体、斜盒或未准入的面类型。
- P05通过反向源记录/面SET顺序、稀疏ID、非默认LINE幅值，以及绕Z轴和全三维有理旋转控制，
  每项分别保留源身份、边界几何和表面角色。三维旋转矩阵九项均非零。
- P05中心、外部及面/边/顶点测试点的材料分类均NotApplicable；
  外点 (15,7,20)→(12,7,20)、距离3，以及角点 (1,-5,1)→(2,-3,5)、距离sqrt(21)，
  保持独立最近点真值。对应P04材料分类仍正常。
- 11种新增错误观测反控覆盖模型/源角色、representation/shape/shell身份、材料/void标志、
  伪造0体积、伪造材料census、体积证书和证据政策；独立比较器均拒绝。
  原07的13种材料反控及05/06的方向、孔、参数和误差边界继续通过。
- 同时持有P04/P05，释放P04后P05仍能查询；P05根快照释放后各owning面视图仍可查询，
  全部native模型/视图释放后源与两份报告仍分别保持自己的角色和来源。
- F1/孔/材料盒体/闭合表面分别55/97/250/242个调用检查点逐一验证限额拒绝；
  完整P05候选报告超限也销毁候选，不发布部分模型。原报告字节N/N−1合同继续通过。

01—04、冻结303材料/独立真值/schema见证本轮均未改。最终收据逐文件核对旧组件源码身份，
显式复用07完整验证中的138项旧核心Python、196份输出/退出码逐字节对比和169个预算边界，
没有声称本轮重新运行这些Python/字节比较。旧C++合同则在本轮两配置实际重建、执行。

## 普通失败与资源清理

资源观察使用同进程导入接口，混合成功、每个native调用预算边界、后验预算不足和必需报告拒绝。
每配置预热后F1/P03/P04/P05分别测量880/1552/4000/3872次；
同一观察器故意保留64个模型作为阳性对照，释放后检查回落，不能用CLI子进程退出冒充清理证据。

| P05 heap busy bytes / blocks | Release | Debug |
|---|---:|---:|
| 测量开始 | 1,928,884 / 4,106 | 2,041,568 / 4,898 |
| 测量结束 | 1,928,884 / 4,106 | 2,041,568 / 4,898 |
| 持有64模型前 | 1,928,884 / 4,106 | 2,041,568 / 4,898 |
| 持有64模型时 | 30,565,172 / 165,130 | 54,859,360 / 371,682 |
| 释放64模型后 | 1,928,884 / 4,106 | 2,041,568 / 4,898 |

Debug接受判定使用当前共享调试CRT的normal/client用户分配，Release继续使用进程堆；
原4096字节/8块增长门槛、65536字节/256块的正控门槛保持。
Debug P05的CRT用户分配从 67,587字节/1,085块到
67,587字节/1,085块；
持有64模型为 33,812,611字节/367,869块，
释放后 67,587字节/1,085块。
完整五次采样和总堆/CRT/PrivateUsage原值留本地；PrivateUsage变化不等于对象泄漏。
本次未授予任意OOM、崩溃、worker或硬取消恢复资格。

### 观察器错误的诊断与修正

初次Debug完整CTest为10/11通过，P04资源测试触发总堆增长断言；P05资源检查已通过。
让资源程序在断言前输出完成的采样后，P04复现前四次不变、最后一次增加33778字节/83块，
释放64模型后回到这个较高基线，不能直接将它当作已修复或隐去。

随后用同一SDK/CRT链接的无模型对照：仅一次空输入源失败，之后300秒不构建/查询任何BRep，只采样。
稳定阶段到末次总堆仍增加42122字节/85块，CRT用户分配仅增加456字节/3块。
因此旧全进程堆断言在没有CAD分配时也会失败，不能单独裁决模型残留。
共享CRT的4000次P04复现则保持67587用户字节/1085块；64模型正控到33799299字节/367613块，释放后精确回落。

按当前SDK源码SmObject/SmContext的new→smos_Calloc及SmMemory.cpp的calloc/malloc路径、
三个Debug模块共享ucrtbased.dll、未启用TBB的已核对配置，Debug改用CRT用户分配作判定，
同时保留总堆诊断。修正后两配置完整11个CTest重新执行，未提高限额或重试掩盖旧失败。
观察器及其正控仅授予当前匹配共享CRT/非TBB SDK的普通模型清理证据，其他分配器需重新验证。

## 两轴审查

### Standards

硬性规范违规0，阻断问题0；保留1条非阻断的possible Primitive Obsession设计建议：
未来可由内部角色枚举派生 `materialSolid`/`closedBoundary`，避免两个bool的潜在无效组合。
当前唯一MakePlan入口赋值一致、原生角色核验完整，按审查建议不在08扩大为角色系统重构。
不是“所有审查发现均0”。观察器修正的聚焦复审新增硬违规/可操作缺陷0，
明确维持当前共享CRT、非TBB SDK的覆盖边界。

### Spec

遗漏/部分实现0，未授权扩展0，错误实现0。
只读审查确认源表示链、全void闭合表面、材料指针与查询门槛、逐面汇总和原有构建/所有权/发布合同符合08。
两轴审查未执行测试；观察器修正聚焦复审无阻断和规格偏离。上述实际运行由主执行进程完成。

## 身份与本机记录

内部适配器C++20，VS18/v145/MSVC19.50、Python3.14；沿用05匹配源码的原生Debug/Release SDK，
SMLib仍为C++17。本票没有改SMLib/SM_API或测试包机制，也没有引入OCCT读取。
两配置SDK源身份、实际加载路径、DLL哈希和DEPENDENTS核对通过，无OCCT/STEPcode/USD读取依赖。

Release CLI SHA256：`4d54f0ff5ebcb9e33bde36b18272391a9d8a39dbff22ce21e33aba0bbcc9a63e`。
Debug CLI SHA256：`c4b80b3fe2a3d237aa13da7cdb3f7b6c111bc00485fb055cb0c429e692d62e00`。

本机根 `C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/`：

- `ticket08-final/validation.json`：绑定81eba48（产品代码148dc22），保存两配置11个CTest目标、23组CLI、源码/运行时身份，
  旧组件/精确EXE的23组CLI复用条件，以及原始观察器失败/无模型对照。SHA256 `30385d697a40c446f378fce99629e53d689c43ca800d64bd50570a32ae2cf7d4`。
- 同目录保存修正后CTest完整资源输出、原始stdout/stderr、P01—P05真实query输出和三维材料控制；
  ticket08-qualification保留初次10/11失败日志及两配置23组CLI，observer-control保留无模型300秒对照。
  resource-observation/crt-observation日志保留原断言失败及CRT分配域复现。
  构建及日志只留本地，没有新增测试包发布协议。
- `ticket08-red.log` / `ticket08-green.log`：P05 owning合同真实Unsupported→Accepted。
  `ticket08-role-red.log` / `ticket08-role-green.log`：真实CLI源链报告缺字段→通过。

比较器初次适配曾把冻结truth的通用SurfaceModel标签当作目标Open/Closed细分类，造成比较失败；
已改为按原始STEP shell类型独立细分，原始truth、预算与产品几何未改，最终全部检查通过。

08完成后，资格仍限当前开放平面/孔和已证明的矩形盒体边界及其源角色。
09单位转换、10可选声明、11进一步生命周期/取消、12首profile汇总尚未开始；
一般多面体、截面/切割、修复、公开绑定/GUI、worker及全内核C++20/23资格不由本票自动授予。
