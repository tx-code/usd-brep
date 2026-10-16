# P0 AP242 来源、版次和必要规则补证

状态：2026-10-15，设计研究；没有编写读取器、测试程序或 STEP 样件，没有编译、调用 CAD 读取器或读取新的模型。本文补充 [P0 主契约](p0-planar-slice.md)，不修改已确认规格 0.1，也不授予 AP242 符合性或运行时导入资格。

## 1. 本轮取得了什么

旧 CAx-IF 地址失效后，找到现行官方 [MBX-IF EXPRESS schemas 入口](https://www.mbx-if.org/home/mbx/resources/express-schemas/)。页面按版次列出 longform、schema identifier 和配套 HTML；其中 AP242 第一、二、三版 ZIP 均取得 HTTP 200，原 ZIP 和未改动解压文件保存在本机研究目录。没有从商业安装包提取，也没有依赖不明镜像。

**取得的是 MBX-IF 官方发布的实现 schema 包，不能一概称为 ISO 出版时未修改原文。** 第一版文件明确声明若干 bug patch，第二版包内文件名保留 `dis2`。原始字节、文件自述和官方页面标签分别记录；不为简化登记删除这些差异。

| 官方页面标签 | 官方标识 OID | 包内文件及自述 | 本轮来源等级 |
|---|---|---|---|
| AP242 Ed.1，2014 | `{1 0 10303 442 1 1 4}` | `ap242_is_mim_lf_v1.36.exp`；CVS 1.36，2014-05-22；WG12 N8324；声明为若干 bug 打补丁，并注释了 `link_motion_relationship.WR1` | 官方实现发布稿，已知经过修改；不是无修改 ISO 原版证明 |
| AP242 Ed.2，页面年份 2019 | `{1 0 10303 442 3 1 4}` | `ap242ed2_dis2_mim_lf_v1.101.exp`；CVS 1.101，2019-07-01；WG12 N10517，替代 N10384 | 官方实现发布稿；`dis2` 文件名与最终出版物之间的逐字节关系未核准 |
| AP242 Ed.3，2022 | `{1 0 10303 442 4 1 4}` | `ap242ed3_mim_lf_v1.152.exp`；STEPmod CVS 1.152，2021-09-24；WG12 N10962，替代 N10517 | 官方实现发布稿；与 ISO electronic insert 的逐字节关系仍待核 |

三个文件的 EXPRESS `SCHEMA` 名均为 `ap242_managed_model_based_3d_engineering_mim_lf`，大小写差异按 EXPRESS 标识规则处理，**OID 的中间分量不是 AP 的 edition 数字**。既有 HEADER 画像中的 `{...442 3 1 4}` 对应官方表中的 AP242 Ed.2，不能据数字 3 标成 Ed.3。只有名字而无 OID 的文件，仍不能据本轮资料唯一确定上述三种版次。

第二版的页面年份、文件修订日期、AP 出版年份是不同字段。STEP Tools 的 [AP242 说明](https://www.steptools.com/docs/stp_aim/notes_ap242.html)将 AP 第二版出版列为 2020；不据页面 2019 或 EXP 修订日期改写文件中的年份或推断是否有效。

## 2. 不可变下载清单

本地目录：`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap242/`。`downloads.ndjson` 记录实际 UTC、HTTP 状态、原路径、字节数和 ZIP hash。原 ZIP 安全核对目标路径后解压；EXP 文件未重排、改行尾或改名。官方实现包不是 Git 项目 checkout，项目 commit 为“不适用”；文件自述 CVS 修订另行保留，不冒充 Git commit。

| 材料 | 原始字节数 | SHA256 |
|---|---:|---|
| [Ed.1 ZIP](https://www.mbx-if.org/home/wp-content/uploads/2024/07/ap242_is_mim_lf_v1.36.zip) | 195877 | `1b1d4a984521916b54fd25ed35cf2d87ea887ca12066558579c1025aa315c32e` |
| Ed.1 EXP | 1762651 | `fd1ecaf343763d2e1b347c95675f8e9ce408ecd2aea0a4034f8bd92bbbd42898` |
| [Ed.2 ZIP](https://www.mbx-if.org/home/wp-content/uploads/2024/07/ap242ed2_mim_lf_v1.101.zip) | 239599 | `c49efcf85172276dd769cf899f7543bcc1ef4bc003cb5ec26329ab6439019cdc` |
| Ed.2 EXP | 2343934 | `ff51beb1be6e78faebd52338da342fd1059da2840beaa7cfd08b4df8e7c5a410` |
| [Ed.3 ZIP](https://www.mbx-if.org/home/wp-content/uploads/2024/07/ap242ed3_mim_lf_v1.152.zip) | 247283 | `9c3deeca68f67e99d184df90eaeda8816971800a5f75384363076cd05c90450a` |
| Ed.3 EXP | 2392527 | `4b43af3041efa5532f33b0d0d99cac48a162c63b25405e1200576294efcf044f` |

三次成功下载 UTC 分别为 `2026-10-15T13:49:16.8752761Z`、`13:49:19.4301349Z`、`13:49:21.9154192Z`。官方索引页面亦在本地保留快照 `mbx-express-schemas.html`；网页后续更新不改变本轮冻结文件。

另核实 [ISO SMRL 官方目录](https://standards.iso.org/iso/10303/smrl/)可访问：v8/v9 ZIP 的 HEAD 返回 200，公布大小分别为 10680868/18104130 字节。一次有界获取未完成，随后 curl 的 25 秒上限只收到 v8 的 2224081 字节；该文件保存为 `smrlv8.zip.partial`，没有解压或当作 schema 证据。`iso-retrieval-attempts.json` 单独记录这些失败/未完成尝试。三个按现行目录结构推测的 `-442/ed-1|3|4/tech/express/mim_lf.exp` URL 返回 404；不据此认定 ISO 原文不存在。本轮停止继续获取，ISO 与 MBX 字节对比保持未证。

## 3. 本切片必须分版处理的差异

| 项目 | Ed.1 v1.36 | Ed.2 v1.101 | Ed.3 v1.152 | 对 P0 的约束 |
|---|---|---|---|---|
| APD 必需 RULE | `application_protocol_definition_required` | `ap242_application_protocol_definition_required` | 同 Ed.2 | 规则名字与字符串常量均不能从 merged 字典直接复制 |
| APD 的 schema name 比较值 | `ap242_managed_model_based_3d_engineering` | `ap242_managed_model_based_3d_engineering_mim_lf` | 同 Ed.2 | Ed.1 此处不含 `_mim_lf`，与文件 `SCHEMA` 名不同；保留源声明并按已选实现 profile 检查 |
| shape subtype RULE | `subtype_mandatory_shape_representation`，针对所有 shape_representation | 同名 RULE | `ap242_subtype_mandatory_shape_representation`，增加 application_context/APD 条件及常量集 | 不能把最新版 guard 移植到旧版，或声称 generic shape representation 永远非法 |
| 维数 RULE | 点/方向经 `item_in_context` 对相关 geometric context 检查 | 调用 `check_geometric_dimension` | 调用同函数，显式允许三个输入集合全空的情形 | 本片有几何时仍须独立追踪 representation context，不能全文件只检查维数一次 |
| conversion-based dimensions | 继承显式 `named_unit.dimensions`，WHERE 验证与换算因子单位一致 | 同 Ed.1 | 同 Ed.1 | 三版示意均使用 `NAMED_UNIT(#dim)`；不能与 AP214 的派生槽规则合并 |

APD 必需 RULE 的实际基数是“存在至少一个 application_context，其反向关联 APD 满足相应 schema name”，不是“每个 context 恰好一个 APD”。P0 对所选根的协议可解释性仍可更严格，但要标为项目的根解释政策，不能误写成该 RULE 原文要求。该规则没有替 P0 验证 `status`、出版年份、OID、每个根的单位或整个 AP 交换结构。

三个 shape subtype RULE 都存在若干允许例外，例如只有 placement 的表示以及特定 shape-aspect 引用情形；本片仍要求直接 ABSR/MSSR 与明确实体/表面模型角色。即使某 generic 表示满足 schema 例外，也不自动获得本片转换能力。

## 4. 已核字段与单位规则

下表是本轮三份冻结文件的直接核对结果；不能据此扩展成所有 AP 或所有未来版次都相同。

| 字段/规则 | 三版结果 | 对已有清单或用例的落实 |
|---|---|---|
| `PRODUCT.description` | OPTIONAL text | `$` 可为合法缺省；空字符串也不因此违反 required 规则 |
| `PRODUCT_DEFINITION.description` | OPTIONAL text | 不因 OCCT 读取实现采用 ReadString 就改成 required |
| `PRODUCT_DEFINITION_FORMATION.description` | OPTIONAL text | formation 的 `$` 正例继续成立 |
| `PROPERTY_DEFINITION.description`，PDS 继承 | OPTIONAL text | PDS 三槽仍为 name/description/definition，第二槽允许 `$` |
| `UNCERTAINTY_MEASURE_WITH_UNIT.description` | OPTIONAL text；name 是 required label | 区分缺少必需字符串、合法 `$` 和空字符串，不从名字猜内核工作容差 |
| `PRODUCT_DEFINITION_FORMATION` UNIQUE | `(id,of_product)` | 仅 id 相同但 of_product 不同，不能据此合并或拒绝；同组合重复须核查 |
| `PRODUCT_DEFINITION_SHAPE` UNIQUE | 继承的 definition | 两个 PDS 指向同一 definition 可能违反 UNIQUE，不能只查 entity ID 唯一 |
| `GLOBAL_UNIT_ASSIGNED_CONTEXT.units` | SET [1:?] OF unit | 成员顺序没有单位角色语义；空 SET 非法 |
| `GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT.uncertainty` | SET [1:?] OF uncertainty_measure_with_unit | constituent 缺席与出现空 SET 分开 |
| `consistent_uncertainty.WR1` | 同一 GUUNC 内不能有两个 uncertainty 使用相同 name | 不同源实体、相同 name 即可能冲突；不能仅查 SET 重复实体 ID |
| `SI_UNIT` | prefix OPTIONAL；重声明 dimensions 为 DERIVE | SI unit 复合仍为 `NAMED_UNIT(*)`；不因 conversion unit 使用引用就一律改成引用 |
| `CONVERSION_BASED_UNIT` | 继承显式 dimensions；WHERE 与 conversion_factor 单位的派生量纲比较 | AP242 三版用显式 dimensions 引用；保留正有限换算因子、无环和目标数值安全的项目政策 |

已检查三份完整 RULE 声明区，未发现 AP214 的 `global_length_and_angle_units_2d_or_3d`，也未发现直接引用 `unit/units/LENGTH_UNIT/PLANE_ANGLE_UNIT` 的全局 RULE。**不能把 AP214 的“恰一长度、恰一平面角”规则当 AP242 这三份实现稿的同名规则。** 这不取消 unit 子型的局部 WHERE、measure 的 `valid_units`，也不取消本项目对所选精确几何必需长度单位的拒绝政策。首片正例仍可明确给出 mm/rad/sr，但这是一组选择清楚的正例，不证明 AP242 普遍强制三单位。

`consistent_uncertainty` 还有 uncertainty_assigned_representation 和 qualified_representation_item 分支；所选内容或有影响的附加数据出现这些分支时，要执行对应检查或准确拒绝，不能只看本片 GUUNC 分支就称整个 RULE 已实现。

## 5. 产品、形状及规则闭包的边界

本片的单零件根链与三版显式属性相容：PRODUCT/formation/PRODUCT_DEFINITION、PDS、SDR、具体 ABSR/MSSR、geometry context/units。与合并字典相同的字段数量，并不足以证明合法性。至少还需保留以下适用性判定：

- ABSR 的 items 白名单、至少一个 solid/mapped item、solid 各壳面为 ADVANCED_FACE、外壳不能为 ORIENTED_CLOSED_SHELL；映射和 void 分支仍属 P0 不支持范围。
- MSSR 的 items 白名单、至少一个 SBSM/mapped item，以及 shell、face、边界曲线、顶点类型的分支检查；本片仅 direct LINE/PLANE 不会绕过其余源约束。
- `APPLICATION_CONTEXT.context_elements` 是 INVERSE SET [1:?]，没有独立 Part21 槽；不能漏掉它的约束，也不能给它补写 `$`。
- `valid_product_and_manufacturing_information_view_contexts` 会按 application/life_cycle_stage 字符串触发不同 view context 的关系及文档要求。普通几何 view、PMI view、process/material/feature view 不能因都是 context 字符串而一律当无影响 metadata。
- `restrict_assembly_category`、`restrict_collection_category`、`restrict_part_occurrence_category` 等分类 RULE 为条件规则。没有相关触发结构时须证明不适用；出现结构时不得只保留 category 文字、跳过关系。Ed.2/Ed.3 另有 support-structure category 检查，不能回填到 Ed.1 后称其原有规则。
- `subtype_exclusiveness_representation_item`、依赖实例化检查和被上述 WHERE/RULE 调用的函数，尚未形成完整的可执行闭包清单。未知有影响的附加 subtype/relationship 继续准确拒绝。

这里区分三件事：表中 RULE 的原义通常针对全文件的相应实例集合；转换必要检查针对所选依赖闭包及已证明会影响它的内容；完整 AP 符合性是单独的资格。本项目不因本表列出某条管理 RULE，就自动要求所有未选择零件的管理信息全部合规。反之，未证明影响边界时，也不能把该 RULE 静默标成不适用并冻结 profile。

原文定位以本地冻结 EXP 的实体/RULE 名搜索为准。建议审阅入口：Ed.1 的 conversion unit 约 6812 行、APD RULE 22240 行、shape RULE 23000 行；Ed.2 分别约 10638、48626、49596 行；Ed.3 分别约 11004、49424、49437 行。排版行号只帮助导航，判定身份使用文件 hash 和定义名。

## 6. 门槛结论

| 门槛 | 本轮状态 |
|---|---|
| AP242 三版有可追溯、可冻结的官方实现 schema | 已取得，原 ZIP/EXP hash、官方标签和内部修订均已记录 |
| 不再依赖 merged 字典猜上述字段、OID、APD/shape RULE 版差 | 上表涉及条目已补证；旧合并表须按此分 profile 修订 |
| ISO 出版版原始 electronic insert 与 MBX 三版字节相同/差异 | 尚未完成；不能把官方实现稿来源升级成未修改原版 |
| P0 精确 profile 的全部必要 SELECT/WHERE/UNIQUE/INVERSE/RULE 与函数闭包 | 尚未完成；本轮仅锁定重点差异与触发边界 |
| Part21 complex 映射签名、完整 schema 约束执行 | 未实现、未运行 |
| 三版 HEADER/OID/APD 联合 registry 与无 OID 判定政策 | 事实输入已补证，仍须形成明确配置，不能猜版 |
| 数学真值、目标构建、失败清理及资源/取消验收 | 继续 Defined/NotRun，本轮没有运行证据 |

因此可关闭的是“AP242 三版官方实现材料完全未取得”这一来源缺口；不能关闭 P0 总门槛、ISO 完整符合性、解析器或 SMLib 构建路径的执行验收。
