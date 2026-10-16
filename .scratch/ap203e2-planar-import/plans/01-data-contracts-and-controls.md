# 01票补充：内部数据合同、参考证据与工具控制

版本：T01-Contracts-0.2，2026-10-16。配套[01实施方案](01-fixture-evidence-plan.md)及[01票](../issues/01-fixture-evidence.md)。**全部为Markdown中的格式/判据设计，没有创建JSON Schema、STEP、真值数据、工具或运行报告；没有执行下述控制。01及其他票继续on-hold。**

这里细化内部v1材料格式，不冻结产品公开API。既有13正/36反、profile、Q及12票依赖不变；字段定义和示例都不能当实际生成或通过的证据。

每组控制的基准、允许改动、摘要维护、实际触达证据及裁决详见[工具控制执行清单](01-control-execution-checklist.md)。12组共有14个子场景；这是01工具自身的控制设计，不增加49个产品样件的计数。

## 1. 真正独立的参考锚点

hash只能证明文件身份，两个公式也可能共同使用一个错误参数。生成器若把矩形宽12写成11，并把truth改为11×8，两边都算出88仍可能一致。01必须保留**不由该生成过程产生的、已审阅的参考锚点**，分别核对“输入是否是指定case”和“truth是否是指定真值”。

未来语料增加`reference/anchors.json`和`review/reference-review.json`：

| 材料 | 独立来源与责任 |
|---|---|
| 记录基准 | 原样件册的明确记录/字段/ID/引用表；用于核对物化字节是否是指定输入。它不能由待核验的生成器输出反填 |
| 数学锚点 | 原独立真值表、角色、单位语义和有向身份；例如F1为源平面上12×8矩形、面积96、周长40，B1尺寸10×20×30且体积6000 |
| case变换锚点 | P06只改单位产生10倍物理模型，P07协调重述保持同形；P03参数原点移动、P05角色变化、P11/P13精确英寸及锚关系，分别有既定定义 |
| 反例锚点 | 指定父例、唯一mutation、首诊断及已证明同因后果；坏输入不因生成器求值失败而被修复 |
| 审阅凭据 | 来源文档hash、锚点hash、具体审阅范围/方法/执行者及真实时间；空白审阅项不能用自动工具名称伪装已审阅 |

锚点在录入后也需要独立复核，不能自称无误。正常运行从明确选择的冻结语料取得其固定hash；工具控制使用临时变体时，**可信参考仍绑定原冻结版本**，不能让被改的变体自己决定新的参考锚点。

若连原文、参考锚点及其信任绑定都被一起改写，材料一致性工具不能保证发现共同错误；这属于新版本与重新审阅的边界。本方案不把hash当真实性签名，也不承诺发现所有共同失误。

## 2. 基本类型与必需值

| 类型 | v1约束 |
|---|---|
| case_id | 现有49项的精确ID；P01—P13/N01—N36属于S203命名空间。别名F1/B1/C203不另占case记录 |
| sha256 | 冻结实际文件时为64位小写十六进制；不得用全零、设计占位符或文档hash充当尚不存在的输入hash |
| artifact_ref | 语料内相对path、角色、实际sha256及非负byte_length；需与manifest对同一材料的登记一致 |
| parent_ref | 指定父case_id、corpus_revision及其input sha256；不是只记父例名称，也不接受另一个物理几何相近的父例 |
| byte_span | UTF-8原始文件的0基start/end，end不含；`0≤start≤end≤byte_length`。新增记录可用空旧span，替换/删除须有匹配的旧字节 |
| record_occurrence | 从1起的DATA记录出现序号；另存源#id。出现序号唯一，源#id可以在预期坏输入中重复；HEADER不占DATA序号 |
| rational | 分子/分母为整数字符串，分母正且约分；0表示0/1，不用JSON float替代 |
| sqrt_rational | 非负有理数的正平方根；根号内也采用分子/分母字符串，区间展示附包含证明 |
| applicability | Applicable或NotApplicable，并有reason。N/A的数值为null/缺省，不用0伪装测量 |
| status与未知 | 使用明确的状态枚举及reason；必需事实缺失为合同问题，未执行的observed为null，不把它变成False/0/空数组 |

所有字段名按内部format_version解释。未知版本不按“兼容猜测”读取；已有版本出现未登记、可能影响判定的字段或facet不能静默丢弃。可选扩展也需有明确保留/解释策略。

## 3. 文件关系及一致性检查

### 3.1 manifest、来源及case

| 文件／字段组 | 必需内容 | 跨文件不变量 |
|---|---|---|
| manifest.identity | format_version、corpus_id、corpus_revision、profile_id、Q及工具资源配置引用、sources_lock/ref_anchors引用 | 语料版本、格式版本与源profile不是同一个编号；run实际绑定manifest最终hash |
| manifest.cases | 每项case_id/kind/case_ref/input_ref、父例引用或无父例原因 | 精确49项；ID唯一；父例属于同一冻结谱系且无环；input不是patch片段 |
| manifest.materials | 参与语料判定的全部材料引用，含Q、sources.lock、reference、truth及assertions | 相对path不出语料根；每个必需角色有材料，不只核已存在的文件；不递归记录manifest自身hash |
| sources.lock | 已确认规格/各设计文档、N2635来源等级/hash、工具与锚点来源 | 不能将实现稿升级成未改ISO原版；文档更新不自动重写已冻结case的expected |
| case.identity | case_id/revision、Positive/Negative、profile/Q、repair=None、选择产品定义#7 | 与manifest同case一致；run选择不由PRODUCT.name或文件名猜测 |
| case.materials | 输入、recipe或mutation、truth、assertions及锚点引用 | 同一引用在case和manifest的path/hash/长度一致；冲突应报材料合同错误，不能任取一项 |
| case.derivation | RootPositive／DerivedPositive／SingleMutationNegative及parent_ref | P01/P04为根；派生正例允许完整协调变更；反例只从其指定正父例派生，不串联其他负例 |

每个case/合同文件也有format_version。机器材料一经实际冻结，必需hash/span不能仍留“以后补”；设计文档中的未生成状态不允许被静默序列化成一份“冻结成功”的manifest。

### 3.2 recipe、mutation与实际输入

| 对象 | 必需字段／含义 | 核验方式 |
|---|---|---|
| recipe.records | 有序出现列表，记录文本/确切字段、语义角色、源#id、设计来源锚点 | 物化后对实际字节复核，不以生成器内部对象数充当实际记录数 |
| recipe.derivation | 父例、完整正例变更集、新增/替换的区分 | P02的旧AF只能替换一次；P10三种子型变化是协调正例，不误称一个token |
| mutation.operation | ReplaceFieldToken、ReplaceTypeName、ReplaceAggregate、AppendOccurrence或RemoveAggregateMember之一，以及唯一逻辑根因 | N28删除成员时可连同必要分隔符改变一个连续span，但不允许额外删孤立记录 |
| mutation.location | 父input hash、语义实体/字段/成员定位、父字节span、old/new实际字节 | 对父文件精确匹配一次，不做模糊定位或全局替换；重复#id不能作为子输入中的唯一定位 |
| mutation.receipt | 变更后完整child input hash/span、未改前后字节相等的证明、允许同因后果 | child字节必须正好等于父prefix＋new字节＋父suffix；新增/删除按空old/new处理 |

旧/新字节可采用base64保存以避免JSON转义和换行歧义，另存可读UTF-8文本用于审阅；两种表示必须对应同一字节序列。receipt是生成后事实，不能预先填写未知offset。

对N02应保存两条独立的#100出现和两个span；对N08/N28不移除连带孤立项；对N34只修改引用，不让生成阶段递归求scale。坏payload与准备工具故障分别判定。

### 3.3 truth与assertions

| 对象 | 必需内容 | 约束 |
|---|---|---|
| truth.reference | 参考锚点hash、方法、源单位→mm/rad的独立解释、role | 数值核算从可信定义出发，不从同一可变truth取参数再证明truth自己 |
| truth.exact | 标识、单位、rational/sqrt_rational/身份关系、适用性 | 不把整个坏输入都声明成合法模型；仅保存可证明的不变定义或反证 |
| truth.derivations | 面积/距离/方向/拓扑的推导与可用第二方法 | 同一错误参数的乘积与shoelace一致不足以证明输入是正确case |
| assertions.expected | 预期Accepted/Rejected、model_present、角色/资格、逻辑阶段及适用前提 | expected只是设计；P05的material volume为N/A；01没有subject时actual不能由这些字段复制 |
| assertions.diagnostics | 稳定内部语义key、源字段/出现/规则/单位/阈值/因果，允许的细分映射 | 不冻结产品公开enum或完整文案；generic Failed/Unsupported不能替代确切根因 |
| assertions.check_scope | 必查项、仅作为已证明后果的项、因早阻断而不执行的项 | N24先有EL收尾反证，12mm几何后果不要求native求值；N28不强求首先NonManifold |

profile/Q只引用冻结配置，source uncertainty的值不能成为修改Q的输入。用于numeric比较的expected及actual都需有适用方法与可信界，缺界不能通过“看起来接近”变成Matched。

## 4. 运行证据的外键与状态不变量

| 文件 | 运行时必须绑定的身份／内容 |
|---|---|
| run | run_id、选择范围、manifest/Q/资源/参考锚点实际hash、工具与解释器身份、开始/结束UTC、subject配置、执行模式 |
| input.receipt | case/attempt及输入真实hash/长度/完整性；实际副本或稳定内容引用；与run使用的case版本一致 |
| preparation | 对账项、结果与证据、谱系/old-span核验；输入自身故意坏内容与材料错误分开 |
| oracle | 参考锚点/expected hash、方法和工具版本、实际核算值或区间、适用性、差异反证 |
| observed | 实际执行身份与输入/Q、执行状态、真实终态/模型角色/诊断/测量；无执行时留null及原因 |
| comparison | 绑定assertions与observed，记录比较项/适用前提及NotChecked/Matched/Mismatched/Indeterminate |
| summary | 所选case/attempt、各维度计数与缺口；case_results逐项列出所选case及其唯一选定attempt，历史尝试另存，不靠dictionary覆盖重复；未选case不填通过，不把工具自检项混入49样件分母 |
| artifacts/completion | 全部必需产物及真实hash、最后提交的清单hash与run身份；不自引用hash，也不凭一条Complete字符串判完整 |

关键不变量：

1. subject未配置时execution=NotRun，observed终态/模型/误差/调用计数为空，comparison=NotChecked；不能因未创建模型就证明Negative已正确拒绝。
2. preparation或oracle为Failed/Indeterminate时，不能将该材料的正式验收汇总为通过；允许发布完整的失败证据包。
3. observed绑定的输入、profile/Q或工具身份不匹配时，comparison不作语义匹配；报Evidence身份错误，不嫁接旧run数据。
4. expected Rejected与一次真实Rejected也不必然Matched：逻辑责任、字段、确切反证及适用范围仍须满足合同。
5. input、truth或oracle未实际读取/执行时，状态只能NotRun等准确值；没有读取证据不填“0误差/0调用/0泄漏”。
6. Complete须位于正式runs目录、completion与产物hash有效、必需角色齐备；存在目录、退出码0或HTML生成不够。
7. 源schema的规定UNKNOWN可以在外层表达式中得到明确True；这一语义结果不同于工具尚未计算或subject未运行。

预期结果、工具检查、实际被测执行、证据完整性各自有状态，不新增一个会把它们折叠的总pass布尔值。

## 5. 三份参考证据的设计示例

以下仅为**预计报告内容**。数字为已有数学/记录设计真值，未来读取的hash、byte_length、offset、运行时间和实际状态现在均未生成；不填示意SHA或虚构Passed。这三份应当在01后续实施时首先贯通，均不启动产品loader。

### 5.1 S203-P01：正常开放平面

| 证据位置 | 设计期望 |
|---|---|
| 输入身份 | 完整共同封套＋F1记录，预计51次DATA出现、51个唯一ID；UTF-8/LF及固定HEADER |
| 参考定义 | 源frame `O=(3,-2,5), X=(3/5,4/5,0), Y=(-4/5,3/5,0)`；UV矩形12×8 |
| exact真值 | 有向面积+96、边界40、形心(17/5,26/5,5)、4VP/4EC/1AF、OpenSurface角色 |
| oracle设计检查 | 从可信锚点核12×8与shoelace；核坐标/frame、身份/有向路径；不由input生成器传来的width变量决定参考 |
| preparation/oracle当前状态 | 未执行；实施后需写实际对账与数学核算结果，不能在本示例标Passed |
| subject/comparison | 未配置／NotRun；observed=null，match=NotChecked |
| expected | 未来完整P2路径Accepted；无material-volume资格，无repair |

### 5.2 S203-N20：纯LINE冲突

| 证据位置 | 设计期望 |
|---|---|
| 父例与唯一变更 | 冻结P01，#202.pnt参数2由#100(A)改#102(C)；其余字节不变 |
| 未改事实 | VP、PLANE和完整路径身份未改；预计仍51记录，源曲线新pnt仍在同一plane |
| 独立反证 | `C=A+12X+8Y`，无限LINE等价垂移8Y；A/B到新LINE各8mm，投影t=-1→0与same_sense=T一致 |
| assertion | D4 EndpointCurveMismatch，8mm显著超过一致性Q=1e-7mm；不是离平面，也不是源参数不在[0,1]即非法 |
| preparation设计判定 | 单mutation与这项坏定义精确保留才可在实施时Passed；不能要求坏输入整体具备合法face面积96 |
| 当前及未来subject | 当前NotRun/null/NotChecked；后续实际拒绝且证据匹配后才Matched，model_present=false是未来断言 |

### 5.3 S203-N02：重复实例ID

| 证据位置 | 设计期望 |
|---|---|
| 父例与唯一变更 | 冻结P01，在DATA末尾明确追加一条同内容#100，旧#100保留 |
| 精确材料事实 | 52次DATA出现、51个唯一ID；两个#100各有出现序号与独立span，不能只存一个dictionary成员 |
| assertion | D0 DuplicateInstanceId，给两处位置及源ID；不继续选择一个#100解释有效模型 |
| preparation设计判定 | 保留预期重复属于材料准备正确，不把它当生成器失败；丢重复或提前修复才是准备失败 |
| oracle设计范围 | 身份歧义与追加事实可证明；不要求对不存在唯一身份解释的完整模型计算正常面积/体积 |
| subject/comparison | 当前未配置／NotRun/null/NotChecked；未来真实源索引拒绝必须符合该反证 |

三例的case HTML需同时显示expected和实际观测的不同来源；图形、色彩和summary不会重新算另一套结论。01当前只有这份内容设计，没有三份真实证据包。

## 6. 工具控制：确实发现错误，而不是只发现hash变化

控制均在专用临时副本及HarnessControl模式下执行，不能修改冻结语料；每个控制有control_id、允许改动范围、原参考锚点hash、预期工具诊断和实际证据。它们不是36项STEP单缺陷反例，不增加49样件数，也不授予产品故障处理资格。

| 控制 | 明确变更／输入 | 必需发现的问题 | 防止的假验证 |
|---|---|---|---|
| CTL01 原字节篡改 | 改input一个字节，不更新其登记hash | Preparation.HashMismatch；定位input与期望/实际身份 | 工具不读实际payload、只信manifest |
| CTL02 真值篡改并更新摘要 | 临时P01 truth面积96→95，变体自身hash链一致；可信参考仍为原F1 | Oracle.ExpectedValueConflict，独立核算96≠95；不能仅靠旧hash不符发现 | hash检查被误称为验证了数学checker |
| CTL03 生成与真值共同错误 | 按下一节改为11×8并将truth同步为88，变体摘要全更新 | 至少发现case定义与原F1参考不符；若继续核truth，也应与可信96冲突 | 两种公式共用错误width，却一起返回88而误绿 |
| CTL04 合法单位正控制 | 原P06和P07分别对各自正确参考运行 | P06为10倍物理几何，P07同形，材料与oracle可通过；subject仍NotRun | 为捕获CTL03而误拒全部单位或尺度变化 |
| CTL05 丢重复的出现账本 | N02 input保留两#100，临时账本却只登记51条并同步该变体摘要 | Preparation.OccurrenceInventoryMismatch：实际出现52、账本51 | 使用ID字典把故意重复吞掉 |
| CTL06 错误父例 | 将N34从无mm锚的P11而不是P13派生，变体自身记录可一致 | Preparation.ParentContractMismatch；不能当正式N34的纯环控制 | 只看mutation文本而不核父例语义/身份 |
| CTL07 未运行假匹配 | subject未配置/NotRun，临时comparison却标Matched | Evidence.StateContradiction | expected Rejected复制成actual或未创建模型就算正确拒绝 |
| CTL08 缺必需产物 | 缺oracle证据，但伪造产物清单不再列它且称Complete | Evidence.RequiredArtifactMissing；按合同角色而非已有文件核完整性 | 改清单即可把半包伪装完整 |
| CTL09 选择与统计不符 | 选择全部49，summary漏一项或将未选项计通过 | Evidence.CaseCoverageMismatch，指出选择/attempt与汇总差异 | 只看总数或HTML颜色，不核case身份 |
| CTL10 表面资格伪值 | P05 material_volume设0且标Applicable，而可信角色仍surface model | Contract.ApplicabilityConflict；值域不适用，不能用0充测量 | 数值类型正确就掩盖错误材料资格 |
| CTL11 精确值降为float | exact有理数字段改为JSON float，其他摘要一致 | Contract.ExactValueEncodingInvalid | 看起来相等的浮点值替代精确参考 |
| CTL12 假提交或旧身份 | completion绑定另一清单/run，或缺payload仍留Complete标记 | Evidence.CommitIdentityOrHashMismatch／缺必需产物；不能发布或复用 | 凭文件存在、退出码或单状态字符串宣称完整 |

失败诊断是内部工具语义标签，不与生产STEP错误混淆。若工具未能发现控制中的已知问题、误拒CTL04或污染冻结材料，01的V10/V11不能通过；控制异常不会被修成“预期如此”后继续关闭票。

### 6.1 CTL03的具体共同错误

从P01的临时副本协调改变下列四个输入字段，其他DATA内容不变：

| 字段 | 原值 | 控制中的错误值 |
|---|---|---|
| #101.coordinates | (10.2,7.6,5.) | (9.6,6.8,5.) |
| #102.coordinates | (3.8,12.4,5.) | (3.2,11.6,5.) |
| #201.magnitude | 12. | 11. |
| #221.magnitude | 12. | 11. |

它们构成同一源frame上的11×8矩形：面积88、边界38、形心(3.1,4.8,5)，bbox为`[-3.4,9.6]×[-2,11.6]×{5}`。再把临时truth、预期记录账本和其摘要同步成这套错误值，但保持case_id=P01且可信参考包仍绑定原F1。源范围内的数学图可以自洽，**错误在于它不再是指定P01**；这不是证明其STEP整体非法，也不是一个单缺陷STEP用例。

若只对错误账本算11×8与shoelace，两者都可能得88；所以必须先对实际输入定义与可信case锚点的对应进行独立核验，并对truth与可信参考96核验。不能让变体提供的width=11悄悄替换参考width=12。

如果直接从原样件文档/手工锚点录入错误也使该控制不再可靠，需要纠正参考审阅本身；不能宣称多一份JSON就实现了数学独立性。

## 7. 完整性、复用及这次的完成边界

正常证据包绑定确切corpus manifest、参考包、case/Q和工具身份；临时控制明确HarnessControl身份，不能计入正常49项通过统计。Partial、失败或身份不符的attempt不被复用；允许复用的检查必须实际完成、证据完整且所有输入/配置/实现身份一致，不能复用HTML或目录存在这个事实。

本补充使01的文件合同、三个首批证据内容及12个工具控制可逐项实现/核验。它们细化原方案A—H与V01—V12，不增加票、修改53条已确认验收或解除任何依赖。当前实际仍只有Markdown文档，未生成数据格式定义、样件或证据，所有执行状态NotRun，01仍on-hold。
