# 独立 STEP 导入与修复：范围及实施顺序

日期：2026-10-15。状态：调查与实施建议，尚未实现 STEP 导入器。

后续完整整合稿见[独立STEP导入与受限修复设计规格](step-import-repair-spec.md)，0.1版已由用户确认为设计基线，包含已确认契约、P0—P7实施/验收门槛与未证事项。“暂不编码”的边界继续有效；不要按本前期草案自动开始实现。

后续设计的规范索引是 [Wayfinder：独立 STEP 导入与受限修复的设计规格](wayfinder/step-import/map.md)。本文保留为前期调查草案，未决建议不代表用户已选择。

用户约束：不使用 OCCT 读取或转换 STEP；当前专注精确 BRep 导入和修复，暂停网格化修改；USD 现有实现暂不调整。新增公开复现仅用合成模型。

2026-10-15 已确定方向：自行实现 C++ Part 21 解析及实体适配，Truck/ruststep 作为源码参考和实验对照。用户允许参考 OCCT 的 STEP 解析文档，OCCT 不作为读取或转换依赖。

已确认按 AP203、AP214、AP242 的共同精确 BRep 子集设计同一条导入链路，并保留基本装配层级、重复实例及各自变换、开放壳与闭合实体类型。通过 FILE_SCHEMA 识别协议和版本，再按明确的实体白名单、属性规则和依赖完整性验收；不能仅识别了协议名就宣称完整支持。AP203 第一版、第二版的 schema 名称不同，须分别识别。[AP203 版本说明](https://downloads.steptools.com/docs/stp_aim/notes_ap203.html)、[AP214 说明](https://downloads.steptools.com/docs/stp_aim/notes_ap214.html)。测试侧允许 OCCT 等独立读取器交叉检查，合成模型仍作主要正确性参照。

## 目标

将 STEP Part 21 中声明支持的精确几何与拓扑转换成 SMLib BRep，保留来源实体 ID、单位、方向、参数范围及诊断。输出是否成功不取决于能否生成三角网格。

拟采用的数据流：

```text
STEP 文本
  → 自有 C++ Part 21 语法/引用表
  → 受支持实体图 + 单位/放置/参数语义规范化
  → 中立导入数据 + 来源映射 + 诊断
  → SMLib 精确几何 / SmBrepData / SmBrep
  → 独立检查
  → 可选、受约束的修复
  → 再检查，发布合格结果或明确失败
```

Truck 的几何/拓扑转换是参考和候选实现，不预先锁定为必经中间模型。详细调查见 [Truck 评估](truck-step-import-assessment.md)。优先保留 STEP 源语义，避免先转换为另一内核表示后丢失单位、参数或被跳过的实体。

## OCCT 参考文档与源码入口

已核对本地 OCCT 提交 `3d097a0328e71b826377d4814ab05ec3c3d23871`。固定版本的 [STEP 用户指南](https://github.com/Open-Cascade-SAS/OCCT/blob/3d097a0328e71b826377d4814ab05ec3c3d23871/dox/user_guides/step/step.md) 为主要实现参考；STEP/EXPRESS 的定义用于判断规范语义，OCCT 文档和源码用于理解一种实现及其兼容策略。不能把文档描述当作本项目已有能力或完整性证明。

| 需要核对的规则 | OCCT 阅读入口 | 自研 loader 的处理 |
|---|---|---|
| 文件语法与模型加载 | `StepFile/step.lex`、`step.yacc`、`StepFile_ReadData.cxx` | 自行实现词法、语法、源位置和引用图；保留加载诊断，与内核对象构建分离 |
| 实体属性读取、类型与引用 | `RWStepShape_RWEdgeCurve.cxx`、`RWStepShape_RWFaceBound.cxx`、`RWStepGeom_RWSurfaceCurve.cxx` | 用于核对属性顺序、继承、类型和值；原始实体不因兼容处理而静默改写 |
| 几何/拓扑映射 | STEP 指南 §2.4，`StepToGeom/StepToGeom.cxx`、`StepToTopoDS_TranslateEdge.cxx`、`StepToTopoDS_TranslateFace.cxx` | 按目标 SMLib 的语义重写适配；跟踪边曲线方向、定向边、面边界方向和面相对曲面方向，不能只保留一个布尔标志 |
| 单位与精度 | STEP 指南 §2.2.2、§2.5，`STEPConstruct_UnitContext.cxx` | 按 representation context 解析长度/平面角单位和 uncertainty，明确换算因子；不使用进程全局单位状态 |
| pcurve / surface curve / seam | STEP 指南 §2.4、§2.5，`StepToTopoDS` 边与边环转换 | 保留同一边在每个面上的使用关系及两侧 pcurve；转换后独立测量与3D边的一致性，重建属于显式动作 |
| 装配关系与放置 | STEP 指南 §2.2.1，产品与 representation relationship 说明 | 识别 mapped item 与带变换的 representation relationship 两类关系，避免只扫描 solid 然后遗失放置和实例 |
| 后处理 / 修复 | STEP 指南的 Shape Processing、SameParameter 与容差说明 | 作为风险清单和测试来源；独立制定修复预算，记录前后变化，不照搬 OCCT 内部拓扑或默认修复序列 |

上述源码位于 OCCT 的 `src/DataExchange/TKDESTEP/`。首次具体实现某实体前应读取其当前源码与对应 EXPRESS 定义；当前只完成分层入口与部分实体读取代码的核对，未逐行审计整个转换器。

尤其需要区别：OCCT 指南允许某些策略下为获得其拓扑有效性而增大容差，并在转换中处理 pcurve。我们应把忠实读取结果、规范化动作和修复动作分别保存，避免兼容策略成为不可追踪的隐式更改。

## 当前 SMLib 可复用部分：源码证据

基于 `f0d3897388d99a652dc3e1c0f3f52eccdbc1db6a`。下表为源码核对，不代表 STEP 端到端测试通过。

| 部分 | 已有能力 | 限制与处理 |
|---|---|---|
| 文件读取 | [SmApiImportExport.h](../source/SM_API/inc/SmApiImportExport.h) 提供原生 SMB/part 读写 | 在 SMLib/SM_API 范围内未找到 Part 21 实体读取器；不能把这些入口称为 STEP 导入 |
| STEP 参数 | [SmCurve.h](../source/SMLib/inc/SmCurve.h)、[SmSurface.h](../source/SMLib/inc/SmSurface.h) 有 STEP 求值、参数域转换 | 名称不等于文件级支持；每类几何需核对变换 |
| 包装类 | [SmSTEPSurface.h](../source/SMLib/inc/SmSTEPSurface.h) 把求值转发到 STEP 参数化 | 是求值包装类，不是文件解析器 |
| 角度 | [SmCircle.h](../source/SMLib/inc/SmCircle.h)、[SmSphere.h](../source/SMLib/inc/SmSphere.h)、[SmCylinder.h](../source/SMLib/inc/SmCylinder.h) 使用度值角参数 | 导入必须独立解释文件单位、几何类型和参数约定，再转为内核约定；不可原样传入角参数 |
| 拓扑装配 | [SmBrepData.h](../source/SMLib/inc/SmBrepData.h) 保存索引形式的几何/拓扑，并有 STEP 来源标记；[SmBrep.h](../source/SMLib/inc/SmBrep.h) 提供 `MakeTopologyFromData` | 构造调用具有几何所有权与失败清理要求，须在适配器测试；来源枚举不是已有解析能力 |
| 自动处理 | `MakeTopologyFromData` 默认启用 healer，并可构造 UV 裁剪、转换解析几何 | 初始导入需明确设置开关，先保存忠实转换结果。`bCheckUVTrimCurves` 注释标为当前未使用，不能当作验证保证 |
| 修复 | [SmHealData.cpp](../source/SMLib/src/SmHealData.cpp) 有重复顶点、退化、容差及部分缝/区域处理 | 有 TODO、空实现和禁用路径，不能宣称一般几何缺口修复齐全 |
| 状态传播 | [SmBrep.cpp](../source/SMLib/src/SmBrep.cpp) 的导入及 `HealBrep` 包装存在忽略内部修复返回状态的调用 | 新流程必须独立验证最终模型，不能仅以 `SM_SUCCESS` 判定已修复 |

## 首个纵向里程碑

先做一个不会静默丢数据的导入探针，而非宣称支持整个 AP203/AP214/AP242。

1. 支持 Part 21 基础语法、实体编号引用、复合实体、字符串及错误定位。保留未识别实体，区分不相关元数据与目标实体图中的必需依赖。
2. 首批精确实体覆盖带孔平面面及闭合盒体：点、方向、向量、轴放置、平面、直线、顶点、边、定向边、环、面边界、面、壳和实体根。
3. 对单位和放置建立显式规范化步骤。以 mm 作为拟议内部长度单位；源文件单位及换算因子必须保留，不从文件名或标签猜单位。角参数单独处理。
4. 增加圆柱、圆锥、球、环面、圆/椭圆和有理 B 样条。每类几何测试参数求值与方向，不只测实体数。
5. 再接入周期缝、退化边、pcurve、多实体与开放壳。装配先保留关系/变换并明确支持范围，不静默拍平或遗失零件；完整实例化和属性可分后续里程碑。

首版的成功条件是目标根的依赖图完全转换、单位正确、边界和邻接一致。目标根有未知几何、缺引用、转换失败或失面时必须失败；若将来支持部分结果，需显式请求并列出所有失败实体。

## 修复应怎样接入

“原始导入”和“修复后的模型”分开保存。修复默认在副本上进行，验收通过后才替换输出。

| 类别 | 拟议策略 |
|---|---|
| 解析/引用错误、非法控制点或节点数 | 拒绝或标为不支持；不能靠 healer 猜测数据 |
| 已证明的单位、角参数、方向转换错误 | 修导入映射，不把问题转嫁给后处理 |
| 顶点/边端点小偏差 | 先测量，只有在配置的位移和容差预算内才候选修复 |
| 周期缝、退化边、裁剪环不一致 | 同时检查源拓扑、3D 曲线和 UV 数据；逐类可复现后修复 |
| 缺面、一般几何缺口、未知边交点 | 首版明确拒绝/未修复，不能仅提高容差后宣称成功 |
| 开放壳 | 保留为开放壳；不能为满足实体验收而自动封口 |

每次修复报告至少包含：源实体 ID、问题类型、执行动作、前后最大间隙/位移、容差变更、拓扑计数变化、删除/新增对象、仍未解决问题，以及操作状态和独立验收状态。不能以提高容差作为几何缺口消失的证明。

现有 healer 的一般缺口处理 `Fix_BadGaps` 和交点补全等路径不完整，`Fix_BadLoops` 调度禁用；需按 [healing 指南](../.agents/operations/heal_import.md) 和实际源码核对。不要在建立失败复现前直接启用被禁用的阶段。

## 不依赖网格的验收矩阵

- **完整性**：根实体、壳、面、环、边、顶点和依赖引用数量可对账；不允许错误经 `filter_map` 或日志输出变成成功。
- **单位/变换**：同一合成模型在不同单位和轴放置下得到一致的目标尺寸；验证重复实例不会丢失。
- **几何**：独立解析公式或 Bernstein/B 样条参照求值；边端点与顶点、3D 边与面贴合；pcurve 经曲面求值与三维边一致。采样通过只能作为证据，不能称为全域证明。
- **拓扑**：循环引用合法、环连通/方向正确、共享边关联正确、开放与闭合语义保留；`ValidatePointers` / `AssertValid` 是其中一层，不是唯一裁判。
- **修复**：干净模型不改变；预算内合成缺陷修复后通过同一检查；超预算、缺面、不可修复案例明确拒绝；重复修复不会持续漂移。
- **工程**：实体 ID 可定位、失败不泄漏、不输出伪成功；资源预算与取消独立于解析能力。

已有本地 MW-SDK 对照台可复用验收思想，但不把 OCCT 接入读取链路，也不将现有私有模型或 SDK 放入公开仓库。

## 当前完成与未完成

已完成：SMLib 导入相关入口及修复边界的源码盘点，Truck/ruststep 候选调查，确定首个纵向里程碑与失败契约。

尚未完成：STEP→SMLib 实体适配器、Rust/C++ 边界方案、完整单位/装配解释、端到端导入测试、修复实现或性能/语料验证。此文不是这些能力已实现的声明。
