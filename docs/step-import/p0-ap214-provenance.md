# P0：AP214 e3 原文来源与版次差异

日期：2026-10-15。状态：**资料核对，未编码、未生成 STEP 样件、未运行符合性或导入测试**。本报告是 [P0 平面／盒体切片](p0-planar-slice.md) 的来源补充，不修改已认可的[设计规格 0.1](../step-import-repair-spec.md)，也不宣布完整 AP214 支持。

## 1. 取得了什么

已经从 MBx-IF 当前的 [EXPRESS Schemas 官方索引](https://www.mbx-if.org/home/mbx/resources/express-schemas/)取得它标为 **AP214 Ed.3／2010** 的 [Longform ZIP](https://www.mbx-if.org/home/wp-content/uploads/2024/07/AP214E3_2010.zip)。原先推荐实践中的 `cax-if.de/documents/` 地址失效，本次采用发布方当前页面实际链接，未使用商业安装包、个人镜像或生成器输出。

| 项目 | 本次冻结证据 |
|---|---|
| 检索／下载 UTC | ZIP 本地写入时间 `2026-10-15T13:48:34.8958754Z`；manifest 记录时间 `2026-10-15T13:50:36.1520993Z` |
| ZIP | `AP214E3_2010.zip`，101465 bytes；SHA256 `8b6360658c444c6b90e0ad63137d7c392103edc4cbc39dc440ee497378630b74` |
| ZIP 唯一成员 | `AP214E3_2010.exp`，860508 bytes；成员时间 `2010-01-20T11:33:36+08:00` |
| EXPRESS | SHA256 `71ab140fe7f774321beee6a31e6fee2afc3973fd60350ae2018c74c211fb4295`；原始字节未改写 |
| HTTP | `200`；`application/zip`；Last-Modified `Tue, 30 Jul 2024 09:58:25 GMT`；ETag `"18c59-61e7404c36200"` |
| 项目 commit | 不适用：来源是官方页面链接的发布文件，没有借用某个 Git 项目的提交身份 |
| 来源分类 | `PublisherLinkedArchive`；不是本项目认证过的 ISO 原版逐字副本 |

原始 ZIP、EXP、官方索引页面快照与 `retrieval-manifest.json` 均留在仓库外的研究目录 [ap214](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap214)。manifest 保存 URL、UTC、长度、hash 和响应头。没有把第三方 schema 复制进仓库源码目录。

版权／许可记录：ZIP 只有 EXPRESS 文件，没有独立 LICENSE；文件头提供作者、机构、工具和来源说明，全文未发现独立版权／许可条款。这不等于公有领域，也不使它自动适用本仓库的 Apache 许可。本轮保留原始文件及其说明，仅形成研究证据；后续若要把第三方 schema 随产品再分发，应另核实相应授权。这里没有采用 STEPcode，故不将其 BSD 许可移植到该文件。

## 2. 版次和标识仍有两处不能抹平的差异

该文件第 1 行自述 `ISO/DIS 10303-214:2007`，第 15 行日期为 `2009-06-30`；第 18 行注册注释写 `part(214) version(3) object(1) automotive-design-schema(1)`，第 21 行 schema 名为 `AUTOMOTIVE_DESIGN`。这与官方页面将 ZIP 标作 2010 e3 的事实一并保存。ISO 自己的 [10303-214:2010 条目](https://www.iso.org/standard/43669.html)确认第三版、2010-05 及已撤销状态，却没有在本轮提供一个可独立逐字比对的已发布 EXPRESS 文件。因此可以说“取得发布方当前提供的 e3 longform”，不能说“已经证明与正式 IS 原文完全一致”。

第二个差异是 OID：MBx-IF 下载表把 AP214 e3 对应到 `{1 0 10303 214 1 1 1 1}`；[STEP Tools AP214 使用说明](https://www.steptools.com/docs/stp_aim/notes_ap214.html)给第三版 `{1 0 10303 214 3 1 1}`，也分别给第一、二版。当前文件自己的符号注释使用 `version(3)`。三份证据并不一致；HTTP 日期或 ZIP 文件名不能消除这个冲突。

P0 的设计处理：保留 `FILE_SCHEMA` 全字符串和所有 OID 分量；分别记录“家族识别”“所选结构 profile”“edition 依据和可信状态”。裸 `AUTOMOTIVE_DESIGN` 只能可靠识别家族；历史尾部额外 `1` 的 OID 不能静默截短。当前证据不足以把每个实际 OID 变体认证成唯一版次。`AUTOMOTIVE_DESIGN_CC2` 是已见历史名称，STEP Tools 说明来自旧数据；不能由其中的 CC2 推导 Part 21 的 complex 编码级别。

## 3. 对当前 P0 清单有实际影响的字段差异

下表行号均针对上述 hash 的本地 [AP214E3_2010.exp](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap214/AP214E3_2010.exp)，不是会随网站合并版更新的实体页。当前[实体核对](p0-entity-source-audit.md)和[单位核对](p0-unit-source-audit.md)仍可作为合并字典的结构研究，遇到本表差异必须按明确 profile 区分。

| 项目 | 此份 AP214 文件的结构 | P0 后果 |
|---|---|---|
| `ADVANCED_FACE`，1312–1371 | 继承 `FACE_SURFACE`；本体没有显式属性重声明；曲面、边、顶点和 bound 限制写在 10 条 WHERE 中 | simple 仍为 4 槽，几何槽仍显式传引用；不能要求旧版存在当前合并字典的 `advanced_face_geometry_select` 类型 |
| `ADVANCED_BREP_SHAPE_REPRESENTATION`，1283–1310 | 无 items 专用 SELECT 重声明；6 条 WHERE 限制 items、实体面及映射等 | simple 仍为 3 槽；先读继承属性，再核对这个 profile 的 WHERE，不能只套当前专用 SELECT |
| `MANIFOLD_SURFACE_SHAPE_REPRESENTATION`，6732 起 | 无 items 专用 SELECT 重声明；使用 WHERE 和 `msf_*_check` 函数 | simple 仍为 3 槽；允许的闭合 shell 仍保留 surface-model 角色，不改成实体 |
| `CONVERSION_BASED_UNIT`，3477–3485 | `named_unit.dimensions` **重声明为 DERIVE**，由 conversion factor 的单位推导 | 此 profile 的 complex `NAMED_UNIT` 对应槽用 `*`；不能使用合并字典“该处显式 `#dim`”的结论 |
| `SI_UNIT`，10165–10176 | 同样 DERIVE `named_unit.dimensions`；prefix optional、name 必需 | 保持 complex `NAMED_UNIT(*)`；`SI_UNIT($,.RADIAN.)` 的 `$` 是 prefix，不能和 derived 槽混用 |
| `year_number`，1129–1130 | 仅为 `INTEGER`，没有当前合并字典中的 `>1581` WHERE | 不能把合并版新增的年份限制称为此 AP214 文件的规则；产品对年份一致性的检查另列 |
| `UNCERTAINTY_MEASURE_WITH_UNIT`，11471–11478 | name 必需，description **optional**；WHERE 调用 `valid_measure_value` | 保留 `$` description；零／负数的数值 uncertainty 与缺失 uncertainty 分开诊断 |
| `ORIENTED_EDGE`，7698–7710 | 两端点重声明为 DERIVE，禁止嵌套 ORIENTED_EDGE | 5 个 simple 槽中的两个端点仍须 `*`；此项与现有 P0 清单一致 |
| `PATH`／`EDGE_LOOP`，7865–7872／4644–4650 | `LIST[1:?] OF UNIQUE oriented_edge`；path 首尾相接函数加 loop 闭合 WHERE | 逐 use 验证身份、次序和闭合；不能把不同 OE 对同一底边的引用当成列表重复 |
| `FACE`，5050–5058 | 禁混不兼容 loop 类型；显式外环数量最多 1 | “首切片必须有 1 个显式外环”仍是 P0 能力限制，不是这份 schema 的普遍必需条件 |
| `GEOMETRIC_REPRESENTATION_CONTEXT`，5603–5606；`dimension_count`，457–460 | 字段为正整数类型；AP 层另有只许 2／3 的全局 RULE | 不要把实体自身类型、AP 全局限制和 P0 仅 3D 的限制合成一个错误原因 |
| `PRODUCT_DEFINITION_FORMATION`，9028–9034 | description optional；唯一键为 `(id,of_product)` | 同产品版本键要检查；不复制某些 reader 对 optional description 的过强读取要求 |

单位的几何消费规则没有因此改变：内部 mm/rad；方向比值不乘长度因子；LINE 的参数和 PLANE 的参数映射分别保存。**变化的是原始 Part 21 属性布局和 profile 验证依据**。转换链必须沿显式选择的结构版本读取，不能用一个合并布局覆盖所有 AP。

## 4. 首切片相关的全局 RULE 和跨实体约束

以下是对目前 P0 类型图有直接影响的首批闭包清单，不等于“已完整枚举 AP214 所有约束”。行号仍定位同一原始文件；每条都尚未执行。

| 约束与行号 | 原文表达的范围 | P0 设计影响 |
|---|---|---|
| `application_protocol_definition_required`，14853–14859 | 至少存在一个 application context，其反向 APD 中有 schema 字段等于 **`AUTOMOTIVE_DESIGN_LF`** 的记录 | 该字面量与 HEADER 的 `AUTOMOTIVE_DESIGN` 不同；不是“每个 context 恰好一个 APD”。不能重写源 APD 去凑通过；把文件自带 RULE 与常见 writer 的差异列为证据问题 |
| `compatible_dimension`，14877–14886 | CARTESIAN_POINT／DIRECTION 通过 `item_in_context` 的使用关系，对所有相关几何 context 检查分量数 | 只从一个面找到单位不够；共享几何被多个 context 使用时必须检查其所有有效解释，不能依赖首次遍历结果 |
| `dimensionality_is_two_or_three`，15225–15230 | 每个几何 context 的维数只能 2 或 3 | P0 的必要 3D context 仍更窄；合法 2D geometry 不应诊断成通用 schema 语法错误 |
| `global_length_and_angle_units_2d_or_3d`，15360–15368 | 每个全局单位 context 至少两个 unit，且恰好 1 个 LENGTH_UNIT、1 个 PLANE_ANGLE_UNIT | 即使平面切片没有角度数值也不能据此删去 angle unit；该 RULE 本身没有强制 solid-angle unit |
| `consistent_uncertainty` wr1，14928–14933 | 同一全局 uncertainty context 内，name 不得重复 | SET 身份不同仍可能触犯同名约束；不能静默选最后一个。wr2/wr3 涉另外两种本切片尚不接受的 uncertainty 表达 |
| `dependent_instantiable_measure_with_unit`，15086–15090；`dependent_instantiable_named_unit`，15092–15095 | 每个对应实例至少被引用一次 | 不能把孤立单位／measure 的 global RULE 失败伪装成几何丢失；正样件应让单位和 conversion measure 有真实引用 |
| `product_requires_category`，15485–15490 | 每个 product 至少被一个 PRODUCT_RELATED_PRODUCT_CATEGORY 引用 | 分类不能无条件归为“已证无影响，可忽略”；它影响此 schema 的符合性 |
| `restrict_product_category_for_product`，17654–17666 | 每个 product 必须具备恰好一个受该规则认可的分类名；其他分类的存在也不能替代它 | 单纯发一个任意名字的分类不满足规则；不要把业务文字统一改名 |
| `product_requires_id_owner`，15492–15503 | 对被归入 `part`／`tool`／`raw material` 的 product，指定 role 名为 `id owner` 的组织分配或人员组织分配合计必须恰好 1 个 | 常用 part 正样件若声称满足本文件 AP 规则，需扩大产品元数据闭包；不是只增加 category 一条记录即可 |
| `product_requires_version`，15505–15509 | 每个 product 至少有一个 formation | 所选 direct P0 产品链已经需要 formation，但仍须区分全文件规则与所选根链规则 |
| `restrict_product_definition_context_for_product`，17682–17720 | 根据 definition context 的 name，约束 product 分类；含 `part definition` 等分支 | 不能为绕开 id-owner 约束而把几何零件伪装成无关分类／生命周期上下文 |
| `subtype_exclusiveness_representation`，18421–18443；`subtype_exclusiveness_shape_representation_geometry`，18525–18544 | 使用 `type_check_function(...,3)` 排除多个互斥 subtype 同时成立 | criterion 3 在函数 14239–14255 中是“至多 1”，不是“必须恰好 1”；不能据此宣称 generic SHAPE_REPRESENTATION 自身 schema 非法 |
| `subtype_exclusiveness_representation_item`，18445–18452 | 对部分 representation-item subtype 作互斥检查 | 类型图应保留 complex 合成成员，不能读到第一个成员后忽略其他成员 |
| `restrict_version_assignment_for_shape_representation`，18102–18108 | 同一个 shape representation 的 `version` 标识分配最多 1 个 | 增加这种元数据时触发；不应给目前无此记录的最小样件凭空补一个 version 分配 |

尚须同时纳入继承的局部约束：`geometric_representation_item` 的 context 类型检查（5607–5622）依赖 `using_representations`（14304 起）；`item_in_context`（13546–13566）本身会沿引用关系上溯；MEASURE_WITH_UNIT 的 `valid_units`（6937–6950、14476 起）核对值类型与量纲。P0 需要有界、可诊断的引用闭包；不能把潜在循环变成无限递归，也不能把检查函数的 UNKNOWN 当 TRUE。

`ADVANCED_FACE`、ABSR、MSSR 的 WHERE 均须保持为本 profile 的约束。Direct LINE／PLANE 只是可按能力裁剪的分支，不使其余句子自动删除。首切片选择不支持的映射、曲线包装、void、oriented shell 等仍按影响拒绝；不能只抽出内部 LINE 后丢掉外层语义。

## 5. 产品最小闭包需要补哪些记录

如果 P0 后续要构造一个分类为 `part`、保留真实产品语义、并用上述 AP214 规则作符合性控制的正样件，当前 direct 产品链之外，最短的组织分配路径需要下列 **3 类记录**。它们是有来源的设计候选，尚未加入已运行样件，也不表示采用了整个 AP214 管理元数据范围。

| 记录和 simple 字段 | 原文依据 | 必要关系 |
|---|---|---|
| `ORGANIZATION(id,name,description)` | 7625–7629；id、description optional | 作为真实来源组织记录；不得借用示例文字伪造现实文件来源 |
| `ORGANIZATION_ROLE(name)` | 7643–7650；description 是派生，非新增槽 | 被分配记录引用，name 在这个规则分支为 `id owner` |
| `APPLIED_ORGANIZATION_ASSIGNMENT(assigned_organization,role,items)` | 基类 7631–7634，子类 1836–1848；items 为非空 SET；`organization_item` 746 起包含 product | items 引用这个 product；同一 product 的人员／组织 owner 分配总数恰好 1 |

`PRODUCT_RELATED_PRODUCT_CATEGORY(name,description,products)` 本来已在 P0 的已知元数据清单，字段来自 8908–8916、9161–9164；现在要把“其 AP214 符合性影响已确认”记录下来。若不启用完整 AP 规则验证，报告应明示已验证的是几何／产品导入子集，不应用“AP214 全符合”措辞。

组织分配将触发新的 dependent／role 约束，必须按真实实体闭包继续核对，而不是仅检查本节 3 个 simple 布局后宣布完整有效。此报告没有选择人员分配路径，也未为规避规则更换 product 分类。

## 6. 追加的设计验收用例候选

以下编号只属于本来源审计，均为 **Defined / NotRun**；不是主P0目录已新增通过项。后续主目录扩至63条，审计建议与主目录可以重叠，不重复计数；实际fixture数量、校验器和支持资格仍须由执行记录证明。

| ID | 输入／比较 | 需要的独立判据 |
|---|---|---|
| A214-01 | 同一 conversion unit 分别采用这个 profile 的 derived `*`、当前合并布局的显式 dimensions | 前者按本文件字段布局；后者不能仅因合并字典接受就称本 profile 符合；几何长度真值另验 |
| A214-02 | SI／conversion unit 两条链得到同一 mm 几何 | 字段解释与 conversion 物理值分别通过；不由单位名猜 factor |
| A214-03 | APD 字面量分别为 `AUTOMOTIVE_DESIGN_LF`、`AUTOMOTIVE_DESIGN`，HEADER 相同 | 文件提供的 global RULE 结果不同；来源差异显式记录，不自动改字符串；几何导入资格与完整 AP 符合性分栏 |
| A214-04 | 裸 schema 名、两种 e3 候选 OID、带尾部额外分量的历史变体 | 保留原文；家族可识别不等于 edition 已证；不截断 OID |
| A214-05 | part 缺 category、只有任意未知名 category、正确 part category | 分别验证 category 规则；不能以面／体积正确覆盖失败 |
| A214-06 | 正确 part category 下 owner 分配为 0／1／2 个 | `product_requires_id_owner` 要求恰好 1；跨两种分配类型累计，不能分别各取 1 |
| A214-07 | 一个全局 uncertainty context 中两个不同实例但相同 name | 必须识别同名冲突；不能 SET 去重成一个并遗漏来源 |
| A214-08 | geometry context 维数 1／2／3，及 geometry 被不兼容 context 共享 | 区分字段类型、AP RULE、P0 3D 能力、引用使用链；拒绝不能凭遍历顺序选择单位 |
| A214-09 | year 为 1500 的仅字段检查对照；normal positive fixture 仍使用有证据年份 | 此文件 year 类型只有 INTEGER；不能套合并字典的年份门槛。产品一致性可以另拒绝，但原因不可冒充原文 WHERE |
| A214-10 | generic SHAPE_REPRESENTATION 与同时组合两个互斥 geometry subtype | 前者不因 exclusiveness 规则被误称 schema 非法；后者应触犯互斥规则。前者仍可因 P0 不支持该选根路径而 Unsupported |

这些案例的 schema 判据来自冻结的声明、集合／计数条件和字段类型；几何判据继续来自[独立解析真值](p0-planar-cases.md)，二者互不替代。OCCT 或任一独立 reader 只作差异证据，不作为这份文件的唯一规则裁判。

## 7. 可关闭的缺口与仍待验证事项

可关闭的是“当前官方发布入口找不到、没有任何可冻结 AP214 e3 原始 EXPRESS 文件”这一资料缺口。已经有官方链接、原始字节、hash、获取记录和可复查行号，也找到会改变 P0 字段／元数据闭包的具体差异。

仍不能关闭以下门槛：

1. **IS 逐字身份和 OID 冲突**：文件的 DIS／2009 自述、官方页面 e3／2010 标签、两种数字 OID 之间仍需独立正式证据；本轮没有把任一冲突变体认证成唯一 edition。
2. **APD 规则字面量**：原文要求 `AUTOMOTIVE_DESIGN_LF` 的条件应与正式出版 schema、勘误及代表性正式 writer 对照；不能自行把它当笔误修掉。
3. **完整约束闭包**：首切片已列出直接相关入口，新增组织／分类后仍须固定所有触发的 SELECT、WHERE、INVERSE、UNIQUE、全局 RULE 和函数语义，包括空集合、UNKNOWN、逆向引用与跨上下文使用。
4. **独立符合性证据**：尚未选择／运行一个与本文件配套的独立 EXPRESS 规则校验器；只有手工阅读，既不是 parser 验证，也不是样件符合性验证。
5. **原生构建证据**：没有因此证明 SMLib 的 16 种方向组合、孔、共享边、单位映射、失败清理全部通过；沿用原 P0 的 NotRun 状态。
6. **再分发范围**：本轮仅保存研究文件；如果后续要把 schema 或生成代码打包进产品，再确认其具体许可和来源，不能借用无关开源项目的许可结论。

完成这些资料核对后，仍由用户授权决定何时进入实现。本报告没有新增 loader、解析器、schema 生成器、测试程序或 STEP 样件。
