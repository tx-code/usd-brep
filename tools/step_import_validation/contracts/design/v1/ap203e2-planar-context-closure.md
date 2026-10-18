# AP203e2 首个平面／盒体 profile：产品、context 与单位闭包

版本：P0-E2-C0.1，2026-10-15。仅完成固定原文的静态设计核对；所有输入、规则执行、数值转换及 native 构建状态均为 **Defined / NotRun**。本文没有生成 STEP 样件、实现读取器、运行 EXPRESS evaluator 或调用 CAD 内核。

本文补充[首个切片](p0-planar-slice.md)和[AP203 来源记录](p0-ap203-provenance.md)。几何／拓扑闭包及完整全局 RULE 清单由同轮附件提供，本文不据局部通过授予全 AP 符合性。

## 1. 固定依据与 profile 决定

唯一字段／约束依据是 MBx-IF 分发的 `part403ts_wg3n2635mim_lf.exp`，本地固定副本 SHA256 为 `c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`。其与 STEPcode commit `24783b448511b8f6bce2829286a3f00015430236` 中 AP203e2 文件逐字节相同；下方链接用该固定 commit 的行号。来源等级仍见[原文来源记录](p0-ap203-provenance.md)，不将其自述的 ISO/TS 年代改写为 ISO 最终出版凭证。

本附件区分 **S（schema 必查）**、**P（项目 profile 必查）**、**N（已有证明的不触发分支）**。N 必须绑定完整文件的人口、类型或字段谓词证据；缺少前向引用不等于已证明不触发。

| 项目 | 首个 profile 的确定边界 | 性质与原因 |
|---|---|---|
| 产品根 | 一个 PRODUCT、一个 formation、一个普通 PRODUCT_DEFINITION、一个 PDS、一个 SDR；SDR 直接使用 ABSR 或 MSSR | P：固定单零件闭包；不能从多根中自动丢件 |
| 应用上下文 | 产品与 PD context 共用同一个 APPLICATION_CONTEXT；至少一个 APD 指向该 AC 且匹配原文 schema-name 字符串；首片只接受一个 APD | APD 存在为 S；共享／单数收敛为 P |
| PD context | `name='part definition'`，`life_cycle_stage='design'` | P：明确采用零件定义语义；不是 AP203e2 对所有 PD 的字段限定 |
| 产品 context | 普通或旧 context 的 `discipline_type='mechanical'` | 普通 PC 为 P 包络；旧 MECHANICAL_CONTEXT 另有 S WHERE，同值不同依据 |
| 旧 context | 普通 PRODUCT_CONTEXT、PRODUCT_DEFINITION_CONTEXT 以及合法的 MECHANICAL_CONTEXT、DESIGN_CONTEXT 均可作为替代叶类型；保留原型身份 | S：旧类型虽 deprecated 仍在固定 schema 内；旧型各自 WHERE 必查；不能静默改型 |
| formation | 基础 PRODUCT_DEFINITION_FORMATION 或其 WITH_SPECIFIED_SOURCE 子型 | S：子型额外 `source` 枚举须验证；此 e2 不套用 e1 的强制子型规则 |
| 分类 | 可无；若有，首片接受一个 PRODUCT_RELATED_PRODUCT_CATEGORY，`name='part'`，`products` 仅包含该 PRODUCT | P：不强制补出分类；其他合法分类超出此窄 profile，不报成 schema 非法 |
| 管理关系 | 不包含 product-definition/context-association/formation 关系、assembly、configuration、PMI、attribute 赋值和其他管理关系 | P：由全文件类型／角色索引证实其缺席，不能仅因不在几何正向链上就忽略 |
| 全文件闭包 | 全部 DATA 记录须属于这一根、APD、可选分类和单位／uncertainty 依赖；不接纳孤立已知记录作为隐式额外根 | P：只适用于此首个窄 profile，不能推广成主规格所有未来独立根请求的统一限制 |
| 表示 context | 一个 GEOMETRIC_REPRESENTATION_CONTEXT + GLOBAL_UNIT_ASSIGNED_CONTEXT + REPRESENTATION_CONTEXT 的 complex；可再含 GUUNC | P：维数固定为 3；GUUNC 缺席合法，出现时不得为空 |
| context 单位 | 恰好一个长度单位、恰好一个平面角单位、零或一个立体角单位；角单位限 SI radian／steradian 的正确名义子型 | P：这是明确的项目包络；固定 e2 未声明 e1 的无条件三单位规则 |
| 长度单位 | SI metre 加合法 prefix，或 CONVERSION_BASED_UNIT + LENGTH_UNIT；conversion 链经长度 measure 到 SI 根 | P：上下文与 uncertainty、factor 的单位可以不同，但每条链都完整核验并换算 |
| uncertainty | 缺席允许；出现则只接受可量化的长度值，各自单位清晰、值正有限、同 context 内 name 唯一 | name 唯一和数值正值有 S 依据；仅长度、有限性和可计算性为 P |

canonical 正例可继续给毫米、弧度、球面度和正的长度 uncertainty；**canonical 给了三单位，不会把三单位反向变成 AP203e2 普遍强制条件**。平面角单位在本 profile 中显式要求，虽然 LINE／PLANE 不消费物理角值；这是为了固定初始质量与单位契约，不是假造实体 WHERE。

## 2. 产品链逐实体清单

`label`、`identifier`、`text` 在此制品都是 STRING 别名。空字符串是一个值，`$` 是缺失；必需字符串不得写 `$`。下面列的是 simple／internal mapping 展开的属性序列；DERIVE／INVERSE 不额外成为位置槽。引用身份使用实体 identity，不能按 id/name 文本合并。[基础类型](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1095)

| 叶实体／祖先 | simple 显式序列 | DERIVE／INVERSE／局部约束及首片落实 |
|---|---|---|
| APPLICATION_CONTEXT | `(application:label)` | DERIVE description=`get_description_value`、id=`get_id_value`；INVERSE context_elements SET[1:?] OF application_context_element FOR frame_of_reference；WR1 description-attribute 反向基数≤1，WR2 id-attribute≤1。本片两种 attribute 人口为零；PC/PDC 反向成员存在，不能漏 inverse 下界。[2393](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L2393) |
| APPLICATION_PROTOCOL_DEFINITION | `(status:label, application_interpreted_model_schema_name:label, application_protocol_year:year_number, application:AC)` | 无实体 WHERE、UNIQUE、DERIVE、INVERSE。`year_number` 在本固定制品仅 INTEGER，**没有 merged 字典的 >1581 规则**。年份不能取代 HEADER／制品映射。APD 存在规则见 §6。[2413](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L2413)、[1948](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1948) |
| PRODUCT_CONTEXT ← APPLICATION_CONTEXT_ELEMENT | `(name:label, frame_of_reference:AC, discipline_type:label)` | 父类声明 PRODUCT_CONCEPT_CONTEXT／PRODUCT_CONTEXT／PRODUCT_DEFINITION_CONTEXT 为 ONEOF 分支，不能组合 PC 与 PDC；普通 PC 没有 `mechanical` WHERE，首片以 P 包络要求该值，不靠其文字推导几何单位。[2406](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L2406)、[9559](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9559) |
| MECHANICAL_CONTEXT ← PRODUCT_CONTEXT | 同 PC 三槽，无新增字段 | WR1 discipline_type 必须精确等于 `mechanical`；普通 PC 与此旧子型不能混用字段规则。[7423](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L7423) |
| PRODUCT_DEFINITION_CONTEXT ← APPLICATION_CONTEXT_ELEMENT | `(name:label, frame_of_reference:AC, life_cycle_stage:label)` | 普通 PDC 无局部 WHERE；本片项目限定 part definition／design。不是将所有其他合法 PDC 宣判非法。[9578](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9578) |
| DESIGN_CONTEXT ← PRODUCT_DEFINITION_CONTEXT | 同 PDC 三槽，无新增字段 | WR1 life_cycle_stage=`design`；name 仍按项目选择 part definition。[4415](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L4415) |
| PRODUCT | `(id:identifier, name:label, description:OPTIONAL text, frame_of_reference:SET[1:?] OF PC)` | 无 WHERE／UNIQUE／DERIVE／INVERSE；本片 SET 恰好一个合法 PC。**没有 PRODUCT.id UNIQUE**，不能拿 e1 唯一性移植过来。[9460](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9460) |
| PRODUCT_DEFINITION_FORMATION | `(id:identifier, description:OPTIONAL text, of_product:PRODUCT)` | UR1 为 `(id,of_product)` 组合唯一；同 id、不同 product 不因这条违反唯一性，同 product 的重复 id 则违反。须跨整个人口查，不只查所选叶。[9610](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9610) |
| PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE ← formation | 上述三槽后加 `make_or_buy:source` | source 仅 made／bought／not_known；不能把缺失写成 not_known，无额外 WHERE。[9628](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9628)、[1767](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1767) |
| PRODUCT_DEFINITION | `(id:identifier, description:OPTIONAL text, formation:formation, frame_of_reference:PDC)` | DERIVE name=`get_name_value`；WR1 NAME_ATTRIBUTE 反向数≤1；本片 attribute 缺席，因此 derived name 为 `?`，不回填 id／PRODUCT.name／空串。无 PD.id UNIQUE。[9565](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9565) |
| PROPERTY_DEFINITION（PDS 祖先） | `(name:label, description:OPTIONAL text, definition:characterized_definition)` | DERIVE id=`get_id_value`；WR1 ID_ATTRIBUTE 反向数≤1；本片只作为 PDS 祖先，不接纳泛型 property 包装。[9782](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9782) |
| PRODUCT_DEFINITION_SHAPE ← PROPERTY_DEFINITION | 同 property 三槽 | UR1 继承的 definition 唯一；WR1 definition 的 SELECT 动态类型必须属于 CHARACTERIZED_PRODUCT_DEFINITION 或 CHARACTERIZED_OBJECT。本片 definition 直接指 PD，满足第一支；PDS.name 可为任意非 §5 六种 surface-condition 专名的 label，canonical 采用空串。[9665](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9665) |
| PROPERTY_DEFINITION_REPRESENTATION（SDR 祖先） | `(definition:represented_definition, used_representation:REPRESENTATION)` | DERIVE description/name 各调用 getter；WR1 DESCRIPTION_ATTRIBUTE 反向数≤1、WR2 NAME_ATTRIBUTE≤1；本片 attribute 人口为零，不替 derived 值造字符串。[9801](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9801) |
| SHAPE_DEFINITION_REPRESENTATION ← PDR | 同 PDR 两槽 | WR1 definition 为 PDS，或 definition.definition 属 SHAPE_DEFINITION；本片直接 PDS 采用第一支；WR2 used_representation 必须属于 SHAPE_REPRESENTATION，本片 ABSR／MSSR 叶均继承满足。第二支保留为未触发，不做猜测包装展开。[10415](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L10415) |
| PRODUCT_CATEGORY（可选 PRPC 祖先） | `(name:label, description:OPTIONAL text)` | DERIVE id=`get_id_value`；WR1 ID_ATTRIBUTE 反向数≤1；本片不接纳独立父型 category，不暗示其他分类非法。[9468](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9468) |
| PRODUCT_RELATED_PRODUCT_CATEGORY ← PRODUCT_CATEGORY | `(name,description,products:SET[1:?] OF PRODUCT)` | 无新增局部 WHERE／UNIQUE；本片只有可选单个 `part` 分类及唯一 PRODUCT 成员。[9738](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9738) |

分类缺席不触发“补分类”动作。`restrict_part_occurrence_category` 对 context.name=`part occurrence` 的 PD 才要求 part／raw material／tool；本片是 `part definition`，其筛选集为空。`part` 分类不触发 functionality／conceptual design／requirement／ply 特殊分支。依据与全局量词逐项见同轮全局规则附件。[part-occurrence 分类谓词](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12871)

### 2.1 SELECT 闭包不等于任意分支可读

| 定义 | 固定原文完整分支 | 本 profile 使用／其他分支 |
|---|---|---|
| characterized_definition | characterized_object、characterized_product_definition、shape_definition | 本片经 characterized_product_definition→PD；其他支路未触发且不在转换范围 |
| characterized_product_definition | product_definition、product_definition_relationship | 仅直接 PD；关系支路出现即超出范围 |
| represented_definition | general_property、property_definition、property_definition_relationship、shape_aspect、shape_aspect_relationship | 仅 property_definition 的 PDS 子型 |
| shape_definition | PDS、shape_aspect、shape_aspect_relationship | SDR 首支直接 PDS 成立；不为支持 SDR 第一支额外开放其余包装 |

来源：[characterized SELECT](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L514)、[represented_definition](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1585)、[shape_definition](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1690)。已知而未支持的合法分支应报 Unsupported；错误 SELECT 引用是源类型错误，两者不混报。

getter 的形参 SELECT 也属于静态依赖：description_attribute_select 的本片成员为 AC、PDR／SDR、REPRESENTATION／ABSR／MSSR；id_attribute_select 的本片成员为 AC、PROPERTY_DEFINITION／PDS、PRODUCT_CATEGORY／PRPC、REPRESENTATION／ABSR／MSSR；name_attribute_select 的本片成员为 PD、PDR／SDR。这只是对形参合法性的证明，并不开放 DESCRIPTION_ATTRIBUTE、ID_ATTRIBUTE、NAME_ATTRIBUTE 实体输入。本片这些赋值人口为零，因此 SELECT 其余分支不需要构建或转换；不能将它们不存在当作 getter 依赖名称也可省略。[description SELECT](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L819)、[id SELECT](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1049)、[name SELECT](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1335)

## 3. representation／context 的祖先及合法 complex

| 实体／祖先 | 显式／派生／inverse | 必查与不触发 |
|---|---|---|
| REPRESENTATION（ABSR／MSSR 祖先） | name；items SET[1:?] OF RI；context_of_items；DERIVE description/id | WR1 id-attribute≤1、WR2 description-attribute≤1；本片 attribute 为零。表示名限定不等于 `centre of mass`／`treatment result`，从而避免两个名字驱动的特殊 RULE 分支；不把显示名称当任意无语义元数据。[10004](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L10004) |
| REPRESENTATION_CONTEXT | context_identifier；context_type | INVERSE representations_in_context SET[1:?] OF REPRESENTATION FOR context_of_items。本片至少被所选 ABSR／MSSR 引用；祖先字段只出现一次。[10021](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L10021) |
| GEOMETRIC_REPRESENTATION_CONTEXT ← REP_CONTEXT | coordinate_space_dimension:dimension_count | dimension_count 的 WHERE >0；本片项目要求 3，不把其他正维数改报基础类型非法。[6278](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L6278)、[839](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L839) |
| GLOBAL_UNIT_ASSIGNED_CONTEXT ← REP_CONTEXT | units:SET[1:?] OF unit | 没有本体 WHERE；本片额外检查恰一长度、恰一平面角、至多一立体角及物理类型正确性。[6467](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L6467) |
| GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT ← REP_CONTEXT | uncertainty:SET[1:?] OF UMWU | constituent 可缺席；存在时非空和 consistent_uncertainty WR1 必查；WR2／WR3 的 uncertainty_assigned_representation／qualified_representation_item 人口为零。[6461](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L6461) |
| REPRESENTATION_ITEM（RI） | name | WR1 using_representations(SELF) 非空。所有间接几何／拓扑项同样继承，不仅是 REP.items 中的顶层 item。[10029](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L10029) |
| GEOMETRIC_REPRESENTATION_ITEM ← RI | 无新增显式字段；DERIVE dim=dimension_of(SELF) | WR1 所有 using representations 的 context 均必须含 GRC。不能只检查“找到的第一个 representation”。[6284](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L6284) |

首片支持两种 context constituent 集合。下列为**字段签名说明**，不是可运行 STEP 样件或序列生成器；成分按名字排列，父属性在 REP_CONTEXT 局部记录中一次提供：

- 无 uncertainty：`GEOMETRIC_REPRESENTATION_CONTEXT(3)`、`GLOBAL_UNIT_ASSIGNED_CONTEXT(units)`、`REPRESENTATION_CONTEXT(identifier,type)`。
- 有 uncertainty：上面三者加 `GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT(uncertainty_set)`。

不能把 GUUNC 缺席与 `GUUNC(())` 混为一谈。多个 sibling constituent 共享同一 REP_CONTEXT 父实例，不能因多重继承而复制两个 context_identifier 槽。首片不接纳额外 unknown context constituent；known-but-unsupported 分支是能力拒绝，缺必需父型／重复成分／错误槽是源映射错误。

### 3.1 representation 使用路径与源图的终止边界

本片的典型逆向使用链是：坐标点→VERTEX_POINT→EDGE_CURVE／OE→EDGE_LOOP→FACE_BOUND→FACE_SURFACE→shell→solid／SBSM→REPRESENTATION.items。曲线点／向量／方向经 LINE、PLANE placement 等附属 RI 进入同一链。某些非 GRI 拓扑项仍是 RI，因此也要验证 RI.WR1。

`using_representations` 不会沿任意实体传播。它只从当前 item 收集直接 REP.items，以及经 RI／FOUNDED_ITEM 逆向使用链到达的 REP.items。SDR、product 等管理实体不会仅因引用 representation 就成为该图内的几何使用节点。必须按原函数的类型过滤建立图，不可将“所有引用”不加区分地全图传递。

所有使用路径都落在同一选定 3D context 为本片 P 约束；若出现第二表示引用同一几何，不能只在第一个 context 中解释后接受。不同 context/units 的共享语义须另行设计；首片准确拒绝它。

使用函数的形参 `founded_item_select` 完整分支仅 FOUNDED_ITEM／REPRESENTATION_ITEM。首片只含后一支，前者全文件人口为零；这与 REP_CONTEXT、unit、DIMENSIONAL_EXPONENTS 等非 RI 附件的存在不矛盾。把“所有实体均需 using_representations 非空”写成泛型规则会错误拒绝这些合法附件。[founded_item_select](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L998)

## 4. 单位、measure 与 uncertainty 清单

| 实体／祖先 | explicit／derived 与局部规则 | 首片落实 |
|---|---|---|
| NAMED_UNIT | dimensions:DIMENSIONAL_EXPONENTS；SUPERTYPE 两组 ONEOF 通过 ANDOR 组合 | 同组 SI／conversion／context-dependent 互斥；量类组 LENGTH／PLANE_ANGLE／SOLID_ANGLE 等互斥。不能只拿几个名字拼任意 complex。[8373](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L8373) |
| SI_UNIT ← NAMED_UNIT | prefix:OPTIONAL si_prefix；name:si_unit_name；DERIVE 重声明 named.dimensions=`dimensions_for_si_unit(name)` | 继承 dimensions 槽必须 `*`。WR1 仅限制被 DERIVED_UNIT_ELEMENT 使用的 MASS_UNIT 必须 kilo；本片无 mass／derived，因此该前件为 false，规则表达式整体为 true。[10702](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L10702) |
| LENGTH_UNIT ← NAMED_UNIT | 无新增字段；WR1 七维指数必须为 (1,0,0,0,0,0,0) | 无论 SI 还是 conversion 均必查；不能因单位名叫 inch/mm 就跳过。[6828](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L6828) |
| PLANE_ANGLE_UNIT ← NAMED_UNIT | 无新增字段；WR1 七维指数均为 0 | 本片与 SI_UNIT 组合且 name=radian；不把维数同为零的 steradian／ratio 当可互换。[9045](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9045) |
| SOLID_ANGLE_UNIT ← NAMED_UNIT | 无新增字段；WR1 七维指数均为 0 | 本片可选，与 SI_UNIT 组合且 name=steradian；出现则完整验证。[10755](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L10755) |
| CONVERSION_BASED_UNIT ← NAMED_UNIT | name:label；conversion_factor:MWU；WR1 named.dimensions 等于 `derive_dimensional_exponents(factor.unit_component)` | 本制品不将 dimensions 重声明 DERIVE，所以该父槽为显式 DIMENSIONAL_EXPONENTS 引用，不能填 `*`。本片只与 LENGTH_UNIT 组合；factor 正有限、量类匹配、链无环属于额外 P。[3990](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L3990) |
| DIMENSIONAL_EXPONENTS | length、mass、time、electric_current、thermodynamic_temperature、amount_of_substance、luminous_intensity 共 7 个 REAL，固定顺序 | 无 WHERE／UNIQUE／INVERSE；不能自行把指数限整数。此片实际只会消费上述长度／零向量；换算必须检查可计算有限值。[4625](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L4625) |
| MEASURE_WITH_UNIT | value_component:measure_value；unit_component:unit；WR1 valid_units(SELF) | 本片支持基础 MWU 承载长度 typed value，或长度子型；完整 SELECT 类型标签要保留。不能将裸数猜成 LENGTH_MEASURE。[7414](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L7414) |
| LENGTH_MEASURE_WITH_UNIT ← MWU | 同 MWU 两字段；WR1 unit_component 含 LENGTH_UNIT 型 | factor 可用此简单子型；额外要求 value_component 是长度族，防止仅用量类标签把不一致 value 冒充长度。[6821](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L6821) |
| PLANE_ANGLE_MEASURE_WITH_UNIT ← MWU | 同两字段；WR1 unit_component 含 PLANE_ANGLE_UNIT | 类型登记到位，但本 profile 无角 conversion／角 uncertainty／物理角 measure 的消费节点，其人口为零；出现为 UnsupportedMeasureBranch，不能误作长度 factor。[9038](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9038) |
| SOLID_ANGLE_MEASURE_WITH_UNIT ← MWU | 同两字段；WR1 unit_component 含 SOLID_ANGLE_UNIT | 与角 measure 同理，当前人口为零；不是因为维数为零便可省掉名义类型核验。[10748](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L10748) |
| UNCERTAINTY_MEASURE_WITH_UNIT ← MWU | value_component、unit_component、name:label、description:OPTIONAL text；WR1 valid_measure_value(value)；另继承 MWU.WR1 | 本片只接受简单 UMWU 的长度 typed value 与长度单位；不允许为方便把 uncertainty 写成长度子型但丢 UMWU 原型。与 LM 子型额外组合虽可能合法，未列入首片签名，准确 Unsupported。[12095](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12095) |

除表中列出的 inherited 规则外，单位／measure 这些实体没有隐含必补 id/name/description attribute 的规则。其“无 WHERE／UNIQUE”结论只针对本固定原文与表中叶类型，不能推成其他 subtype 一概相同。

### 4.1 固定单位 complex 签名

| 接纳的 constituent 集合与局部字段 | source dimensions | 语义与限制 |
|---|---|---|
| LENGTH_UNIT()；NAMED_UNIT(*)；SI_UNIT(prefix或$,metre) | 派生 | prefix 仅固定枚举；转换到毫米的比例为 prefix 对 metre 的倍数×1000 |
| NAMED_UNIT(*)；PLANE_ANGLE_UNIT()；SI_UNIT(prefix或$,radian) | 派生 | 可解释合法 prefix 的角量；canonical 用 `$`，弧度为内部物理角 |
| NAMED_UNIT(*)；SI_UNIT(prefix或$,steradian)；SOLID_ANGLE_UNIT() | 派生 | 可选，正确保留立体角角色；不能作为角量消费 |
| CONVERSION_BASED_UNIT(name,factor_MWU)；LENGTH_UNIT()；NAMED_UNIT(dimensions引用) | 显式 | factor 链终点必须是本片长度 SI 组合；source name 只保留，不参与数值选择 |

si_prefix 固定枚举为 exa、peta、tera、giga、mega、kilo、hecto、deca、deci、centi、milli、micro、nano、pico、femto、atto；不因为现代 SI 新增前缀就扩展本版 enum。对应 10 的指数是 18、15、12、9、6、3、2、1、−1、−2、−3、−6、−9、−12、−15、−18。[原枚举与 SI 名称](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1703)

不接纳 NAMED_UNIT 父型单独作为物理长度、CONTEXT_DEPENDENT_UNIT、DERIVED_UNIT／DERIVED_UNIT_ELEMENT、ratio、mass 等分支。这些可以是 schema 合法内容，但本片没有转换契约。不能用“维数恰好是长度”替代 profile 所要求的 LENGTH_UNIT 名义类型，也不能自动把未知单位按名称映射成 mm。

本 profile 采用 simple／internal mapping 的普通记录，complex／external mapping 只开放 §3 两种 context 和本节四种单位签名，共六种 constituent 集合。MWU／LMWU／UMWU 当前仅 simple 形式；其他合法 external／CC2 展开并不因此授予读取资格，须走其对应载体 profile，不能在此片自动合并成支持。

### 4.2 typed measure 与数值闭包

`measure_value` 的原 SELECT 很宽；本片 factor／uncertainty 只接纳 LENGTH_MEASURE、NON_NEGATIVE_LENGTH_MEASURE、POSITIVE_LENGTH_MEASURE 三种长度族，保留最具体标签。后两者经命名类型继承回到长度类型，分别有 ≥0、>0 的 WHERE；factor／uncertainty 的实际消费均再要求 >0，因此零在该角色不能通过。基础 LENGTH_MEASURE 自身是 REAL，没有正值 WHERE。[完整 measure SELECT](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1187)、[non-negative](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1357)、[positive](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1483)

`unit` 完整 SELECT 只有 derived_unit／named_unit；本片只取后支，且再限定 §4.1 的叶组合。`derive_dimensional_exponents` 的 derived_unit 分支因人口为零不执行；但对应依赖名必须在 manifest 中登记为范围守卫，不能只在遇到时当“无影响 metadata”。[unit SELECT](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L1912)

conversion 尺度递推为：某 conversion unit 的毫米尺度＝其 factor.value × factor.unit 的毫米尺度；终点 SI metre 按 prefix 换算。必须独立验证所有 declared dimensions、MWU value 量类、MWU unit 名义长度型及正有限数值；conversion.WR1 不会检查其 factor 数值正负，也不直接证明链无环。累计乘积溢出／下溢、循环或无法可靠计算均拒绝，不能猜尺度。共享链节点按实体身份共享；不同 source 单位名相同不合并。

uncertainty 的 value 使用它自己的 unit 换到毫米，不能直接乘 representation 的长度尺度。它仅作为源声明记录／告警，不自动覆盖项目工作容差、验收阈值、修复预算，也不因值大就允许移动几何。GUUNC 内不同实体的相同 name 必须命中 consistent_uncertainty，即使数值和单位完全一致。[consistent_uncertainty](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12457)

## 5. 函数递归闭包与分支判定

下表完整列出本附件实体直接触及的项目内 EXPRESS 函数；TYPEOF、USEDIN、QUERY、SIZEOF、HIINDEX、LOINDEX、EXISTS 等语言内建另列为语言语义前提，不伪装成省略的项目函数。

| 函数／直接函数依赖 | 首片必执行的分支或等价证明 | 已证明不触发的分支及理由 |
|---|---|---|
| get_description_value／get_id_value／get_name_value；无自定义子调用 | 各自沿精确反向角色取 BAG；恰好一个返回属性值，否则返回 `?`。各调用者 WHERE 的≤1仍单独必查。本片相关赋值实体全文件人口为零，导出 `?` 为规范行为 | 恰一条赋值分支未触发；不能把 `?` 当空串、false 或源错误。[14575](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L14575) |
| using_representations → using_items → using_items、bag_to_set | 先取直接 REPRESENTATION.ITEMS，再递归 RI／FOUNDED_ITEM 逆向使用链，最后对每个中间项再取 direct REP.ITEMS，输出去重 SET。RI.WR1、GRI.WR1 依赖它；必须覆盖全部 item 使用者 | FOUNDED_ITEM 类型分支人口为零；但 RI 分支不是空集。不得只看 `items` 顶层。using_items 的 checked_items 防环是原函数的一部分。[15647](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L15647)、[15678](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L15678) |
| item_in_context → item_in_context | 直接 REP.ITEMS 与 context.representations_in_context 有交集则 true；否则只沿 RI 反向使用者递归；所有支路无结果才 false。compatible_dimension 对全部 point／direction 和全部 GRC 使用它 | 本片无 founded item，故与 using_representations 的 founded 差异不影响此限定范围；不等于二函数对任意 AP 图相同。本片 RI 图的无环／可终止性需由输入守卫证明，不能把原函数没有 visited 集合误当可以不受限递归。[14923](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L14923) |
| bag_to_set；无子调用 | 遍历 BAG 元素加入 SET；按实体 identity 去重，不能按名称或结构值把不同 source 记录合并 | 空 BAG 分支返回空 SET；不代表 inverse 下界被满足。[13423](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L13423) |
| dimension_of → using_representations（及其闭包） | point 为 coordinates 长度；direction 为 ratios 长度；vector 为其 orientation.ratios 长度；其余 GRI 从使用表示的 GRC 取得维数。本片先证明所有使用 context 同一且为 3D，再允许原函数 `x[1]` 的取值 | 不以 SET 首元素选中的偶然 context 掩盖其他 context；无使用者由 RI.WR1 拒绝，不访问空 SET；纯非几何 context 由 GRI.WR1 拒绝。[14181](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L14181) |
| dimensions_for_si_unit；无子调用 | metre→(1,0,0,0,0,0,0)，radian／steradian→全零；prefix 不改变维数 | 其他 SI 名称分支因签名守卫不触发；OTHERWISE 返回 `?` 不作兜底单位；未识别 enum 先拒绝。[14220](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L14220) |
| derive_dimensional_exponents；无自定义子调用 | named-unit 分支返回其 dimensions；SI dimensions 的派生已由上行定义；conversion 的显式 dimensions 仍需自身 WHERE 与量类约束验证 | derived-unit 分支逐元素加权七维指数；本片其人口为零，单位 SELECT 守卫证明不执行。此函数自身**不递归 conversion factor**，尺度递归是项目另一个契约，不能混称原函数已有。[14145](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L14145) |
| valid_units → derive_dimensional_exponents | 它由一串独立 IF 组成，不是独占 switch；长度族 TYPEOF 可同时触发 LENGTH_MEASURE 与 POSITIVE_LENGTH_MEASURE 的检查，均需满足长度维数。末尾 true 不能替代项目 typed-value／名义单位检查 | mass、time、angle、ratio、area、volume、电学等 IF 因本片 measure 标签守卫为 false；不是因为这些维数同值或原函数完全不检查它们。[15964](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L15964) |
| valid_measure_value；无子调用 | REAL／INTEGER 动态数字支要求 >0；本片三种长度族落入 REAL 分支；其 typed WHERE 也独立必查 | 其他类型分支原函数可返回 true；比如 descriptive 字符串不能仅凭该 true 就作为本片 uncertainty 被接纳，先由支持包络拒绝。[15900](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L15900) |
| categories_of_product；无子调用；**已知辅助，不在本片活跃调用闭包** | 语义为沿 PRPC.PRODUCTS 返回 category.name 的去重 SET；无分类为空，有可选分类为 {'part'}。本片相关调用者的特殊 PD 人口被 profile 守卫排除，因此不要求实际调用它来证明这些空集分支 | 不沿任意 category relationship 扩展，不从 PRODUCT.name 推分类；不能因有可选 PRPC 就将本函数误标活跃必执行。[13504](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L13504) |
| surface_condition_correlation；无子调用 | SDR 也是 PDR，因此 restrict_representation_for_surface_condition 的人口**不为空**；PDS.name 不是下列六种专名（canonical 为空串），函数 OTHERWISE 返回 UNKNOWN，`NOT UNKNOWN` 仍 UNKNOWN，QUERY 只纳入 TRUE，故外层失败集为空 | `visual appearance`、`tactile appearance`、`contact ratio`、`hardness`、`treatment result`、`surface texture` 六种名字分支不触发。不得将“失败集为空”误写为函数返回 TRUE或整条 RULE 没有被遍历。[15570](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L15570)、[12933](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12933) |

语言层必须保留 `=` 的值相等、`:=:` 的实例相等、SET 的成员唯一和 BAG 的重复计数区别；conversion 的 dimensional-exponents 比较按其七个数值，不要求两条维数实体引用身份相同。LOGICAL 的 TRUE／FALSE／UNKNOWN 与未定值 `?` 不能统一转成 bool；尤其不得把 WHERE 中的 UNKNOWN 一概当“没问题”。本片只对上表已经逐表达式分析的 UNKNOWN 路径作结论。

## 6. 本附件直接涉及的全局规则接口

此表是局部闭包向完整 RULE ledger 的接口；它不能取代其余 RULE 的逐条审阅。

| 规则 | 真实量词／本片必查内容 | 不触发部分 |
|---|---|---|
| application_protocol_definition_required WR1 | 原文是存在至少一个 AC，其反向 APD 至少一个且 schema-name=`ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies`；小写且**无 `_mim_lf`**。单 AC、单 APD 的项目守卫使它在该 AC 上必须成立。[12371](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12371) | 不声称原文为所有 AC 恰好一个 APD；本片以外的多 AC／多 APD 先报范围差异，不改写原文量词 |
| compatible_dimension WR1／WR2 | 所有点／方向在所有可达 GRC 中的坐标／比率维数必须匹配；本片为 3。RI／GRI 基类检查单列，不让此规则替代它们。[12398](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12398) | 没有共享到其他 GRC 的输入守卫成立，才无额外 context 分支 |
| consistent_uncertainty WR1／WR2／WR3 | 有 GUUNC 时 WR1 检查每个 name 唯一；同身份 SET 的唯一性不能替代 name 唯一。[12457](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12457) | GUUNC 缺席则 WR1 population 空；WR2／WR3 的两类人口由签名守卫证零 |
| restrict_representation_for_surface_condition WR1 | 每个 PDR（包括 SDR）调用 surface_condition_correlation；本片以 PDS.name 不是六种专名而进入 UNKNOWN 分支（canonical 用空串），按 QUERY 真值语义使失败集为空 | 六类 surface-condition 名称不触发；不能把此条标成“PDR 未出现” |

其余 product／formation／PD／representation 为 FOR 人口的 RULE 仍可能被遍历，但按本片 part definition／可选 part 分类／无关系人口等谓词不进入特殊语义集合。这些结论应引用完整 RULE ledger 的对应行，不能用“本片没有管理实体”一句概括全部全局约束。

## 7. 反向索引是必要输入，不是优化选项

| 索引／全图人口 | 使用者 | 必须给出的证据 |
|---|---|---|
| AC ← APPLICATION_CONTEXT_ELEMENT.frame_of_reference | AC.inverse 下界；PC/PDC 归属；APD 对账 | 至少有 PC／PDC 两个有效元素；二者同 AC；不把 APD 当 context_element |
| AC ← APD.application | application_protocol_definition_required | 全部反向 APD，精确 schema-name 文本和身份；不沿 PRODUCT 正向找不到就判不存在 |
| PRODUCT ← PRPC.products | categories_of_product；多个分类驱动的全局规则 | 人口完整、可选 part 分类之外没有暗藏关系；源 SET 与文本分类名分别检查 |
| PRODUCT ← formation.of_product；formation ← PD.formation | formation.UR1；根唯一性和全局规则 | 文件内所有 formation／PD，不按引用到的第一个结束 |
| PD ← PROPERTY_DEFINITION.definition；PDS ← PDR.definition | PDS.UR1；SDR 根关联；关系驱动规则 | SELECT 动态类型准确，排除泛型 property／关系包装；同 PD 的重复 PDS 不能漏过 |
| REP_CONTEXT ← REPRESENTATION.context_of_items | context.inverse；item_in_context；单位绑定 | 全部表示使用者，不仅所选 SDR 指向的那一个 |
| 任意 item ← 任意显式引用，附 owner 类型和角色 | using_items、using_representations、item_in_context | 所有 DATA 段已完成实体／引用解析；再按原函数 RI／FOUNDED_ITEM 类型过滤；同一 BAG 的重复使用不能提前丢失 |
| item ← REPRESENTATION.items | 两种使用函数的直接种子 | SET 身份、直接与间接使用路径和 context identity |
| 任意对象 ← DESCRIPTION_ATTRIBUTE.described_item／ID_ATTRIBUTE.identified_item／NAME_ATTRIBUTE.named_item | derived getter 及≤1 WHERE | 首片全文件这些赋值人口为零；将来开放时按精确 role 计数，不能靠 `$` 或缺显式槽推断 |
| MASS_UNIT、DERIVED_UNIT_ELEMENT、FOUNDED_ITEM 等完整类型人口 | SI.WR1 及函数分支范围证明 | 初 profile 签名守卫拒绝其出现，生成“不触发”证据；unknown complex constituent 不能隐藏该类型 |

反向引用的依赖闭包包含 schema TYPEOF 祖先／subtype 身份；不是只将正向可达实体放进一个集合。尚未遍历的 DATA 段、未解析的 complex constituent、未定 SELECT 或悬空引用都会使“不触发”证明无效。初 profile 不从未知孤立 metadata 推导安全；影响不明即拒绝，且仍保留原始记录用于诊断。

## 8. 静态完成与后续验收条目

本附件已将产品／context／单位的字段、祖先、局部规则、SELECT 分支、关键 inverse、合法 constituent 集合及自定义函数递归依赖列全；**这是设计闭包，不是执行证据**。以下是应并入 P0 测试 manifest 的设计条目，均 NotRun，不重复计入原用例总数。

| 组 | 正／反控制及预期 |
|---|---|
| 产品基础 | 基础／specified-source 两 formation，optional description=`$`／空串，普通／合法旧 context 四种组合；保持原型。旧 mechanical discipline 错值、旧 design life-cycle 错值是局部 S 错误 |
| 唯一性 | 同 product+formation.id 两 formation 命中 UR；同 PD 两 PDS 命中 UR；PRODUCT 同 id 不因此触发不存在的 UNIQUE，但第二 product 超首片范围；准确区别规则与包络 |
| APD | 正确无后缀 schema-name；误写 HEADER 的 `_mim_lf` 全名不能匹配；APD 年份整数不套 >1581；额外 AC／APD 报项目范围而非篡改∃量词 |
| 分类／名称谓词 | 无 category 与单 part 均是范围正例；part occurrence／alternative definition／特殊类别变更必须触发范围拒绝并指出后续规则闭包，不能继续沿普通零件通道 |
| inverse | 孤立 AC／REP_CONTEXT 触发 inverse 下界；孤立 RI 触发 using_representations 非空；几何被第二 context 使用必须被发现，不能只测正向根 |
| context | 无 GUUNC 正例；有非空 GUUNC 正例；GUUNC 空 SET 是 S 错；两个不同 uncertainty 同 name 是 S 错；不同单位且可正确换算是正例 |
| complex | SI NAMED_UNIT(*) 与 conversion NAMED_UNIT(#dimensions) 分开；调换两种槽应拒绝；重复 constituent、量类 ONEOF 冲突和额外 unsupported constituent 分别定位 |
| 单位与 factor | mm／cm／m／inch；inch 通过 exact 25.4mm factor；foot→inch→mm；同名不同因子保留独立身份；负／零／循环／量类错／数值不可计算拒绝 |
| function 语义 | 长度 typed value 的 TYPEOF 祖先闭包，valid_units 多个 IF；non-numeric uncertainty 即使 valid_measure_value 返回 true 仍被 P 守卫拒绝；derived getter 缺赋值为 `?`；surface_condition_correlation 返回 UNKNOWN 的路径不能被 bool 转换误杀或误判 TRUE |
| 边界清理 | 所有拒绝均不交付模型；检查报告区分源字段／局部规则错误、profile Unsupported 和未定依赖，未运行 native 路径不声称所有权／清理已通过 |

此文没有改变已批准的主规格数值预算，也没有由源 uncertainty 推生产容差。P0 的独立数学真值仍在[独立真值与用例表](p0-planar-cases.md)，native 构建／失败清理门槛仍在[SMLib 来源审计](p0-smlib-source-audit.md)。
