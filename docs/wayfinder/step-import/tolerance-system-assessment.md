# SMLib 与 OCCT 容差契约初步对照

日期：2026-10-15。目的是回答现有容差系统及其与新导入/修复预算的关系。源码核对加一个SMLib合成setter探针，不是工业模型质量比较。

## 结论

SMLib已有实体容差、容差类别、上下文/模型尺度、三维到参数空间映射以及传播/修复相关逻辑。不能把它描述为“没有容差系统”。当前源码明确启用旧方案，新consistent tolerance方案在配置中标为未完成；本轮不改变任何宏或生产代码。

与OCCT最明确的差距在目前这条产品链路：OCCT已经把STEP单位和source uncertainty连接到工作精度、pcurve一致性处理、实体容差更新和ShapeFix限制；本项目自研loader尚未实现这些契约，更没有证明修复的预算可全程追踪。现有证据不能得出SMLib在所有几何算法上整体更差，也不能把OCCT导入成功作为固定精度证书。

原建议1 μm/5 μm只是尚未定标的候选；用户未批准该数值，现暂停定案。需要先完成源精度→目标有效容差→独立验收→修复预算之间的映射和对账，再讨论数值。

## 固定对象与证据

- SMLib源码：`f0d3897388d99a652dc3e1c0f3f52eccdbc1db6a`。
- OCCT源码：`3d097a0328e71b826377d4814ab05ec3c3d23871`。
- 合成探针实际加载既有基线SMLib.dll，SHA256 `8cd99a41d961734baf2925879e5f46382d83097dd568b6924e75dbaf351068b2`；没有覆盖运行中的GUI DLL，也没有使用已撤回的网格修复DLL。
- 编译头文件宏打印为`SM_USE_OLDTOL`，运行时的逐实体行为与该路径一致；该打印不是从DLL导出的独立编译标志证明。
- 本机证据：`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/tolerance-probe/`的源码、构建及`result.json`；OCCT只读证据为同级`occt-tolerance-contract.md`。

## SMLib 确认存在的机制

| 机制 | 证据 | 边界 |
|---|---|---|
| 当前方案及开发中方案 | [SmConfig.h](../../../source/SMLib/inc/SmConfig.h)定义`SM_USE_OLDTOL`；new scheme明确标为未完成、不ready | 不把宏名字当成熟度或可靠性证明；不直接切宏试图升级 |
| 逐面/边/顶点zone tolerance | [SmFace.h](../../../source/SMLib/inc/SmFace.h)、[SmEdge.h](../../../source/SMLib/inc/SmEdge.h)、[SmVertex.h](../../../source/SMLib/inc/SmVertex.h)保存字段及setter | setter/cascade可以改变有效邻接容差；不是严格的源精度不可变记录 |
| zone / intersection / approximation分类 | [SmTol.h](../../../source/SMLib/inc/SmTol.h)、[SmTol.cpp](../../../source/SMLib/src/SmTol.cpp) | 当前典型getter中Approx为zone的一半，两对象XSect为zone之和；特定算法可传自己的阈值，不能据此概括全部调用 |
| 模型/上下文及尺度相关值 | `SmContext`、`SmTol::SizeEstimateToZoneTol3d`等 | 数字是模型单位，内核不自行解释STEP文件物理单位；不能凭默认值推断真实残差 |
| 参数空间换算 | `SmTol::MapTo1d/MapTo2d`等 | 极点/导数退化及周期分支仍需专门验证；入口存在不等于所有case已测 |
| 向低维边界传播 | [SmFace.cpp](../../../source/SMLib/src/SmFace.cpp)的`SetTolerance`、[SmEdge.cpp](../../../source/SMLib/src/SmEdge.cpp)对应setter | cascade启用时face→edge→vertex；关闭cascade或直接底层写值不能自动得到全局一致保证 |
| gap、tol协调与验收 | `SmHealData::Fix_TolSizes`及BRep验证流程 | 会涉及状态/容差变化；几何闭隙、验收、修复预算是不同问题 |

## 实测：一个新建盒体的容差传播

盒体尺寸10×20×30，未加载任何STEP/客户模型，未执行healer或网格化。在基线DLL中显式调用一张face的`SetTolerance(0.001,FALSE,TRUE)`。

| 指标 | 修改前 | 修改后 |
|---|---:|---:|
| 选中面zone tolerance | 1e-5 | 0.001 |
| 被传播到0.001的边/顶点 | 0 / 0 | 4 / 4 |
| 该面approximation getter | 5e-6 | 0.0005 |
| 与另一未改face的intersection getter | 2e-5 | 0.00101 |
| 顶点坐标改变 | — | 无 |

所有值均为探针模型单位，不能未声明单位就直接标为mm。这个实验仅证明容差字段、传播和相关getter可观察，并直接说明“容差变大”不等于“几何被移动或缺口缩小”。没有测试交点算法是否正确，也没有证明所有局部修改都使用相同传播路径。

## 与 OCCT 的对照维度

| 维度 | SMLib当前证据 | OCCT当前证据 | 对本项目的影响 |
|---|---|---|---|
| Vertex/Edge/Face局部容差 | 存在并已做小探针 | `BRep_TVertex/TEdge/TFace`存在，builder文档规定父子容差关系 | 需要抓取全体相关实体的before/effective/after，不只保存一个BRep最大值 |
| 几何/数值阈值 | Zone/XSect/Approx及数值常量、算法参数 | Precision::Confusion/Intersection/Approximation及局部算法阈值 | 名称/数字不同不能直接比较；应对照具体操作含义 |
| 单位/source uncertainty导入 | 没有自研STEP入口，尚未接通新规范单位和source记录 | `STEPControl_ActorRead::PrepareUnits`与DESTEP参数已有连接 | 明确这是新loader必须补的链路，而非新增一个全局epsilon |
| pcurve一致性 | 有UV/gap检查与生成机制，部分约定/状态路径需探针 | SameParameter等流程可改pcurve并增长edge/vertex容差 | 源master与受控派生UV预算必须独立于内核操作状态 |
| 修复动作范围及上限 | 全healer不能直接满足已选白名单；部分gap修复无实现/禁用 | ShapeFix precision/min/max及多级设置存在，但仍需逐路径核对 | 新loader须记录实际变化、绝对值、增量、传播及拒绝原因 |
| 独立质量证据 | 当前验证会涉及缓存/容差状态，不能单靠AssertValid | BRepCheck也基于当前实体容差，不是固定质量保证 | 两边都需要固定外部验收门槛与源比较 |

OCCT来源入口：`BRep_Builder.hxx`、`Precision.hxx`、`STEPControl_ActorRead.cxx`、`DESTEP_Parameters.hxx`、`ShapeFix_Root`及[固定STEP指南](https://github.com/Open-Cascade-SAS/OCCT/blob/3d097a0328e71b826377d4814ab05ec3c3d23871/dox/user_guides/step/step.md)。详细行号与未测范围在本机OCCT报告。

特别注意：OCCT的Preferred最大容差允许部分3D/pcurve及相关顶点偏差越界；Forced可以直接截小容差，指南指出可能留下无效模型。存在设置字段不证明每个算法遵守全流程硬预算。不能把这些默认值搬过来作为本项目批准数值。

## 建议先补的设计和验证

1. 明确每种容差的来源、单位、存储值/有效值、作用实体及传播关系，记录与用户要求精度的映射。
2. 按源引用/几何质量测得初始误差，选择明确的工作容差政策；source uncertainty仅作来源信息，不自动授权位移。
3. 每次构建、验证、派生UV或修复后对账实际容差和gap；不能通过压小字段伪装精度，也不能通过放大字段掩盖超预算。
4. 先用等价单位、缩放件、小特征大模型、3D/pcurve偏差和局部传播合成例验证，再用真实分层模型定标。当前尚未做实体容差分布或OCCT/SMLib导入残差长跑。
5. 由调用要求和独立质量门槛约束修复额度；固定默认值是否合适仍为未决，不根据本次setter小测试制定生产精度。

本轮没有修改SMLib/OCCT源代码，没有运行修复或网格化，没有批准原1 μm/5 μm数值预算。
