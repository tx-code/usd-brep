# AP203e2 首个平面／盒体 profile：全局 RULE 覆盖与依赖

版本：P0-G203-0.1，2026-10-15。配套 [schema 候选表](p0-schema-profiles.md)及[首片契约](p0-planar-slice.md)。**这是静态原文审计；所有下述验收控制均 Defined/NotRun。未编写解析器、规则执行器、测试或 STEP 样件。**

## 1. 来源及判断范围

唯一规则依据为已固定的 [AP203e2 MBx-IF longform 文件](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp)。SHA256 为 `c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`。文件自述 N2635、ISO/TS 10303-403、2009-09-10，来源身份和限制见 [AP203 溯源](p0-ap203-provenance.md)。本文所有 `S:L` 是该文件的一基行号；RULE 区完整覆盖 **47 条，S:12358—13125**。不将这些结论套用到 AP203e1、AP214 或 AP242，也不声称完整 ISO 最终版/AP 符合性。

本表的“不触发”是**先证明输入满足以下狭窄 profile，再对全文件实例总体证明**的结果。不是从 face 正向遍历没看到某类型就忽略。一个 DATA 实例的 TYPEOF 含其全部实际 subtype/complex constituents 和 supertypes；`FOR(supertype)` 含其 subtype 实例，不能仅统计名字等于该 supertype 的记录。

### 1.1 不触发证明所依赖的 profile 前提

| 前提 | 首片约束；均属项目能力边界，除非另标 schema 规则 | 对证明的作用 |
|---|---|---|
| P1 单一产品链 | 全文件恰有一个 PRODUCT、一个 formation、一个 PRODUCT_DEFINITION、一个 PDS、一个 SDR；一个 AC，同时被 PC、PD context、APD 使用；无第二产品根 | 不以选根缩小全局量词；多根留后续能力 |
| P2 context 角色 | PD context.name=`part definition`，life_cycle_stage=`design`；不接纳 context association、PD relationship、occurrence、configuration、make-from、复合材料定义 | 排除 alternative/constraint/occurrence/assembly/collection 条件分支；不擅自改写源字符串 |
| P3 分类 | 可完全缺席；或恰有一个 PRODUCT_RELATED_PRODUCT_CATEGORY，name=`part`、products={唯一 PRODUCT}。不接纳其他 category 名、独立 PRODUCT_CATEGORY 或 category relationship | `part` 并非普通 part definition 的全局 RULE 必需条件；排除 solution、breakdown、requirement、ply 等反向触发 |
| P4 表示 | 唯一直接 ABSR→MSB→CLOSED_SHELL，或 MSSR→SBSM→OPEN_SHELL/CLOSED_SHELL；items 仅该形状和合法辅助 AXIS2_PLACEMENT_3D；REP.name 不得为 `centre of mass`、`treatment result`，PDS.name 不得为 §3.3 六个表面属性专名；其他名原样保留 | 专名表示其他语义，首片返回 UnsupportedProfile，不改名后接受；不接纳 mapped、assembly、style、annotation、PMI、tolerance、材料属性或其他表示包装 |
| P5 几何与拓扑 | 仅该切片声明的 LINE/PLANE、点/方向/向量/placement、VP/EC/OE/EDGE_LOOP、bound、face、shell；无孤立未归属记录、未知 subtype 或額外 complex facet | 证明几何相关 RULE 的实际总体；EDGE_LOOP 的 LOOP 父类型不能漏计 |
| P6 context/单位 | 一套三维 geometric + global unit context，可组合 global uncertainty；仅已列的 SI/length conversion 链、具名物理类和 uncertainty；无 qualified item 或 uncertainty-assigned representation | uncertainty 总体可为空，但其同名约束在存在时必查；无“没用角度就不检查 unit 字段”捷径 |
| P7 其余 DATA | 每个记录须匹配主契约完整白名单及合法 complex 签名；其余类型或多出的语义 facet 返回 UnsupportedProfile，不执行转换后丢弃 | 排除集合为空的必要证据；不扩大总体基线中未来的元数据支持范围 |

P1—P7 是首个实验 profile 的限制，不是合法 AP203e2 文件的全部合法形式。合法但越界与已证实违反源约束应分别报告；未知表达式/未实现检查不能伪报为 InvalidSource 或通过。若 profile 扩大一个实体/分类/关系，其相关“不触发”证明立即失效，必须重开闭包。

### 1.2 三种状态及三值语义

- **M 必查**：FOR 总体在首片非空，或规则适用的条件可能出现。按原量词/函数求值；可以通过证明恒真简化，但必须记录证明。
- **N 不触发**：列出为空的 FOR/筛选集合或未进入的循环；其证明由 P1—P7 保证。运行时仍须核实该前提，不能直接禁用全条规则。
- **C 条件必查**：允许的可选记录存在时必须求值；不存在时才是空集通过。

EXPRESS `QUERY` 仅保留谓词为 TRUE 的元素；UNKNOWN 不等于 FALSE，也不等于“实现没做”。语言维护方的 [QUERY 说明](https://www.expresslang.org/learn/tutorial/express-advanced-03-global-constraints)明确了这点。本文只在 `surface_condition_correlation` 的完整 CASE 已知返回 UNKNOWN 时使用这一语言结果；不能以同样措辞掩盖执行器/证明器的未判定。

## 2. 全部 47 条 RULE 的逐条分类

下表 FOR/谓词以源文件为准；为使量词可读，`∀ x∈A: count(...)` 表示源 `SIZEOF(QUERY(A | 违反条件))=0`，不是修改规则。空集分支也列出将使其失效的单一变化。表中 `—` 表示没有用户自定义 FUNCTION 调用；QUERY/USEDIN/TYPEOF 等语言内建见 §4。

| # / S 起止 | RULE 名称 | 首片状态；总体/触发与原量词 | FUNCTION 依赖、反例或失效条件 |
|---|---|---|---|
| G01 / 12358—12369 | alternative_solution_requires_solution_definition | **N**。从所有 formation 中筛其 product 恰有一个 `alternative solution` PRPC；P3 使筛集空。触发时每个该 formation 恰有一个 context.name=`alternative definition` 的 PD | —；把唯一 `part` 改为 `alternative solution` 即不再空，不能继续假定普通 part definition 足够 |
| G02 / 12371—12379 | application_protocol_definition_required | **M**。存在至少一个 AC，其反向 APD 至少一个，且 APD.application_interpreted_model_schema_name 精确等于 `ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies` | —；这不是“每个 AC”，也不是“恰有一个 APD”。P1 单 AC 收敛为该 AC 必须有匹配 APD；漏 APD 或带 `_mim_lf` 字面量均无法满足 |
| G03 / 12381—12396 | breakdown_element_requires_product_definition | **N**。∀ formation：恰一个 `functionality` category 时须至少一个 `functional definition` PD；恰一个 `conceptual design` 时须至少一个 `conceptual definition` PD；P3 两条件均假 | —；改 category 为任一该名，仍仅有 part definition 就违反对应 WR |
| G04 / 12398—12408 | compatible_dimension | **M**。对全体 CARTESIAN_POINT/DIRECTION 与每个 GRC：若 item_in_context 为真，则坐标/分量的 HIINDEX 必须等于 context.dimension | `item_in_context`；点只经 placement/plane/face 进入表示也适用。把任一三维方向改为二维应失败，不能只查 REP.items 的直接点 |
| G05 / 12410—12455 | component_class_for_assembly_select | **N**。外层 composite_assembly_sequence_definition 为空；即使 optional PRPC 非空也不进入外层。触发时每个序列至少一个匹配 NAUO，且每个匹配件恰一个指定五类 category | —；加入 composite sequence/NAUO 后重开；不能以所有 FOR 集合均空来解释 |
| G06 / 12457—12473 | consistent_uncertainty | **C**。WR1：每个 GUUNC 内不得有两个 uncertainty.name 相同；WR2：每个 UAR 同名约束；WR3：每个 QRI 的 uncertainty qualifier.measure_name 唯一。P6 只可能 WR1 非空 | —；两个不同 #id、同名 uncertainty 也违反 WR1；一条复用同名物理单位不等于 uncertainty 复名 |
| G07 / 12475—12486 | constraint_definition_requires_constraint_category | **N**。从 PD 筛 context.name=`design constraint definition`；P2 使其空。触发时其 product 至少一个 `requirement` category | —；改 context 名会激活；life_cycle_stage=`design` 不会替代该 category |
| G08 / 12488—12497 | design_constraint_requires_product_definition | **N**。∀ formation：product 有任意 `requirement` category 时须至少一个 `design constraint definition` PD；P3 条件假 | —；新增 requirement 分类后不能忽略反向 PD 需求 |
| G09 / 12499—12516 | draughting_model_items_constraint | **N**。DRAUGHTING_MODEL 总体空；非空时禁止普通 annotation_occurrence 冒充 draughting subtype，亦限制 draughting_callout 的类型 | —；加入 draughting representation 即 UnsupportedProfile，不能仅删其 items |
| G10 / 12518—12523 | external_version_assignments_are_valid | **N**。APPLIED_EXTERNAL_IDENTIFICATION_ASSIGNMENT 总体空；非空须逐项 external_version_assignment_is_valid | `external_version_assignment_is_valid`（不进入首片执行闭包）；增加 assignment 后重开其函数闭包 |
| G11 / 12525—12535 | material_for_coating_layer | **N**。SHAPE_ASPECT 总体空；触发于 name=`coating layer`，须恰一个反向 material designation | —；加入 coating aspect 及材料语义即越界 |
| G12 / 12537—12549 | plib_property_reference_requires_name_scope | **N**。EXTERNALLY_DEFINED_GENERAL_PROPERTY 总体空；原筛 KNOWN_SOURCE，要求一个满足 name scope/class/known source 条件的反向关系 | —；外部属性不在几何链也不能静默保留后宣称本 profile |
| G13 / 12551—12562 | plib_property_reference_requires_version | **N**。同上主体总体空；原 QUERY 还含 KNOWN_SOURCE.NAME 字符串表达式，本文不替原文件修成意图版本 | —；扩大到 PLIB 时须逐字审计这个实现稿表达式及来源差异，不能照规则名猜语义 |
| G14 / 12564—12607 | ply_reference | **N**。PLY_LAMINATE_SEQUENCE_DEFINITION 外层空；触发时每序列至少一个 NAUO，每个匹配件恰一个 `ply` category | —；optional `part` PRPC 不使外层非空 |
| G15 / 12609—12672 | ply_stock_material_select | **N**。PRPC 可非空，但其 name 均 `part`，源外层 IF(name=`ply`) 从不进入，dkuhr 保持 TRUE | —；改为 `ply` 会进入 make-from/stock-material 循环；无 make-from 会使计数不为1 |
| G16 / 12674—12683 | product_concept_feature_requires_category | **N**。PRODUCT_CONCEPT_FEATURE 总体空；适用的非 inclusion/conditional subtype 须恰一个 specification category member assignment | —；增加概念特征触发，不由 shape 路径排除 |
| G17 / 12685—12692 | product_definition_replacement_requires_effectivity_assignment | **N**。PRODUCT_DEFINITION_RELATIONSHIP 总体空；名 `definition replacement` 时须至少一条反向 effectivity assignment | —；增加替代关系即重开，不能只看两端几何相同 |
| G18 / 12694—12712 | restrict_alternative_definition | **N**。PD 筛 context.name=`alternative definition` 为空；非空时约束一个 solution alternative relationship、PD.name 枚举及 supplier organization assignment | —；改变 context 名会激活三项；不偷引 organization 作为普通零件必需项 |
| G19 / 12714—12724 | restrict_assembly_category | **N**。PD 存在其 frame_of_reference.name=`assembly definition` 的反向 PD_CONTEXT_ASSOCIATION 才筛入；该 association 总体空。触发时 product 类别含 `assembly` | `categories_of_product`（不进入执行闭包）；不是直接测试 PD.frame_of_reference.name |
| G20 / 12726—12736 | restrict_centre_of_mass_representation | **N**（前件假）。representation 非空，但 P4 排除 name=`centre of mass`；触发时 items 恰一个且该 item 是 POINT、名 `centre point` | —；把盒体 ABSR.name 改为该值即 UnsupportedProfile；按其既有 BRep items 求原规则也会违反，不能把 name 当无语义元数据忽略 |
| G21 / 12738—12743 | restrict_classification_assignments | **N**。APPLIED_CLASSIFICATION_ASSIGNMENT 总体空；非空逐项 class_assignment_is_valid | `class_assignment_is_valid`（未入执行闭包）；不得与 P3 的 PRODUCT_RELATED_PRODUCT_CATEGORY 混为同一类型 |
| G22 / 12745—12755 | restrict_collection_category | **N**。通过反向 PD_CONTEXT_ASSOCIATION.frame_of_reference.name=`collection definition` 筛 PD；association 总体空；触发时 product 类别含 `collection` | `categories_of_product`（未入执行闭包）；只有 PD context 名不会满足源筛选路径 |
| G23 / 12757—12770 | restrict_concept_feature_operator | **N**。CONCEPT_FEATURE_OPERATOR 总体空；非空时约束五个 operator 名、implication 的相关 inclusion 用法及 not 的同一实例关系 | —；概念运算内容越界 |
| G24 / 12772—12789 | restrict_configuration_design_for_class_breakdown_association | **N**。CONFIGURATION_DESIGN 总体空；非空且名 functionality/realization 时约束 design 的 PD 类型/context 与 product_class | —；不能仅抛弃 configuration 外壳 |
| G25 / 12791—12798 | restrict_configuration_design_for_design_constraint | **N**。CONFIGURATION_DESIGN 总体空；名 `design constraint usage` 时 design 须为约束定义 PD | —；同上 |
| G26 / 12800—12807 | restrict_group_relationship_for_classification_hierarchy | **N**。GROUP_RELATIONSHIP 总体空；名 `class hierarchy` 时两端均 CLASS | —；group 不是 product category，不能按名字互换 |
| G27 / 12809—12816 | restrict_group_relationship_for_specification_category | **N**。GROUP_RELATIONSHIP 总体空；名 `specification category hierarchy` 时两端均 PRODUCT_CONCEPT_FEATURE_CATEGORY | —；增加关系重开 |
| G28 / 12818—12829 | restrict_language_assignment_per_attribute | **N**。ATTRIBUTE_LANGUAGE_ASSIGNMENT 总体空；非空时每 item 的同 attribute_name、同 assigned_class 身份的 assignment 不得多于一 | —；字符串语言 assignment 也不在首片白名单 |
| G29 / 12831—12869 | restrict_part_occurrence | **N**。PD 筛 context.name=`part occurrence` 为空。非空时有 WR1—WR6：四类 PD.name、定义/配置关联、usage/occurrence 存在性、selected representation、quantity 和 specified higher usage 条件 | `valid_selected_instance_representation` 仅 WR4 且 selected instance 分支；本片不执行其闭包 |
| G30 / 12871—12880 | restrict_part_occurrence_category | **N**。同一 part_occurrences 筛集空；触发时至少一个 PRPC.name∈{part,raw material,tool} | —；这条不能据规则名变成所有 PRODUCT 必须分类 `part` |
| G31 / 12882—12890 | restrict_product_definitions_for_base_element | **N**。PD_RELATIONSHIP 总体空；`solution alternative definition` 的两端 context 受限 | —；增加关系重开 |
| G32 / 12892—12903 | restrict_product_definitions_for_collection | **N**。PD_RELATIONSHIP 总体空；`collection membership` 要求 part definition→part occurrence 及 collection definition 类型 association | —；不是所有产品关系都能只验证两端指针存在 |
| G33 / 12905—12912 | restrict_product_definitions_for_definition_usage | **N**。PD_RELATIONSHIP 总体空；`definition usage` 要求 part definition→part occurrence | —；增加 occurrence/usage 越界 |
| G34 / 12914—12922 | restrict_product_definitions_for_design_constraint_association | **N**。PD_RELATIONSHIP 总体空；`design constraint association` 两端须为约束定义及三类替代/功能/概念定义之一 | —；增加关系重开 |
| G35 / 12924—12931 | restrict_product_definitions_for_part_definition_relationship | **N**。PD_RELATIONSHIP 总体空；`geometrical relationship` 或 `definition replacement` 两端均须 part definition | —；关系越界，不靠几何内容猜等价 |
| G36 / 12933—12938 | restrict_representation_for_surface_condition | **M**。FOR PROPERTY_DEFINITION_REPRESENTATION **含唯一 SDR**；按 `SIZEOF(QUERY(pdr \| NOT surface_condition_correlation(...)))=0` 求值。P4 排除六个专名后，函数预定 UNKNOWN→不入失败 QUERY→最终 TRUE | `surface_condition_correlation`。不能谎报 FOR 为空；若扩展六个专名则须与 rep.name 同名，见 §3.3 |
| G37 / 12940—12955 | restrict_treatment_result | **N**（前件假）。representation 非空，但 P4 排除 name=`treatment result`；触发时 items≤2、全部为 DESCRIPTIVE_REPRESENTATION_ITEM、至少一个名 result、至多一个名 purpose | —；仅改 ABSR.name 为该值即 UnsupportedProfile；保留现有 BRep items 求原规则亦假，别静默抹平名称 |
| G38 / 12957—12965 | selected_instance_usage_requires_representation | **N**。ASSEMBLY_COMPONENT_USAGE 总体空；筛 name=`selected instance usage` 后调用 representation 验证 | `valid_selected_instance_representation`（未入执行闭包）；与 G29 的 FOR 不同 |
| G39 / 12967—12977 | solution_definition_requires_solution_category | **N**。PD 筛 context.name=`alternative definition` 为空；触发时 product 至少一个 `alternative solution` category | —；P2/P3 成对限制，不能只删类别使替代定义漏验 |
| G40 / 12979—13025 | stock_material_reference | **N**。PERCENTAGE_PLY_DEFINITION 外层空；触发时每定义至少一个 make-from，且每匹配件恰一个五类 stock category | —；PRPC 非空并不改变外层为空 |
| G41 / 13027—13031 | styled_curve | **N**。STYLED_ITEM 总体空；若其 item 为 CURVE，则恰一个 style assignment 中含 curve style | —；外观元数据是后续能力，不在本片假设下静默保留 |
| G42 / 13033—13037 | subtype_exclusiveness_geometric_tolerance | **N**。GEOMETRIC_TOLERANCE 总体空；非空时指定15种 tolerance subtype 最多一种 | `type_check_function` criterion=3；函数已因 G43 纳入，但本分支不运行 |
| G43 / 13039—13046 | subtype_exclusiveness_representation_item | **M**。所有 REP_ITEM（包括间接点/边/环/面）逐项；其 TYPEOF 与 {MEASURE_REPRESENTATION_ITEM,VALUE_REPRESENTATION_ITEM,COMPOUND_REPRESENTATION_ITEM} 交集≤1 | `type_check_function` criterion=3。允许的首片精确签名均交集0，因此已证明恒真，但签名检查不可省；给 LINE 加额外语义 facet 会破坏前提 |
| G44 / 13048—13052 | subtype_mandatory_geometric_tolerance | **N**。GEOMETRIC_TOLERANCE 总体空；非空时15种 tolerance subtype 至少一种 | `type_check_function` criterion=0；不能只做 G42 的至多1 |
| G45 / 13054—13059 | text_font_usage | **N**。EXTERNALLY_DEFINED_TEXT_FONT、PRE_DEFINED_TEXT_FONT 均空；非空时每 font 至少被一个 TEXT_LITERAL.FONT 使用 | —；孤立 font 不应被未经证明地忽略 |
| G46 / 13061—13106 | thickness_laminate_table_component_select | **N**。THICKNESS_LAMINATE_DEFINITION 外层空；触发时每定义恰一个合格 NAUO，每匹配件恰一个指定三类 category | —；不是 G05/G14 的至少一个 NAUO 计数 |
| G47 / 13108—13125 | validate_dependently_instantiable_entity_data_types | **M**。FOR 长列表中 **LOOP 总体含所有 EDGE_LOOP**；其余列举族在精确白名单下为空。按所有这些实际实例的 TYPEOF 并集启动依赖实例验证 | `dependently_instantiated`；FACE_BOUND→EDGE_LOOP 是允许的直接外部引用见 §3.4；孤立 LOOP 不能因不是 REP.items 直接成员而略过 |

G42—G44 编号以本表为准：三条相邻规则依次是 geometric tolerance exclusiveness、representation item exclusiveness、geometric tolerance mandatory；不要把表行编号误当源 WHERE 标签。

## 3. 非空规则的完整数据依赖

### 3.1 APD 不是从 HEADER 名字拼出来

G02 的数据依赖是 AC 总体→`USEDIN(AC, APPLICATION_PROTOCOL_DEFINITION.APPLICATION)`→每个 APD 的 schema-name 字符串；唯一 AC 的 PC/PD context 引用必须指向同一实例。HEADER/schema 文件身份是独立校验，不会替 APD 记录“补值”。规则字符串不带 `_mim_lf`；其比较对象也不是 FILE_SCHEMA 的标识列表。P1 可以另限制恰一个 APD，以使首片图唯一，但这属于项目约束，不能声称源规则要求 exactly one。S:12371—12379。

### 3.2 点与方向：全文件反向可达性

G04 调用 [item_in_context](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:14923)。函数先检查 item 的直接 REPRESENTATION.ITEMS 使用与 context.representations_in_context 交集；未命中时沿全部 `USEDIN(item,'')` 中的 REP_ITEM 递归。故其闭包包括：

`点/方向 → vector / placement / line / plane / VP / EC → OE / bound / face / loop / shell / solid / SBSM → representation.items → representation.context_of_items`。

上述箭头表示源引用的反向可达关系，不声称每个相邻类型有直接属性；具体路径由实际引用决定。检查必须使用所有 DATA 实例建立的反向角色索引，按真实类型判断中间项；不能只走点→LINE，漏掉 PLANE placement 的 location/axis/ref_direction。首片几何 context 一律三维，因此每个可达 CARTESIAN_POINT、DIRECTION 的 LIST 长度均须3；但长度3还不是所有局部几何约束的全部内容。

函数原文没有通用 visited 集。未来实现需用等价可达性算法、循环检测及资源限制，不能照抄递归造成未终止；有界实现与原判定的等价性是后续待运行控制。文档阶段尚未实现它。context 的 INVERSE `representations_in_context` 是从 `representation.context_of_items` 派生，不能信任一个不存在于 DATA 槽中的“缓存值”。S:10021—10027、14923—14953。

### 3.3 SDR 实例确实落入表面属性 RULE

SDR 是 PROPERTY_DEFINITION_REPRESENTATION subtype（S:10415），所以 G36 的 FOR 集合非空。其函数 [surface_condition_correlation](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:15570)仅依赖 PDS/property_definition.name 与 representation.name：

| PDS.name | 函数结果及 G36 的结果 |
|---|---|
| `visual appearance`、`tactile appearance`、`contact ratio`、`hardness`、`treatment result`、`surface texture` 任一 | 比较该 name 与 REP.name 是否相等；不相等得到 FALSE，NOT 后为 TRUE，QUERY 有该 SDR，规则失败 |
| 其他名字，包括普通空串 | 函数按 OTHERWISE 返回 UNKNOWN；NOT 后仍 UNKNOWN，QUERY 不收该 SDR，SIZEOF=0，规则为 TRUE |

首片 P4 显式排除六个专名，其余普通 PDS/REP 名原样保留；仍应记录 G36 的完整求值证明，不能把它误写为 FOR 空集。**不可统一规定任意 UNKNOWN 即失败，也不可把未实现规则的未知当作这个预定分支。** 表中六个专名分支是扩展能力时的原文语义对照，本片输入这些名返回 UnsupportedProfile。例如 PDS.name=`treatment result` 且 REP.name 同名时 G36 自己为真，但 G37 会因精确 BRep items 类型而失败；不能据一条为真宣称整体源合法。来源函数 S:15570—15577；QUERY 语言依据见 §1.2。

### 3.4 依赖实例 RULE：只有 LOOP 分支非空

G47 原文长 FOR 列表不是全部非空，也不是全部为空。EDGE_LOOP 同时继承 LOOP 和 PATH（S:5319），其 TYPEOF 将 EDGE_LOOP、LOOP、PATH、TOPOLOGICAL_REPRESENTATION_ITEM、REPRESENTATION_ITEM 等实际父类型放入集合。首片没有 BOUNDED_CURVE/BOUNDED_SURFACE：LINE 不是 BOUNDED_CURVE，PLANE 不是 BOUNDED_SURFACE；AXIS2_PLACEMENT_3D 不是 AUXILIARY_GEOMETRIC_REPRESENTATION_ITEM。仅按名称“几何辅助”推测 subtype 会错。

[dependently_instantiated](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:14043)的闭包是自身递归和语言内建：对每个输入实例找全部反向引用；若某引用者的 TYPEOF **不是**输入类型集合（加 GENERIC）的子集，则该输入有允许的依赖实例路径；若是子集，则递归沿引用者找外部锚点，previous_in_chain 防止只靠闭环自证。每个输入均需成立，而非只找一个合法 LOOP。

P5 保证每个 EDGE_LOOP 被 FACE_BOUND/FACE_OUTER_BOUND.bound 使用；FACE_BOUND 的 TYPEOF 不在前述输入类型集合的子集内，故该直接引用就是证明锚点。该证明只要求至少一个合格使用，**不把函数加强成每 LOOP 恰一个 owner**；构建层是否允许/复制共享 loop 是另一项语义/所有权约束。孤立 LOOP 的反向使用为空，函数返回 FALSE；仅受限实例相互引用的闭环没有外部锚点也不得通过。不能以 `SmAssertValid` 代替源依赖规则。

为使“其余为空”可审查，G47 的 **59 个 FOR 类型**在首片下按下面完整分区登记。每个空集证明都要求检查实际 subtype/complex 签名；例如禁止 standalone BOUNDED_CURVE 还不够，B_SPLINE_CURVE subtype 也会进入该总体。

| FOR 类型分区 | 所有名字；无省略 | 首片实例总体 |
|---|---|---|
| 活跃1类 | loop | 所有 EDGE_LOOP；由 P5 的 bound 使用锚定 |
| 几何／拓扑／表示16类 | auxiliary_geometric_representation_item, bounded_curve, bounded_surface, cartesian_transformation_operator, connected_edge_set, edge_blended_solid, founded_item, oriented_open_shell, oriented_path, positioned_sketch, procedural_representation, procedural_representation_sequence, swept_area_solid, symbol_target, variational_representation_item, view_volume | 精确白名单均排除，包括其额外 complex facets，故全空 |
| 表达式20类 | binary_numeric_expression, boolean_expression, comparison_expression, expression, generic_expression, generic_variable, indirectly_selected_elements, interval_expression, literal_number, multiple_arity_boolean_expression, multiple_arity_generic_expression, multiple_arity_numeric_expression, numeric_expression, simple_boolean_expression, simple_generic_expression, simple_numeric_expression, unary_generic_expression, unary_numeric_expression, user_selected_elements, one_direction_repeat_factor | 全空；不从相似名字推断几何 scalar 是这些 ENTITY |
| 管理／特征／其余22类 | action_method_role, annotation_text, attribute_value_role, concept_feature_relationship, concept_feature_relationship_with_condition, document_usage_constraint, effectivity_context_role, event_occurrence_role, explicit_procedural_representation_item_relationship, local_time, modified_solid_with_placed_configuration, product_definition_context_role, product_definition_effectivity, runout_zone_orientation, solid_with_depression, solid_with_hole, solid_with_pocket, solid_with_protrusion, solid_with_shape_element_pattern, solid_with_slot, tolerance_zone_form, two_direction_repeat_factor | 全空；PRODUCT_DEFINITION_CONTEXT 不是 PRODUCT_DEFINITION_CONTEXT_ROLE，MSB 不是这些 feature-solid subtype |

### 3.5 uncertainty 与 subtype 检查

G06 的 WR1 只需 GUUNC→uncertainty SET→每个 uncertainty.name；比较名字，不按 #id、数值、unit 或物理类归并。缺席 GUUNC 时 WR1 空集通过，但质量配置仍必需，不能用源 uncertainty 缺席来默选精度。WR2/WR3 的类型全部排除；只把同一 GUUNC 加入另一个 complex facet，不会自动得到 UAR/QRI 类型。S:12457—12473。

G43 的唯一自定义函数 [type_check_function](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:15629)在 criterion=3 时检查三个 subtype 集合与 TYPEOF 的交集基数≤1。首片允许签名没有这三个 facet，故结果为 TRUE。函数自带“不存在输入/空 sub_names/criterion 越界→UNKNOWN”分支，在这里由已解析实体、非空常量集合、固定3排除，不能省略参数有效性前提。SI/CONVERSION 的单位 complex 不是 REP_ITEM；不要把它们错误计入此 FOR。

## 4. 全局规则的函数与语言闭包

| 层 | 必需依赖 | 冻结边界 |
|---|---|---|
| 活跃用户函数 | item_in_context（S:14923）、surface_condition_correlation（15570）、type_check_function（15629）、dependently_instantiated（14043） | 四个函数的直接/递归调用已读；它们不再调用其他用户 FUNCTION。ENTITY 局部/派生函数闭包由几何及产品/context 附件补齐，不能据“四个”宣称整个 importer 只需四个函数 |
| 仅由不触发分支调用 | categories_of_product（13504），class_assignment_is_valid（13682），external_version_assignment_is_valid（14336），valid_selected_instance_representation（15913） | 首片不进入，故其内部传递调用不属于活跃执行闭包；扩大关联类型后必须展开。categories 的反向引用数据已核，用于说明 G19/G22 不靠直接 PD context 筛选 |
| 全体规则所需语言能力 | 精确 ENTITY/subtype/complex TYPEOF；USEDIN 可带具体角色或空角色；QUERY、SIZEOF、HIINDEX/LOINDEX、EXISTS；LIST/SET/BAG；值相等与实例身份相等；集合并/交/子集；三值逻辑；循环与递归 | 这是一套设计语义要求，不是要求首片实现通用 EXPRESS 解释器；可以专用规则，但必须保持该规则在本 profile 上的等价含义 |
| 全文件索引 | 文件实体身份/实际签名；按 supertype 的总体；全部显式引用及其源角色；表示的 context 反向索引；formation/product/PD/PRPC/APD 的反向角色 | 所有 DATA 段纳入同一已明确载体语义下的检查；不得只装载所选 face 的正向图再评价全局 RULE |

表内 47 条 RULE **没有** AP214 的 `global_length_and_angle_units_2d_or_3d`，也没有 AP203e1 修改副本的全局三单位规则；不能移植后声称这是本文件的 RULE。单位选择、有限正因子、单位物理类、合法 complex、context3D 等仍是已声明的首片/局部约束，必须检查；这与不存在某条全局 RULE 并不矛盾。

## 5. 必要反例与不触发控制清单

下述是文档用验收合同，尚未生成文件或运行。它们须与主用例目录关联，不能把行数当新增 fixtures 或已通过数量。

| 控制 | 一次变化及预期 | 要防止的误判 |
|---|---|---|
| G-C01 | 单 AC 有匹配 APD→G02 真；删除 APD / 加 `_mim_lf`→G02 假 | 用 HEADER 替 APD、把至少一个误成恰一个 |
| G-C02 | 两 AC、仅一个有匹配 APD，在原 RULE 下为真但本片 P1 越界 | 将全局量词错写成每 AC；不把本片拒绝伪报源 RULE 失败 |
| G-C03 | 间接被 plane placement 使用的点/方向少一个坐标→G04 假 | 只查 representation.items 直接成员 |
| G-C04 | 不同 ID、同 name 的两个 uncertainty→G06 WR1 假；完全无 GUUNC→该分支空集真 | 按 identity 判重复，或将缺失 uncertainty 当无限精度 |
| G-C05 | PRPC 缺席与唯一 `part` 各为正控制；改为 requirement/alternative solution/ply→profile 越界并指出新增规则 | 错称普通 part definition 必需 part 类别，或任意类别无影响 |
| G-C06 | REP.name 普通→G20/G37 条件假；分别改为 centre of mass、treatment result→UnsupportedProfile；对应原规则在既有 items 下为假是独立语义对照 | 丢掉 representation.name 的语义；把能力越界和源错误合并为一个状态 |
| G-C07 | PDS.name 普通→函数预定 UNKNOWN，G36 最终真；改为 hardness→UnsupportedProfile。原函数对照：REP.name 不同→假、相同→真 | 错把 SDR 排除在 PDR 总体；将 schema 预定 UNKNOWN 当实现未判定 |
| G-C08 | 每 LOOP 由 bound 引用→G47 真；插入独立 EDGE_LOOP→profile 归属检查与 G47 均不能通过 | 只对正向根图求值，孤立 LOOP 未进入 FOR |
| G-C09 | 不允许的 complex facet 即使其基础 LINE 可读仍 UnsupportedProfile | TYPEOF 只看最熟悉实体名；G43 的恒真证明依赖签名白名单 |
| G-C10 | 加一个 assembly/context association/style/PMI/PLIB 等记录，即使无 face 引用仍越界 | 用“不在正向闭包”冒充全局不触发证明 |
| G-C11 | 不触发 RULE 涉及多个 FOR 总体时，PRPC 可以非空，而外层序列空或 IF(ply)假 | 把每条 N 简写为全部 FOR 类型均不存在 |
| G-C12 | 语言语义控制：QUERY(TRUE)收元素、QUERY(FALSE/UNKNOWN)不收；NOT UNKNOWN=UNKNOWN；相同坐标的不同 #id 仍非实例身份相等；TYPEOF 含 supertype；USEDIN 空角色含全部显式引用 | 用二值语言默认强转破坏 QUERY、值相等替代身份、忽略 subtype 或反向角色 |

## 6. 当前完成与未完成

已完成的是固定 N2635 字节下 **47/47 RULE 的手工覆盖（5条 M、1条 C、41条 N）、活跃四函数闭包、各条空集/名称/外循环不触发前提以及主要反例设计**。N 仍含强制 profile guard；不表示跳过 guard。G36/G43 虽可在该 guard 下证明恒真，仍归 M 以保留实际适用总体及求值依据。这不能代替 ENTITY/TYPE 的继承 WHERE、DERIVE、INVERSE、UNIQUE、SELECT、complex 合法性；这些必须与几何和产品/context 清单合并后才构成完整首片契约。

尚未实施或运行：规则专用实现及其等价性、三值/QUERY/身份/集合语义控制、全部 DATA 反向索引、循环/资源保护、受约束图的实际 STEP 字节、独立真值计算和 SMLib 构建/清理验收。P0 未因本文完成而退出，任何运行时 profile 尚未因此获准 Accepted；已认可设计基线0.1不变。
