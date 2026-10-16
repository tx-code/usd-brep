# AP203e2首个平面／盒体profile：规则与依赖闭包

版本：203e2-P0-Planar-0.1，2026-10-15。状态：**静态设计清单，不是已实现或已通过的导入能力**。本附件细化已确认[总体规格0.1](../step-import-repair-spec.md)的首个窄切片，暂不编码，不修改总体首版覆盖目标。

原文身份固定为MBx-IF当前发布的N2635 longform，EXPRESS SHA256为`c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`。文件自述ISO/TS、2009年修订与发布页的AP203e2标签分别保留，不声称已认证ISO最终出版附件。来源与同字节固定行号入口见[AP203来源报告](p0-ap203-provenance.md)。

## 1. 本profile究竟限定什么

这是一个用于首轮完整证明的**封闭输入profile**：先完整读取所有DATA，再对全部记录及complex成员核对允许清单。不是从任意大装配扫描出一个盒体，也不是把不支持内容先删掉再验证。总体规格允许独立选根和经证明无影响的附加数据；本profile先采用更窄的输入包络，其他范围以后以新profile增加，不能把这里的窄条件说成STEP普遍要求。

| 部分 | 项目profile范围 | 源规则与项目条件的区别 |
|---|---|---|
| schema绑定 | 精确AP203e2名称及OID`{1 0 10303 403 2 1 2}`绑定N2635制品；canonical文件写完整标识 | 裸名可识别家族，本profile不据裸名猜版；新别名/矛盾声明不会覆盖已有绑定 |
| 文件结构 | 完整、无重复ID/破损语法/悬空必需引用的自包含Part21文本；DATA按相应载体规则全部可见 | canonical载体为`2;1`单DATA；本附件不替代P1的多DATA/编码/载体规则资格 |
| 产品主图 | 一个AC、APD、PRODUCT_CONTEXT、PRODUCT、formation、PRODUCT_DEFINITION_CONTEXT、PD、PDS、SDR、具体shape representation | 恰一条无歧义主链是本profile范围；不能冒充e2全局规则对所有文件都要求恰一条 |
| 产品角色 | PD context的name为`part definition`、life_cycle_stage为`design`；product context使用`mechanical` discipline | 这些具体值用于界定首片分支，原值保留；源是其他合法角色时Unsupported，不能自动改字符串 |
| 产品附属 | 可无category；或一个PRPC，name为`part`、products只含本PRODUCT；不独立接受基础PRODUCT_CATEGORY记录 | 不将AP203e1/AP214的无条件分类/owner要求移植到e2；其他category先因范围不支持而拒绝 |
| 表示与壳角色 | 一个ABSR含一个MANIFOLD_SOLID_BREP／CLOSED_SHELL；或一个MSSR含一个SBSM，后者含一个OPEN_SHELL或CLOSED_SHELL | SBSM闭合表面不变成material solid；合法辅助AXIS2_PLACEMENT_3D item可保留，不当全局变换 |
| 几何与拓扑 | direct LINE、PLANE，显式VERTEX_POINT，OE/EDGE_LOOP、FACE_BOUND/FOB；每面一个显式FOB，允许合法孔 | 本profile两种表示的面均只准ADVANCED_FACE；FACE_SURFACE仅为祖先，普通FS另设后续profile。显式FOB、非零直线边和简单区域另含项目限制 |
| 单位/context | 一个3D GEOM+GUAC context；GUUNC可缺；长度恰一、平面角恰一、立体角零或一；所消费单位全部可解释 | 此计数为项目profile限制，不是e2新增全局单位规则。长度可SI或已核conversion链；角/sr分支和complex签名见context附件 |
| 额外角色字符串 | representation.name不使用`centre of mass`、`treatment result`；PDS.name不使用六个surface-condition专名 | 这些名字会改变规则分支，准确Unsupported特殊表示用途；普通名称保留，不改名绕规则 |
| 记录归属 | 每条记录必须属于主链、形状/定义依赖，或已列明的APD/category/单位附属闭包 | 不以坐标/label合并；孤立已知记录也不能隐式成为额外根。函数计算临时值不算额外DATA记录 |
| 关闭的能力 | 装配、mapped/relationship变换、源pcurve/seam/trimmed、void、oriented face/shell、退化loop、PMI/style/attribute分配及其他管理图 | 合法但超范围为Unsupported；已支持字段/规则非法才报对应源错误；修复保持关闭 |

六个surface-condition专名是`visual appearance`、`tactile appearance`、`contact ratio`、`hardness`、`treatment result`、`surface texture`。它们是被固定规则比较的值，不做大小写替换、后缀删除或自动改写。

旧MECHANICAL_CONTEXT/DESIGN_CONTEXT和带source的formation作为替代叶准入，精确字段和额外约束见[context附件](ap203e2-planar-context-closure.md)；它们不因deprecated而变成源非法。**只列祖先类型不意味着允许它独立作为输入记录，也不意味着任意祖先/subtype组合都能成为合法complex。**

### 1.1 封闭准入清单

以下是全部可独立出现的simple叶名与complex签名。字段顺序、局部/祖先规则由两份明细逐项给出；叶名总计36种，不表示必须各出现一次。普通/旧context和两formation为替代选择。少掉必需主图或加入超包络记录时都不能交付，但应准确区分必需内容缺失与合法表达不支持。

| 记录组 | 本profile全部simple/internal叶名 | 角色限制 |
|---|---|---|
| 定义几何，6种 | CARTESIAN_POINT、DIRECTION、VECTOR、AXIS2_PLACEMENT_3D、LINE、PLANE | 均为3D；点/轴/向量不能作为另一个独立线框根 |
| 拓扑与形状，11种 | VERTEX_POINT、EDGE_CURVE、ORIENTED_EDGE、EDGE_LOOP、FACE_BOUND、FACE_OUTER_BOUND、ADVANCED_FACE、OPEN_SHELL、CLOSED_SHELL、MANIFOLD_SOLID_BREP、SHELL_BASED_SURFACE_MODEL | 每次源使用、端点身份、方向和壳角色完整；FACE_SURFACE本体不准独立作为记录 |
| 具体表示，2种 | ADVANCED_BREP_SHAPE_REPRESENTATION、MANIFOLD_SURFACE_SHAPE_REPRESENTATION | 二选一且仅一实例；辅助axis项之外恰一shape item |
| 产品链/附属，13种 | APPLICATION_CONTEXT、APPLICATION_PROTOCOL_DEFINITION、PRODUCT_CONTEXT、MECHANICAL_CONTEXT、PRODUCT、PRODUCT_DEFINITION_FORMATION、PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE、PRODUCT_DEFINITION_CONTEXT、DESIGN_CONTEXT、PRODUCT_DEFINITION、PRODUCT_DEFINITION_SHAPE、SHAPE_DEFINITION_REPRESENTATION、PRODUCT_RELATED_PRODUCT_CATEGORY | 单主链；PC/PDC及formation子型按替代叶核验，PRPC可缺或单part；不独立接纳其余基类 |
| 单位依赖，4种 | DIMENSIONAL_EXPONENTS、MEASURE_WITH_UNIT、LENGTH_MEASURE_WITH_UNIT、UNCERTAINTY_MEASURE_WITH_UNIT | dimensions只供需要的显式单位维数；factor最具体叶为MWU或LMWU；UMWU仅作GUUNC条目，不改作factor |

仅允许下列六种complex constituent集合，集合内字段与排列遵照[context/单位明细](ap203e2-planar-context-closure.md)：

| complex签名 | 本profile用途 |
|---|---|
| GEOMETRIC_REPRESENTATION_CONTEXT + GLOBAL_UNIT_ASSIGNED_CONTEXT + REPRESENTATION_CONTEXT | 无GUUNC的唯一3D context |
| 上行 + GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT | 有非空GUUNC的唯一3D context |
| LENGTH_UNIT + NAMED_UNIT + SI_UNIT | SI长度；NAMED_UNIT槽为`*` |
| NAMED_UNIT + PLANE_ANGLE_UNIT + SI_UNIT | SI平面角；NAMED_UNIT槽为`*` |
| NAMED_UNIT + SI_UNIT + SOLID_ANGLE_UNIT | 可选SI立体角；NAMED_UNIT槽为`*` |
| CONVERSION_BASED_UNIT + LENGTH_UNIT + NAMED_UNIT | 转换制长度；NAMED_UNIT槽为显式dimensions引用 |

几何、产品、measure实体的其他合法external complex映射，包括CC2将全部子型拆为external mapping的形式，未在本profile冻结签名，准确Unsupported。它们不是schema普遍非法；P1的相应载体/映射能力另行验收。任何不在六集合中的附加constituent不会因其中包含可识别叶名而被忽略。

### 1.2 MSSR普通面分支为何暂不准入

N2635的MSSR.WR8—WR12把来自`fa.bounds`的FACE_BOUND变量直接按`\path.edge_list`访问；WR13/WR14的vertex-loop路径也有类似问题。不能把文档中补上`.bound`后的意图解释叫作原式证明。准确行号和原式边界见[几何明细§6](ap203e2-planar-geometry-closure.md)。

首profile所有面均为ADVANCED_FACE，因此MSSR.WR6—WR14的`ADVANCED_FACE IN TYPEOF(fa)`前项均可明确证明True，外层析取的结果不依赖有问题分支；AF自身10条局部WHERE仍全部核验。源文件保持原字节、原hash，普通FS未准入，不做静默schema修补。这也不意味着已经证明整份schema可被某通用EXPRESS编译器/执行器原样执行。

## 2. 闭包的组成与追踪方向

```text
全DATA实体/constituent索引与类型图
  → 唯一产品链 + SDR / 具体representation + context
  → shape → shell → face → bounds → loop → OE → EC → vertex / LINE
  → PLANE → placement → point / directions
  → units → typed measure / dimensions / base unit（可选uncertainty）
  → 按角色查反向引用、同型实例集合、USEDIN/USEROF和继承约束
  → 局部WHERE / UNIQUE / INVERSE + 全部47条RULE的适用性
  → 实际触发函数及其分支
```

不是只沿上述箭头从shape往下读一次。每个字段都关联“原记录、声明origin、类型/聚集/optional/derived、来源定义及规则”；每次反向查询都针对完整输入范围，不只针对已创建的native对象。无法完整捕获记录/类型或达到资源界限时拒绝该候选，不伪造空集合证明。

| 闭包材料 | 本附件中的责任 | 不能替代的检查 |
|---|---|---|
| [全47条RULE矩阵](ap203e2-planar-global-rules.md) | 每条规则的原集合、谓词/量词、必查/不触发依据、函数与反例 | 不用“无装配”一句话替全部规则判定 |
| [产品、单位与context闭包](ap203e2-planar-context-closure.md) | 精确属性、SELECT/complex、反向约束、单位/维数及相关函数 | 不照merged放宽字段；不把数学等价当源complex合法 |
| [几何与拓扑闭包](ap203e2-planar-geometry-closure.md) | 所有适用祖先、局部WHERE、derived、环/shape及函数 | 不只核最末子型；plain-FS在本profile明确未准入，不能把AF证明冒用到普通FS |
| [数学真值与63条主用例](p0-planar-cases.md) | 定义几何、方向、区域、单位等价、共享身份及目标验收真值 | schema通过不证明SMLib构建、查询、清理或发布正确 |

## 3. 必查与不触发怎样裁决

**先证范围，再作不触发判定。** 禁止类型、未知complex成员、第二产品/context或孤立额外记录出现时，不能继续沿“类型不存在”的旧证明放行。

| 结论 | 所需证据 | 示例 |
|---|---|---|
| 必查 | 源集合非空，或祖先使具体实例进入规则集合；保留原谓词、数量与函数语义 | SDR进入property_definition_representation；EDGE_LOOP进入LOOP；普通RI进入representation_item |
| 空集合不触发 | 全DATA类型检查证明FOR集合或实际循环集合为空；计入所有继承类型和complex成员 | 没有classification assignment、PMI或字体记录；不能只按最末实体名匹配 |
| 前件为False／筛选为空 | 集合非空，但已检查profile角色值或类型使实际触发条件为False | PD context固定`part definition`，排除`assembly definition`等分支；category只可为`part` |
| schema计算得到规定的UNKNOWN | 已实现其语言语义；继续按外层QUERY/逻辑组合求最终值，而非提前当通过或失败 | 普通PDS名使surface-condition函数返回UNKNOWN，但外层失败QUERY不收它 |
| 实现无法判定／证据不足 | 字段、类型、引用、数值或计算语义未能证明 | 这是项目未判定，不能借schema的UNKNOWN处理掩盖；无本次模型 |

`QUERY`只收谓词为True的元素。因此`NOT UNKNOWN`仍不会进入失败集合；若外层为“失败集合长度等于0”，最终可明确得到True。这是可证明的表达式结果，区别于“实现没算所以按通过”。语言依据见[EXPRESS Language Foundation的QUERY说明](https://www.expresslang.org/learn/tutorial/express-advanced-03-global-constraints)，本profile的实际原式和控制见全局矩阵。

47条全局RULE的静态分类为**5条始终必查、1条条件必查、41条有范围守卫的不触发证明**。5条是APD存在、compatible_dimension、surface-condition关联、RI subtype互斥、依赖实例化；GUUNC存在时再检查consistent_uncertainty。不触发项仍要先验证封闭清单及其谓词，不是把41条代码直接关闭。该计数不包含实体/祖先/TYPE的局部约束。

该语言说明不替代完整ISO 10303-11/21规则，也不允许把所有未知字段当UNKNOWN。类型/必需值/引用必须先满足合同，未实现分支必须准确Unsupported。

## 4. 必须做的反向与跨实例检查

| 检查 | 本profile的确切责任 |
|---|---|
| 根与产品关联 | PDS→PD、SDR→PDS/representation，以及APD→AC、category→PRODUCT完整唯一关联；不从任意solid倒猜主根 |
| 实例集合和继承 | 构成类型图后确定每个RULE、UNIQUE和INVERSE的真实集合；子型会进入祖先集合 |
| AC.context_elements | 真正由product/definition context的frame_of_reference形成，满足inverse基数；APD不是该context_element子型，不能充数 |
| REPRESENTATION_CONTEXT.representations_in_context | 按representation.context_of_items形成，满足inverse非空；不增加序列化槽 |
| using_representations／using_items／item_in_context | 点、方向、曲线、面、loop/边/壳必须能经实际使用链到该表示；全图反向索引与防循环/限额不能省略 |
| 属性附属关系 | 本profile不允许ID_ATTRIBUTE/NAME_ATTRIBUTE/DESCRIPTION_ATTRIBUTE等源记录；仍必须全DATA证明其不存在，相应derived可为`?`，不能补成显式字段 |
| 唯一键 | formation的`(id,of_product)`、PDS的definition等按固定原文检查；PRODUCT.id在本e2无同一UNIQUE，仍不能据它合并记录 |
| uncertainty与unit | 同一GUUNC内name唯一；typed value、量纲、换算链、环/数域和消费单位；不要求跨独立context名字唯一 |
| LOOP依赖实例化 | 每个EDGE_LOOP须通过允许的bound被使用；不能把LOOP的FOR集合误写为空或只数最末实体名 |

schema函数可能构造临时direction/vector等值；这些是解释所需数学对象，不是偷偷增加外部记录或SMLib拓扑。源临时计算与native保形规范化的日志边界仍分别记录。

## 5. 与SMLib构建之间的门槛

完成源闭包后，仍必须独立完成源线/面、端点/环、有向使用、孔/闭合性、无自交、shape角色的几何检查，再按[已有构建契约](p0-smlib-source-audit.md)材料化。以下事项不由47条RULE自动保证：

- LINE端点参数与same_sense、四层方向、共享源身份和实际邻接；不能按坐标焊接。
- 平面/孔区域、有限window、UV lift误差与SAME owner、构建/validator实际tol及传播。
- owning快照、失败清理、取消/成功提交点，以及应用revision/token接入。
- 数学真值、精确查询、截面和后续建模资格；没有执行的内容继续NotRun。

同样，native AssertValid/体积正确也不能覆盖源字段、规则或未知必需内容。首profile关闭修复，合法的等价参数映射单独记录；不得改名、移点、删边、补面或放宽质量配置使候选通过。

## 6. 可执行前的验收控制设计

下面编号属于本profile的规则控制，**全部Defined/NotRun**，是63条主用例的细化，不当成已有fixture或新增通过数量。

关联主用例ID只表示所检查语义相同，不把超出本profile的父级正例自动变成此处正例。例如多产品/多context、其他Part21映射和普通FACE_SURFACE仍可属于后续目标；在本profile中必须先按明确包络拒绝，不能同时宣称接纳。

| ID | 配对控制 | 预期与主用例关联 |
|---|---|---|
| C203-01 | 每种允许角色的完整产品/表示链；缺PDS/SDR或错指对象 | 正确选根／对应属性拒绝，P00/R01/R03 |
| C203-02 | 完整schema名/OID与不匹配OID/裸名/临时别名 | 精确profile绑定；未绑定Unsupported，X06/X08 |
| C203-03 | APD原文字面量、错误后缀/大小写、缺记录/多记录 | schema量词和项目恰一主链分别记录，P26/R20 |
| C203-04 | 普通PDS名、六个特殊名、surface-condition函数UNKNOWN的外层QUERY | 普通名的规定计算不误拒；特殊用途Unsupported，不改名，P26 |
| C203-05 | 普通representation名与两种特殊表示名 | 不把非空FOR集合当空；准确分支或范围拒绝，P26 |
| C203-06 | 正常part-definition、改成assembly/collection/其他专名 | 原名称保留；profile guard失败，不沿已失效的“不触发”证明放行，X07 |
| C203-07 | category缺席／单part，改成其他category或增加额外产品 | category不是无条件必需；越界Unsupported；不跨产品合并，P19/R17 |
| C203-08 | 普通types与受支持旧subtype、再增加未登记complex成员 | 执行对应祖先/子型合同；多成员不被丢弃，R03/X06 |
| C203-09 | formation/PDS唯一键重复、PRODUCT重label | 按所声明UNIQUE与身份检查；不按label合并，P19/R03 |
| C203-10 | inverse非空、缺使用者、额外attribute记录 | 完整反向索引及范围门槛；derived不增加槽，R02/X07 |
| C203-11 | RI嵌套使用链及共享点，增加无表示使用的孤立RI | 核真实using chain；不伪造全局context或忽略孤立项，P18/R01 |
| C203-12 | 正常bound→EDGE_LOOP，增加孤立loop/只有闭合引用圈 | 检查LOOP依赖实例化；区分schema失败、profile孤立项和资源拒绝，R05/E07 |
| C203-13 | 类型集合包含祖先，对比只按最末类型计数的错误checker | SDR/PDR、OE/EDGE、LOOP、RI等都进入正确集合，E08 |
| C203-14 | SI/转换length，错用派生槽、typed value/量纲 | 按N2635布局与数值解释，P07/P08/P25/R14 |
| C203-15 | 有/无GUUNC；同一GUUNC重name、零负值、单位不同 | 完整uncertainty分支、Q不变，不将缺失与非法混同，P17/R15/R19 |
| C203-16 | context和单位集合重排；第二context/冲突长度单位 | SET无序；项目单context/计数限制明确，P09/P20/R13 |
| C203-17 | 各方向/孔/共享边使用ADVANCED_FACE；改为普通FACE_SURFACE作能力边界控制 | 准入类型全部WHERE及祖先检查；普通FS在本profile为Unsupported，不借相同槽数放行，P01—P05/P12—P14 |
| C203-18 | MSB盒体和相同几何SBSM闭表面 | 类型、范围和材料角色保持；6000体积资格只属实体，P04/P05 |
| C203-19 | 不触发证明成立的封闭记录集，加入style/PMI/映射/assembly/未知关系 | proof前提失效即Unsupported；不丢新记录继续，X01—X07 |
| C203-20 | 极端引用深度/环、截止/取消、未支持函数分支 | 准确资源/取消/能力结果；不把未计算当schema空集或UNKNOWN，E06/E07/E08 |

每条控制还须绑定：具体输入及hash、变更的唯一条件、profile版本、源规则/函数、预期层与原因、数值真值及报告字段。源约束控制不需要为了“凑BRep”调用native；端到端控制才检查整个请求全成全败。

后续具体化已写入[样件总册](ap203e2-sample-design.md)：13个正例与36个反例逐项给出父例、输入记录／增量、独立真值、首诊断及同因后果；PF/B-N/C203等编号为来源关联，不与样件数量重复累加。它们仍是Markdown设计，尚未生成实际STEP或执行程序。

## 7. 完成边界

本轮交付的是固定N2635上的输入范围、实体/继承/函数闭包、全47条规则适用性和配对验收设计。完整性以三份明细中每个入口及其依赖的可追踪覆盖为依据，不以“已列了很多实体”代替。

未授予的资格保持明确：ISO最终出版制品逐字身份、Part21实现、EXPRESS规则执行、SMLib端到端导入、所有63条主用例及本profile细分控制的实际通过。它们不是静态清单中的“未知规则可以跳过”，而是后续相应阶段的执行门槛。已确认总体规格0.1字节不变；本轮没有写loader、healer、规则校验器、测试程序或STEP样件。
