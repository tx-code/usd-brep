# P0 来源补核：AP203版次、单位及产品约束

版本：P0-AP203-A0.1，2026-10-15。本文只核对可追溯资料与设计约束，不授予reader或AP符合性资格。未写parser、fixture或测试，未调用CAD读取器，未读取模型库。本文补充[首个平面／盒体切片](p0-planar-slice.md)，不修改已批准主规格。

## 1. 已取得的原文与证据等级

[MBx-IF当前EXPRESS下载页](https://www.mbx-if.org/home/mbx/resources/express-schemas/)将AP203 Ed.2列为2011，提供[Longform ZIP](https://www.mbx-if.org/home/wp-content/uploads/2024/07/part403ts_wg3n2635mim_lf.zip)，并列出schema OID `{1 0 10303 403 2 1 2}`。这关闭了旧CAx-IF地址404导致的“没有原文”缺口，但文件来源和内容年代仍须分开记录。

| 资料 | 固定身份 | 可以证明／不能证明 |
|---|---|---|
| MBx-IF AP203e2下载包 | ZIP SHA256 `398a3f6fb1f148494594ed23fa90d574498f83037468d4bb8e42d58752e29961` | 证明本次从当前官方资源入口取得这组字节；不是独立ISO出版原件认证 |
| 包内 `part403ts_wg3n2635mim_lf.exp`，665100 bytes | SHA256 `c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088` | 文件头自述WG3 N2635、ISO/TS10303-403、CVS日期2009-09-10；不能把页面2011改写成文件生成时间 |
| [STEPcode AP203e2副本](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp) | commit `24783b448511b8f6bce2829286a3f00015430236`，与MBx包内EXPRESS逐字节同SHA256 | 可用固定行号审阅同一原文，不靠可变分支；两处相同不是两个独立规范真值 |
| [STEPcode AP203e1副本](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp) | 同commit，208521 bytes；SHA256 `020b4d25dbd0b6ee7d15099b978e3448f6699a72cb862d381e416e32187562f1` | 文件头明确说明是修改过的AP203 Amd.1，基于N916／2000-05-04；不能标成未修改的ISO原件 |

AP203e1副本注释称删除一个永远走同一分支的IF以适配libexpress，且声称不影响行为。这是维护者说明，不是本项目已证明等价。故e1目前是高价值的项目副本证据；仍需未修改N916／可审阅差异或同等来源闭包后，才提升其规范证据等级。[修改说明与schema声明](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L1)

原始下载与`provenance.json`保存在仓库外 `C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/`。没有将整份第三方schema复制进产品源树，没有安装其SDK。

## 2. FILE_SCHEMA与版次登记

| 源名／标识 | 本次可核事实 | P0登记边界 |
|---|---|---|
| `CONFIG_CONTROL_DESIGN` | 第一版名称；本次e1副本基于2000 amendment | 名称区分e1族，不能独自区分1994原版与Amd.1；未知OID不删除后猜测 |
| `AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF` | 第二版名称；MBx表关联OID `{1 0 10303 403 2 1 2}` | 保存完整名称/OID及固定schema hash；无OID只表示源未声明这段版次信息 |
| `CONFIGURATION_CONTROL_3D_DESIGN_MIM_LF` | STEP Tools将其说明为2005临时TS名称 | 独立历史变体，不能仅改名为最终e2后声称规则已核 |
| `CCD_CLA_GVP_AST_ASD`／`CCD_CLA_GVP_AST` | STEP Tools说明其来自历史构建测试 | 识别线索，不授予本片转换资格 |

上述家族／历史名称依据[STEP Tools AP203用法说明](https://www.steptools.com/docs/stp_aim/notes_ap203.html)。它还说明merged库会将旧context映射为父型；本项目必须保存原类型，不能把第三方内存映射当源数据没有该类型。

`APPLICATION_PROTOCOL_DEFINITION.application_protocol_year`不是FILE_SCHEMA版次替身。P0 registry需要分别记录：原声明、匹配的schema制品、由制品得到的规则、项目已支持的输入包络。仅识别家族时不得称`SchemaValidated`。

## 3. 会改变P0用例期望的字段差异

以下e1栏只指固定的修改副本，e2栏只指MBx分发的N2635字节；不是对所有历史版本的概括。

| 项目 | AP203e1 Amd.1项目副本 | AP203e2 N2635 | 首片影响 |
|---|---|---|---|
| `CONVERSION_BASED_UNIT`继承的`dimensions` | 显式；该子型没有DERIVE重声明 | 显式；WHERE核对factor单位维数 | conversion complex用`NAMED_UNIT(#dim)`；不能沿用其他AP中可能存在的derived签名 |
| `SI_UNIT`继承的`dimensions` | DERIVE | DERIVE | SI complex为`NAMED_UNIT(*)`；不能因conversion显式就同样填写SI |
| PRODUCT.description | required text | OPTIONAL text | `$`分版次，空字符串与缺失仍不同 |
| PRODUCT_DEFINITION.description | required text | OPTIONAL text | e2合法缺失不回填空串；e1不能静默接受缺失 |
| PRODUCT_DEFINITION_FORMATION.description | required text | OPTIONAL text | 原P24不可统一把`$`设为所有AP正例 |
| PROPERTY_DEFINITION.description（含PDS） | required text | OPTIONAL text | PDS完整根链同样需要按profile检查 |
| UNCERTAINTY_MEASURE_WITH_UNIT.description | required text | OPTIONAL text | 单位文档里的`$`片段仅对核准可缺省的profile成立 |
| PRODUCT.id | 声明UNIQUE | 本次e2实体没有该UNIQUE | 按实体引用身份保留两份product；是否违反源唯一约束另按profile裁决，不能通过按name/id合并“修复” |
| ADVANCED_FACE.face_geometry | 通过WHERE限制继承属性 | 通过WHERE限制继承属性 | 本次两个schema均非merged字典的SELECT重声明写法；simple仍为四个字段，不增加derived占位 |

字段依据：[e1 conversion](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L1115)、[e2 conversion](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L3990)、[e1产品链](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L2189)、[e2产品链](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L9460)、[e1 uncertainty](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L2810)、[e2 uncertainty](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12095)。

e2的conversion WHERE只证明其继承维数应等于factor单位的推导维数；正有限因子、无环、数值承载和质量预算仍有本项目独立要求。不能将这些项目要求伪称为同一条EXPRESS WHERE。

## 4. 单位、context和uncertainty规则

| 制品／规则 | 精确效果 | P0动作 |
|---|---|---|
| e1 `global_unit_assignment` | 对每个GUAC要求SET恰好3个成员；长度、平面角、立体角各恰好1个 | length+rad但缺sr在此profile是反例，即使纯LINE/PLANE也不能通过 |
| e1 `subtype_mandatory_representation_context` | representation_context须具GEOMETRIC_REPRESENTATION_CONTEXT型 | 不把纯基础context当完整3D表示context；仍由项目要求3D |
| e1／e2 GUUNC实体 | uncertainty SET `[1:?]` | constituent缺席和出现空SET不同；空SET非法 |
| e2 `consistent_uncertainty` WR1 | 同一GUUNC内不能有两个相同name的uncertainty | SET成员身份不同、数值相等或不同，都不能替代name唯一检查 |
| e1／e2 `valid_measure_value` | 数字型uncertainty须大于0 | 零／负数不能按“未声明”跳过；其他SELECT分支另按支持范围处理 |
| e2 GUAC实体 | units SET `[1:?]`；本次文件没有e1同名的三单位全局规则 | 不移植e1三单位结论；项目仍显式要求实际消费的长度/角度语义及冲突处理 |
| e1／e2 `compatible_dimension` | 点／方向在其所属geometric context中维数须一致 | 须沿representation使用关系判断context，不能只验证列表长度范围 |

规则原文：[e1全局单位](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L3189)、[e1强制context](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L3500)、[e2 GUAC/GUUNC](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L6461)、[e2重复uncertainty名](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12457)、[e2维数一致性](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12398)。

“未发现同名全局规则”不等于已证明任意e2文件均可省角单位；本片仍须完成所选实体、context使用链及适用规则闭包。canonical正例保留length/rad/sr有助于固定真值，但不能因此把三单位强制性误标为所有AP共同事实。

## 5. 产品链、旧context和管理信息的边界

e1的强制约束比“有PRODUCT→formation→PD→PDS→SDR”更强：PRODUCT_CONTEXT须为MECHANICAL_CONTEXT，discipline_type为`mechanical`；有property形状关联的PD需DESIGN_CONTEXT，life_cycle_stage为`design`；formation须为WITH_SPECIFIED_SOURCE子型。e2仍保留这两个旧context及其局部WHERE，但将它们列为deprecated；不能从deprecated推成非法，也不能在源层删掉子型身份。[e1强制子型](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L3469)、[e1设计context规则](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L3146)、[e2弃用清单](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L23)。

APD的规则不能按名称猜量词：e1对每个application_context要求恰好一个匹配APD，其schema-name字段等于小写`config_control_design`。e2 N2635的`application_protocol_definition_required`实际只要求**至少有一个**application_context具匹配APD，其字段值为小写`ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies`，没有`_mim_lf`后缀。项目可以为选择根另要求上下文可判定，但须注明是项目约束，不能声称e2 EXPRESS对每个context要求恰好一个。[e1 APD](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L2901)、[e2 APD](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12371)。

e1还要求每个PRODUCT恰好一个属于assembly／inseparable_assembly／detail／customer_furnished_equipment的相关分类，并要求version、审批、时间、person/organization及security等管理关联。已有P0的38类实体表不足以据此构造“全AP203e1符合”的完整正例。e2本次制品没有同一套无条件管理规则；其部分分类约束由assembly/part-occurrence等语义触发，不能直接搬用e1分类白名单。[e1分类与管理规则](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L3229)、[e2 assembly分类](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L12714)。

设计上的处理是将“选定精确BRep链条已解释”“已检查的schema规则集合”“完整AP符合性”分别登记。严格导入的源闭包政策不变：已核的必需规则不能跳过；影响不明的依赖继续阻止Accepted。若e1需要的管理实体尚未列入可保留/校验清单，就准确报告能力未完成，不能把其不存在改写为可忽略元数据。

## 6. 形状规则及P0用例补充建议

固定e1/e2的ABSR均允许合法辅助AXIS2_PLACEMENT_3D，但要求有solid或mapped item，并对solid shell内面型施加ADVANCED_FACE约束。MSSR允许SBSM、mapped item和辅助placement，并要求至少有SBSM或mapped；本片直接SBSM路径仍要检查其面/边/壳约束，不能只看representation名。[e1 ABSR](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203/ap203.exp#L503)、[e2 ABSR](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L2122)、[e2 MSSR](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap203e2/ap203e2_mim_lf.exp#L7165)。

下表是待关联总矩阵的**设计建议，全部NotRun**；不是现有STEP样件。后续主目录扩至63条，细分建议可与其重叠，不重复计数。

| 设计条目 | 期望与隔离目的 |
|---|---|
| P24按e1/e2拆分 | formation.description=`$`在N2635可合法；e1副本规则下非法；使用空串控制几何不变 |
| conversion dimensions分AP签名 | e1/e2为显式#dim；任何其他AP的derived签名必须按对应制品另证，不能自动容错切换 |
| e1缺sr | 从三单位规范例只移除sr；准确命中global_unit_assignment，不以没有曲面角参数放行 |
| e2重复uncertainty名 | 两个不同记录同name，含同值/不同值两变体；命中consistent_uncertainty，不能只看SET去重 |
| e1旧context强制性 | 用父型替换机械／设计context、用基础formation替代指定source子型；每次只改一项，准确拒绝 |
| e2旧context局部规则 | 旧子型但discipline/life_cycle字段错误应拒绝；合法旧子型不能仅因deprecated拒绝 |
| APD量词与字段值 | e1重复匹配APD、缺匹配APD；e2两个AC中仅一个匹配的规则控制，以及无任何匹配的反例；项目额外根要求独立报告 |
| e1产品管理闭包 | 最小几何链缺分类／审批／security各一项；不能用几何通过盖住已知规则失败 |
| PRODUCT同id两身份 | e1触发UNIQUE；e2保留独立身份并继续适用其他规则，不按id/name合并 |
| 不完整schema凭证 | 只有CONFIG_CONTROL_DESIGN名称或未核OID，且无规则制品映射；诊断待核，不把副本hash冒充源文件宣告的规范版次 |

## 7. 仍未关闭的事项

后续状态：本轮用户要求的AP203e2首个窄profile已形成[完整静态闭包合同](ap203e2-planar-profile.md)及全局/产品context/几何三册；下列第2、5项在其36种simple叶、六complex签名范围内已完成静态清单，不代表运行资格。普通MSSR FACE_SURFACE分支因N2635路径访问问题不准入，后续需独立依据；e1及最终ISO身份问题不因e2合同完成而关闭。

1. e1未修改N916或权威发布字节及与STEPcode改动的可审阅等价证据。
2. 选择实际P0可启用的schema profile；每个profile完整登记实体字段、SELECT、UNIQUE/WHERE、subtype约束与适用全局规则。本报告只完成重点差异核对。
3. e1管理链所需实体若进入初始有效正例，须补全其属性与规则清单；否则明确e1仍是识别/诊断能力，不授予严格导入资格。
4. e2 N2635的发布关联已由MBx资源页支持；若验收要求原始ISO2011发布制品的逐字节凭证，仍需另取相应出版物附件，不用文件头年份猜测。
5. complex合法组成和Part21 external mapping仍需逐profile验证；本文没有写序列生成器、EXPRESS evaluator或reader。
