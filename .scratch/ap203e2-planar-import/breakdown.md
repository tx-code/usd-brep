# AP203e2首个profile：实施票拆分草案

状态：Approved／已按用户确认发布为12张本地独立票，2026-10-15。依据用户显式调用的[to-tickets技能](C:/Users/tangx/.codex/skills/to-tickets/SKILL.md)。下文保留原审阅拆分内容；正式状态和依赖见[票组索引](README.md)。**用户仍明确暂不编码，确认及建票不改变任何用例的NotRun状态。**

## 范围与跟踪方式

这组票聚焦已经完成静态设计的AP203e2 N2635平面／盒体profile：把49项样件设计落成可验证输入，经独立自研源读取和语义检查，形成owning只读模型快照与Import report。首个内部入口为C++及可重复调用的inspect／导入验证入口；GUI、公开SM_API/Python稳定化、应用worker和后续AP/几何/修复能力属于总体规格后续增量。

总体依据：[已确认规格0.1](../../docs/step-import-repair-spec.md)、[AP203e2首profile](../../docs/step-import/ap203e2-planar-profile.md)、[49项样件设计](../../docs/step-import/ap203e2-sample-design.md)、[36项反例诊断](../../docs/step-import/ap203e2-sample-cases.md)、[领域术语](../../GLOSSARY.md)。这些来源决定行为和验收；票体使用术语与用例ID，不绑定易过时的实现文件路径。

既有tracker为[本地Markdown跟踪](../../docs/wayfinder/README.md)。拆分确认后，依to-tickets约定在本功能的issues目录一票一文件、按依赖编号，并以文本Blocked by链接表达阻塞关系；triage状态使用技能规定的ready-for-agent。已关闭的Wayfinder设计票和父地图保持原状态，不重开、不改写，不创建远端issue或PR。

**执行边界：** ready-for-agent仅表示票的技术拆分可领取；用户“暂不编码”的约束优先。确认拆分只授权正式建本地票；开始实现须另外得到明确指令。下文“无阻塞”只指技术依赖为空，不代表本轮开工。

## 拆分原则

每票交付一条能运行或核验的用户行为：输入／源证据／模型或诊断／独立验收连通。inspect是有独立价值的可验证交付，不把lexer、实体类、中立模型、构建adapter各拆成只能等待下一层才能使用的水平票。新loader采用既定三个模块；现有证据没有要求先作广泛内核或USD重构，构建封装随首个owning平面路径交付。

修复保持关闭；产品读取链路完全自研。所有成功均绑定输入hash、profile、质量配置和检查版本；源规范化、派生UV和工作tol有记录。任一未证能力准确Unsupported/NotRun，不能为了使票通过扩大支持声明。该票组最后只授予实际验证的首profile资格，不自动把总体P1/P2全部AP、载体、共享片或建模项目标为通过。

## 提案概览

| 编号 | 标题 | Blocked by | 独立交付 |
|---|---|---|---|
| 01 | 从样件设计得到可重复的独立验收证据 | 无 | 冻结真实输入、精确真值和单mutation谱系，能逐case出证据 |
| 02 | 最小STEP可inspect，文件错误准确定位 | 01 | 对基础文件保留完整源记录/位置；破损、重复ID和悬空引用有明确报告 |
| 03 | 单零件、类型与SI单位的源语义可检查 | 02 | 找到唯一产品/表示/context，给出规范单位与源映射，越界不猜 |
| 04 | 平面的必要规则与有向区域可独立验收 | 03 | inspect对最小平面给出完整准入证据或可复核反证 |
| 05 | 从STEP交付owning只读开放平面 | 04 | 返回有来源与报告的精确平面快照，普通失败没有部分模型 |
| 06 | 带孔面与负UV原点保持源区域 | 05 | 孔、参数原点、查询与UV/window在完整导入中正确 |
| 07 | 从盒体STEP保留共享边和材料实体 | 05 | 共享拓扑、外侧方向、精确查询及6000体积贯通 |
| 08 | 同形闭合表面保留surface-model角色 | 07 | 盒体与同形表面模型有相同边界、不同材料资格 |
| 09 | 单位重述与conversion链保持物理几何 | 07 | 厘米、英寸的scale/参数/维数正确，零负因子准确拒绝 |
| 10 | 可选来源声明与旧AP203类型完整保留 | 05 | uncertainty/category/sr及旧context/source字段不丢失、不越权改精度 |
| 11 | 生命周期、取消与限额不留下部分结果 | 05 | 普通失败/合作取消/资源拒绝及快照与副本生命周期可重复核验 |
| 12 | 汇总49项端到端证据并限定首profile资格 | 06、08、09、10、11 | 组合可选分支和单位环，49项形成完整且不混代的结果 |

## 01：从样件设计得到可重复的独立验收证据

**What to build:** 执行者能按样件ID取得固定输入和独立真值，看到正例及单缺陷反例的版本、谱系和预计诊断；尚未实现的导入行为明确NotRun。

**Blocked by:** 无技术依赖。

**Proposed acceptance criteria:**

- [ ] 13正例、36反例各有唯一ID、固定输入字节/hash、父例与精确mutation；记录数/引用与设计对账，反例不混入额外编辑。
- [ ] 精确坐标、方向、面积/体积/距离和身份真值由独立数学定义取得；不从被测loader、native CreateBox或OCCT输出回填expected。
- [ ] 样件、Q、诊断合同及oracle的版本、误差界和来源可追溯；未运行不写零误差、Passed或零泄漏。
- [ ] 一次case选择可产出可读证据；报告能区分设计期望、输入对账和真正的被测执行。后续票复用该调用路径。

## 02：最小STEP可inspect，文件错误准确定位

**What to build:** 从基础平面STEP文件得到完整不可变Source snapshot及inspect记录；遇到文件/引用错误，调用者得到明确失败和原始位置。

**Blocked by:** 01「从样件设计得到可重复的独立验收证据」。

**Proposed acceptance criteria:**

- [ ] 自研路径读取首profile的完整HEADER/DATA、simple和已准入complex原值；保留字符串、typed values、聚集、`$`/`*`、ID/引用与位置，不建立native模型。
- [ ] 基础51记录输入可inspect；N02/N03等索引/引用错误准确拒绝，既有部分记录不冒充完整源成功。
- [ ] 前向引用和记录乱序不改变已支持范围的原始图；不按最大ID分配无限容器，不读第一个shape就停止。
- [ ] 输入捕获后修改原路径不会改写Source snapshot；输入/记录/报告的已配置基本限额生效，未支持载体或编码明确拒绝。
- [ ] 本票只声明已实现载体范围，不能以一个ASCII单DATA样件宣布全部Part21或总体P1通过。

## 03：单零件、类型与SI单位的源语义可检查

**What to build:** inspect能从源文档解析唯一零件定义、具体表示和SI单位context，给出可追踪的中立解释；必需字段、类型或范围不成立时不猜默认值。

**Blocked by:** 02「最小STEP可inspect，文件错误准确定位」。

**Proposed acceptance criteria:**

- [ ] 完整名称/OID明确绑定N2635；APD声明与HEADER分开保留，不因年份或家族名覆盖矛盾。
- [ ] 产品/PDS/SDR/表示/context、祖先及constituent身份完整；源图没有native指针，也不另复制一份全文件类型对象图。
- [ ] 基础SI长度/rad、复合局部字段和derived槽按确切profile解释；方向比值、物理长度、LINE参数和PLANE参数分开。
- [ ] N01、N04—N09、N14/N15及本票涉及的范围反例给出正确逻辑责任；未知/未支持必需内容不被跳过。
- [ ] 反向索引覆盖完整已捕获范围，后续规则检查不会把未遍历内容伪造为空集合；未完成语义在inspect中明确标记。

## 04：平面的必要规则与有向区域可独立验收

**What to build:** 通过inspect即可判定最小平面的源规则、定义一致性和区域是否满足本增量的导入前提，给出可复核报告而不依赖native构建。

**Blocked by:** 03「单零件、类型与SI单位的源语义可检查」。

**Proposed acceptance criteria:**

- [ ] 实现相关祖先/TYPE的WHERE、UNIQUE/INVERSE，以及47条RULE在当前能力/封闭守卫下的适用性；5必查/1条件/41守卫的结论有输入证据。
- [ ] SDR/PDR、EDGE_LOOP/LOOP和间接RI使用者不漏计；QUERY规定UNKNOWN与未实现/未判定严格区分，不私修原schema。
- [ ] 源LINE/PLANE、四层sense、环身份、区域及误差证据独立检查；8mm、0.001mm、零LINE、PATH与首尾不同责任有对应反例。
- [ ] 单次mutation的首个阻断与后果按目录报告，不要求所有后果同时输出，也不把Unsupported伪报源非法。
- [ ] 只有已支持的形状/语义获得这一层证据，其他case保持准确Unsupported/NotRun；所有源阻断均未开始native构建。

## 05：从STEP交付owning只读开放平面

**What to build:** 对最小开放平面，内部C++调用返回完整Imported model snapshot与Import report，能精确查询，源与结果均有清晰所有权。

**Blocked by:** 04「平面的必要规则与有向区域可独立验收」。

**Proposed acceptance criteria:**

- [ ] 从STEP经原始文档、中立模型及受控构建进入SMLib；F1面积96、边界40、拓扑/法向/参数/bbox及查询满足独立Q。
- [ ] target UV后装、SAME owner、有限window、来源映射、工作tol/有效阈值逐项核验；规范化与repair分开，Repair=None。
- [ ] 只在完整候选及必需报告成立后提交Accepted；普通源/构建/后验失败销毁候选，没有部分模型或暴露owned裸指针。
- [ ] 快照、源证据和报告的生命周期可测试，默认只读；开放表面不授予material volume资格。
- [ ] 仅证明本进程内部调用，后续公开API、GUI或worker并不因本票自动获得资格。

## 06：带孔面与负UV原点保持源区域

**What to build:** 同一导入调用能交付带孔面，并在源平面原点平移后保持世界几何及孔语义。

**Blocked by:** 05「从STEP交付owning只读开放平面」。

**Proposed acceptance criteria:**

- [ ] P02/P03分别得到面积88、边界52、正确孔内/孔边/面内分类及独立距离；世界结果一致，源参数映射可追踪。
- [ ] 负UV与window原点不导致clamp掩盖缺边，不把window当额外trim。
- [ ] N25等孔方向/区域反例在正确阶段拒绝；不翻孔、填孔或增tol。
- [ ] 每次use的UV/owner及源loop身份保持，查询证据和原有F1回归一起交付。

## 07：从盒体STEP保留共享边和材料实体

**What to build:** 导入盒体得到真正共享拓扑的材料实体，能精确验证内外、面积/体积和边界；错误的壳声明不靠缝合修成成功。

**Blocked by:** 05「从STEP交付owning只读开放平面」。

**Proposed acceptance criteria:**

- [ ] P04保留8VP/12EC/6AF及24次有向使用、两面共享同一源边；得到面积2200、体积6000、形心/内外/距离真值。
- [ ] 每面平面、UV、outer侧及共享边邻接逐层对账；计数、Euler或validator成功不单独证明正确。
- [ ] N26/N27/N28等声明、定向、缺面/孤立问题按因果准确拒绝，已失败前提不拿来算假体积。
- [ ] 该实体资格与表面资格分开；额外截面/切割及开放共享片等总体验收项目仍按各自证据晋升。

## 08：同形闭合表面保留surface-model角色

**What to build:** 调用者导入同形盒体的两种源表示，能得到相同精确边界和不同形状角色，表面模型不会被自动升级成solid。

**Blocked by:** 07「从盒体STEP保留共享边和材料实体」。

**Proposed acceptance criteria:**

- [ ] P05的MSSR/SBSM/闭壳关系、共享拓扑和面积与P04对照正确，源表示和角色信息可查询。
- [ ] material volume/InsideSolid资格为NotApplicable，不能返回0假成功或因有向积分可算6000就变solid。
- [ ] surface与solid路径的owner/报告独立；普通FACE_SURFACE等未准入分支仍明确Unsupported。

## 09：单位重述与conversion链保持物理几何

**What to build:** 导入调用按源物理单位正确解释厘米重述与英寸盒体，并在零负/不完整换算时明确拒绝。

**Blocked by:** 07「从盒体STEP保留共享边和材料实体」。

**Proposed acceptance criteria:**

- [ ] P06只改单位声明得到10倍几何；P07协调重述保持同一世界几何，LINE t不重复缩放，PLANE UV保持专属映射。
- [ ] P11的127/5换算、显式dimensions、typed factor、共享单位链与U1面积/体积/形心贯通；不按inch标签猜factor。
- [ ] N31/N32/N33区分源量纲反证与项目正尺度要求，预算继续用canonical mm，不静默取绝对值/修单位。
- [ ] 本票不依赖可选uncertainty支持；P13与N34的组合单位环控制在12统一验收，不人为让两条独立分支串行。

## 10：可选来源声明与旧AP203类型完整保留

**What to build:** 在相同导入入口中，调用者可取得uncertainty、part分类、sr、旧context和指定source的完整来源声明，精确几何及质量配置不会被它们偷偷改写。

**Blocked by:** 05「从STEP交付owning只读开放平面」。

**Proposed acceptance criteria:**

- [ ] P08/P09/P10/P12逐项通过；声明缺席也按合法范围处理，旧类型不因deprecated丢失身份。
- [ ] source uncertainty用自身单位并只记录/告警，不覆盖工作tol、Q或repair budget；rad与sr维数同零仍保留名义类型。
- [ ] N29/N30/N35/N36及对应旧类型局部约束给出准确原因，既不移植其他AP管理规则，也不将新增关系忽略。
- [ ] 可选分支与F1完整模型/报告一起交付；和conversion盒体的组合由12验证，避免多余阻塞边。

## 11：生命周期、取消与限额不留下部分结果

**What to build:** 调用者取消或超过已配置资源限制时，不得到半成品；快照、view、报告及独立建模副本的持有/释放顺序清晰可验证。

**Blocked by:** 05「从STEP交付owning只读开放平面」。

**Proposed acceptance criteria:**

- [ ] core同步调用在安全边界合作取消；取消/成功通过一个提交点裁决，旧结果及捕获源不变。
- [ ] 输入/记录/深度/内存/报告等声明限额准确拒绝，和意外分配/内部故障分开；报告中的未测值不填零。
- [ ] 报告可独立保存；view保活与独立可编辑BRep副本不会悬空或改写只读原快照；普通native失败和后验拒绝的资源账可重复检查。
- [ ] 精确记录实际源码/二进制身份及残留/峰值/趋势证据；不据占位heap计数或进程退出宣称进程内零泄漏。
- [ ] worker硬取消、任意崩溃恢复和跨进程物化仍是后续P6资格，不用本票合作取消冒充。

## 12：汇总49项端到端证据并限定首profile资格

**What to build:** 一次可重复验收生成49项样件的真实结果与能力范围，调用者能判断哪些请求可接纳、怎样失败，以及尚未覆盖什么。

**Blocked by:** 06「带孔面与负UV原点保持源区域」；08「同形闭合表面保留surface-model角色」；09「单位重述与conversion链保持物理几何」；10「可选来源声明与旧AP203类型完整保留」；11「生命周期、取消与限额不留下部分结果」。

**Proposed acceptance criteria:**

- [ ] P13组合conversion盒体与uncertainty锚；N34只有预期单位环、没有额外孤立unit，根因诊断与拒绝准确。
- [ ] 13正例、36反例按固定输入/配置/检查版本执行；输入hash、源图/阶段、独立oracle、真实模型/诊断和owner/质量证据一致，不复用混代结果。
- [ ] 36反例均无本次模型；已有明确反证的case不能以笼统未判定代替应有诊断；未测能力不被计为通过。
- [ ] 输出首profile支持清单、失败原因和缺口；零已知错接纳、静默丢必需内容、超预算通过或未声明部分发布。
- [ ] 将49项首profile证据与总体P0—P7门槛逐项映射；未覆盖的全部Part21载体、其他AP、开放共享片/全sense组合、截面/建模及工程故障仍保持待证，不提升为整个P1/P2或首版完成。

## 依赖复核

01→02→03→04→05是首条可核验路径。05之后06、07、10、11只依赖已完成的owning平面基础；09要对照英寸材料盒体所以依赖07；08需要复用盒体共享图并与材料实体成对验证所以依赖07。12合并五条末端分支，不重复列其传递依赖。

未来所选输入若要求额外能力，需明确增加相应依赖；不能靠当前拆分的无阻塞掩盖源检查缺口。票号无阻塞环，所有编号按前置票优先排列；通过或未通过都需实际证据，当前没有票已执行。

## 用户确认记录

2026-10-15，用户明确确认“就按这12票写成本地独立票，依赖关系记录清楚，暂不编码”。已按确认内容一票一文件建档，未变更票的交付、验收条件或阻塞关系；所有票Execution为on-hold，未领取或实施。原提案正文中的“确认后”描述保留为审阅历史，当前发布状态以票组索引为准。
