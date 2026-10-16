# 01票实施方案：样件、独立真值与证据组织

版本：T01-Plan-0.3，2026-10-16。对应[01：从样件设计得到可重复的独立验收证据](../issues/01-fixture-evidence.md)。**本轮仅细化方案；票的Status、Assignee、Execution与四条验收条件不变。暂不编码，不生成STEP、JSON数据、oracle、测试或运行证据。** 下文目录、字段和步骤均为后续实施合同，没有创建同名数据文件。

设计依据：[总体规格0.1](../../../docs/step-import-repair-spec.md)、[首profile](../../../docs/step-import/ap203e2-planar-profile.md)、[样件总册](../../../docs/step-import/ap203e2-sample-design.md)、[平面记录](../../../docs/step-import/ap203e2-sample-plane.md)、[盒体记录](../../../docs/step-import/ap203e2-sample-box.md)、[36反例及诊断](../../../docs/step-import/ap203e2-sample-cases.md)、[独立数学真值与Q](../../../docs/step-import/p0-planar-cases.md)。本方案组织已有承诺，不增加生产默认值、完整AP认证或读取器支持范围。

具体字段约束、三个首批证据内容及12项工具控制见[内部数据合同与控制设计](01-data-contracts-and-controls.md)。它补齐本方案的可复核细节，全部仍未实施。

CTL01—CTL12进一步展开为[12组、14子场景执行清单](01-control-execution-checklist.md)，明确基准、设置diff、摘要维护、目标检查触达及Passed/SetupFailed等独立裁决。没有触及目标检查的任意报错不算控制通过。

## 1. 01票交付到哪里

后续实施的最小调用是：给出case ID、冻结语料版本、测试Q和输出位置，获得该输入、真值来源、单mutation谱系、材料对账及数学核算结果的可读证据。首次只实现合成数据工具和独立oracle；产品loader从02票开始。

| 可以在01票完成时成立 | 仍不成立 |
|---|---|
| 13正例、36反例已经实际生成、逐项复核、冻结真实字节与hash | 49项STEP已经被产品读取器接受／正确拒绝 |
| 独立有理数/代数真值及工具自检实际通过 | SMLib构建、native查询、UV/tol/owner与清理已经通过 |
| 单case／全语料可输出完整材料证据，指出错误、缺失或NotRun | 源符合完整AP、整个P0/P1/P2已退出，或旧native探针可替代新样件 |
| 01的四条验收条件有对应真实证据 | 后续票前置自动完成、开始执行或已解除暂不编码限制 |

01票可以在导入行为全部NotRun时完成，但其材料核验、数学核算、工具自检和报告链必须真的执行；不能仅凭本方案或文件存在就关闭01票。

## 2. 三类材料与实现边界

| 材料 | 决定什么 | 不能从哪里反推 |
|---|---|---|
| 样件定义与完整输入 | 原始STEP字节、产品/几何记录、源ID和指定mutation | 不能从native CreateBox、OCCT写出或被测loader往返获得唯一输入 |
| 独立真值与预期断言 | 精确世界几何、身份/使用关系、诊断根因、预期结果与资格 | 不能将实际导入/查询输出复制成expected；不能只用生成器自己的计算证实生成器 |
| 每次运行的证据 | 实际输入身份、工具/配置、哪些步骤做了、观察与比较结果 | 不能把expected复制成observed，不能以已有旧报告或成功退出代替执行 |

建议01采用轻量Python合成验证工具，优先使用标准库的任意精度整数、有理数、hash和JSON能力；它不是产品C++ loader的运行依赖。实际运行时记录解释器路径/版本及工具源码身份。本轮不选装依赖、不建立环境、不编写该工具。

工具职责明确分成：样件物化、材料/谱系核验、独立数学核算、证据记录/展示。共享case ID和版本是必要的；共享一条几何转换实现同时产生输入与真值则不能构成独立正确性证据。观察adapter只消费后续票交付的实际结果，不拥有或改写golden输入及真值。

## 3. 未来文件目录

### 3.1 仓库内：可版本化的合成语料与真值

建议后续01在专用轻量验证工具下建立以下目录；当前不创建。仅放自行设计的公开合成材料，既有私有模型库和第三方运行库不复制进来。

```text
tools/step_import_validation/
  README.md
  contracts/v1/                 内部材料/证据格式的约束与说明
  corpus/ap203e2-planar/v1/
    manifest.json              49项清单、依赖与全部材料hash入口
    sources.lock.json          设计/规范来源与工具可追溯身份
    reference/
      anchors.json             独立审阅的记录/数学/变换/反例锚点
    quality/
      Q-P0-mm-01.json           测试Q；明确不是生产默认
    resources/
      T01-corpus-limits.json    材料工具的显式限额，不是产品限额资格
    cases/
      S203-P01/
        case.json              case身份、选择、预期与材料引用
        recipe.json            完整有序记录账本／正例增量
        input.step             冻结的完整文件，含HEADER与END标记
        truth.json             精确数学和拓扑真值
        assertions.json        期望行为、阶段与诊断合同
      ...                      P02—P13采用同一结构
      S203-N01/
        case.json
        mutation.json          父例、唯一变更及其旧/新字节证据
        input.step             完整坏输入，不能只存patch
        truth.json             已证明的输入事实与失效范围
        assertions.json
      ...                      N02—N36采用同一结构
    review/
      reference-review.json    参考锚点的来源与独立审阅凭据
      generation-review.json   记录/引用/变更账本的复核凭据
      oracle-review.json       数学来源与独立复核凭据
      coverage.json            49项与原设计/票验收的对应
```

生成器、核验器和oracle的实现位置由01编码时按现有工具约定落地；本方案不先拆公开API、编译项目或多套测试框架。contracts为内部可版本化格式，不承诺公开稳定接口；corpus版本、格式版本、工具版本和case ID分开记录。

正式冻结后不覆盖同一语料版本中的字节或真值。修正文档、输入、真值或diagnostic合同均形成新revision/hash，重做相关材料验证；原case ID可表示同一测试意图，但不能仅凭ID复用旧通过证据。

### 3.2 仓库外：逐次运行的实际证据

证据根目录由调用者明确提供。此工作区可选用现有本机outputs下的`step-import-evidence`，不把本机绝对路径写进仓库内manifest。运行ID使用适合Windows文件名的UTC时间段与唯一后缀，不以本机显示日期改写golden输入。

```text
<evidence-root>/
  staging/<run-id>.partial/
    run.json
    cases/<case-id>/<attempt-id>/
      input.receipt.json       实际读取的输入hash/字节及冻结身份
      preparation.json         材料、父例/单mutation、记录账本核验
      oracle.json              本次独立核算及精确/区间证据
      observed.json            被测执行；01中明确NotConfigured/NotRun
      comparison.json          比较范围、结果及不能比较的原因
      diagnostics.json          工具问题与后续实际诊断，分来源标记
      case.html                单case可读报告
    summary.json
    summary.html
    artifacts.json             本次产物hash与完整性清单
    completion.json            最后写入的提交凭据，绑定产物清单hash
  runs/<run-id>/               完整证据提交后出现；不覆盖旧run
```

单case也使用相同包结构，summary只含所选case，不将未选择的其余48项填通过。黄金材料已经版本化时，receipt可引用其内容hash及可解析语料包身份；若执行期间不能保证其字节稳定，则在同一证据包保存实际输入副本并绑定hash，不能仅留原路径。

写入先在staging；必要文件与hash清单齐备并冻结后，最后写completion凭据，再在同一文件系统提交为完整run。只有位于runs、提交凭据与全部hash有效的包才为Complete，不能仅相信run.json中自行写出的状态。Partial/取消/报告写失败不会自动成为Complete；下次不能仅因目录存在就跳过。完整报告也可以记录工具失败或被测失败，**证据完整与模型接纳是两回事**。未来涉及目录移动前核实绝对路径位于配置根内；本轮不做任何移动或清理。

## 4. 每种文件至少包含什么

### 4.1 语料身份与case合同

| 文件 | 最小字段／语义 |
|---|---|
| manifest | format_version、corpus_id/revision、profile_id、Q及工具资源配置引用、sources_lock摘要；49项case_id/kind及相对材料路径/hash；父例ID/hash；材料计数与设计预期 |
| sources.lock | 总体0.1、首profile、样件/反例/真值文档的版本/hash；N2635原始hash及可追溯URI、明确实现稿等级；生成/核验/oracle工具源码身份；不将已知但本次未用的DLL伪装成实际依赖 |
| case | case_id/revision、Positive或Negative、输入路径/hash/字节数、选择产品定义#7、profile/Q/repair=None、材料/真值/断言引用、父例及变更类别、适用和不适用检查 |
| recipe | 有序记录出现列表、完整记录文本或确切字段、原语义角色、记录顺序/HEADER固定值；根正例为完整账本，派生正例明确完整变更集；不能从目录扫描顺序猜排序 |
| mutation | 指定父例及其真实input hash、唯一操作类别、父字节定位、预期旧字节/new字节、变更前/后实体与字段角色、修改范围之外字节不变的证据、目标根因与允许的同因后果 |
| truth | 角色及适用性、独立几何定义/单位/参数映射、exact数学量、source身份/有向使用表、查询真值或反例输入事实、推导方法和来源；不把无效输入强行变成一个合法shape |
| assertions | 预期终态、逻辑阶段/前提、语义诊断key与来源字段/规则/精确反证、容许的细分诊断映射和同因后果、model=None等断言；字段消息文本不是唯一判据 |

运行只接受清单中的相对材料路径，路径必须仍处于冻结语料根内。manifest引用的文件先核hash再用于核算。manifest自身不放入自己的递归hash列表，其最终hash由run.json绑定；artifacts排除自身及completion，其余已冻结必需产物逐项hash；completion绑定artifacts的最终hash而不要求自身hash。工具源码身份也不把输出包作为源码输入，避免形成自引用摘要。

### 4.2 记录出现、身份与精确定位

输入按UTF-8无BOM、LF和固定记录顺序物化。固定HEADER的时间仍为设计指定的`2026-10-15T00:00:00`；实际运行日期只写证据元数据，不改变输入。每个DATA记录采用已声明的固定书写格式，字段值不经过二进制float来回格式化；不随机改ID、排序LIST或统一翻方向。SET也保持冻结设计的书写顺序，不排序/去重以遮掩N09的故意重复。

确定性JSON材料同样使用UTF-8无BOM/LF、4空格缩进和固定键顺序；数组保留语义次序，exact值用下节的字符串结构。不得写入临时目录、文件mtime或本次运行时间。证据run的时间/路径字段允许随执行变化，不参与输入及truth的确定性比较。

记录账本必须用**有序出现列表**，不能用实体ID为key的JSON对象吞掉重复。每项同时有record_occurrence（文件内出现序号）、源`#id`和实际字节span；N02的两个#100作为两个出现保留。区分data_record_occurrences、unique_entity_ids、字节数以及最高ID，不能互相代替。

字节span为UTF-8原始文件的0基起点、尾端不含；可另给1基行/列辅助阅读。mutation先在父例hash和唯一旧span匹配后应用一次，再记录子例span；旧值找不到、找到多处或父hash变化均是材料准备失败，不能模糊搜索/全局替换后继续。N02为明确追加一条重复记录，不能以“重复ID”为由在生成或账本阶段将其修好。

独立材料核验应直接检查生成后的字节及其出现账本，不能只重复遍历生成器内部对象。它只需识别本合成材料的固定文本形式、对账和diff，不实现产品lexer、通用Part21 parser或EXPRESS规则执行器；输入自身的预期坏引用/类型/enum/环必须原样保留。文档/账本对账通过不写成“schema合法”。

### 4.3 精确数学值与oracle误差

| 值种类 | 表示与检查 |
|---|---|
| 整数/有理数 | 用十进制整数字符串保存分子/分母；分母为正、约分、零为0/1；25.4用127/5，0.001用1/1000。语义长度/面积/体积/无量纲另有unit字段 |
| 正平方根 | 仅需sqrt_rational(a/b)，a≥0、b>0；√37/2表示sqrt(37/4)，√21表示sqrt(21/1)。不靠eval任意表达式或浮点字符串充当精确真值 |
| 几何/拓扑 | 点和基向量、有限区间、VP/EC/OE身份、四层sense、loop路径、面/壳角色和材料资格分别保存；native指针不作source ID |
| 不适用 | P05的material volume/InsideSolid为NotApplicable，值为缺省或null并带原因；不填0或把有向体积辅助积分6000当solid资格 |
| 反例输入事实 | N20端点-to-LINE残差8、N21边-to-plane为1/1000、N24的PATH真/EL假、N28的11孤立RI、N34的单位环；已失效的整体shape属性不计算为正常体积/面积 |

未来oracle以任意精度有理数核对基本运算、shoelace、有限线段投影、盒体区间分类和组合身份；不导入产品loader或CAD库。生成输入与expected数值不复用同一坐标/单位转换函数。明确审阅过的数值表和第二条数学推导互证，例如矩形乘积与shoelace、盒体乘积与有向行列式体积、源方向叉积和已声明normal。它们仍不能替代源规则验证。

两种公式也可能共用错误参数，因此另需可信case参考锚点：分别证明实际输入是指定case、truth与独立参考一致。不能让生成器的width变量或被改的truth自己定义参考；共同错误控制与其信任边界见补充文档CTL03。

正平方根用可证明的有理区间展示：选S=10^p，求整数k满足`k²b ≤ aS² < (k+1)²b`，则真值在`[k/S,(k+1)/S]`内；若左式相等则为精确端点。oracle区间宽度应小于适用比较预算的1/10，不足则继续收紧或返回未判定；不能假定某种浮点sqrt的round模式自动给出了上下界。

后续比较须记录actual和expected的可信区间/误差来源。最坏间距不超过对应Q才证明相符，区间之间的最小距离已超过Q才证明超差；其余未判定。读到一个浮点数并不自动获得其几何查询误差界。01没有被测观测时不执行这一比较，不把精确oracle自己的误差当被测模型误差。

材料工具的输入字节、记录出现、深度、报告大小和时间等限额须显式记录为T01工具配置。完整读取49项的正常档位应覆盖实际生成后的最大B/N/D并保留清楚的有限上界，不让工具限额先遮挡指定反例。数值在物化后确定，不宣称生产默认或OS硬限额；产品取消/资源资格仍由11票处理。

## 5. 全49项的物化与父例顺序

### 5.1 正例

| 组 | 输入生成依据 | DATA设计数 |
|---|---|---|
| P01、P04 | 两个独立根：共同12条封套分别加完整F1或B1账本 | 51、145 |
| P02←P01、P03←P02 | F2孔增量及AF替换；F3仅改专用plane原点 | 81、81 |
| P05←P04 | MSSR/SBSM合法角色替换集 | 145 |
| P06/P07←P01 | 单改cm声明实际放大10倍／协调源数值重述保持世界几何 | 51、51 |
| P08/P09/P10/P12←P01 | 分别为两uncertainty、part、旧context/指定source、sr | 53、52、51、52 |
| P11←P04、P13←P11 | U1精确inch；再加毫米uncertainty锚 | 148、149 |

先生成并复核13正例，再从其已冻结字节分别生成36反例；负例不会互作父例。正例协调变更可有多条记录，不能误称一个token的单缺陷。

### 5.2 反例父例清单

| 父例 | 全部子反例，闭区间 | 数量 |
|---|---|---:|
| P01 | N01—N08、N10—N24、N26、N36 | 25 |
| P04 | N09、N27、N28 | 3 |
| P02 | N25 | 1 |
| P08 | N29、N30 | 2 |
| P11 | N31—N33 | 3 |
| P13 | N34 | 1 |
| P09 | N35 | 1 |
| 合计 | N01—N36，无缺号、无重复 | 36 |

精确old/new和诊断以既有[反例目录](../../../docs/step-import/ap203e2-sample-cases.md)为依据，不再抄出一套可能漂移的完整表。每项mutation可以产生多个后果，但必须保留早门槛及未触发检查的事实。

生成负例时不求解、归一化或修复其不合法几何。N11零方向直接作为记录字节保留，N34单位环只是一次引用变更；不能由物化工具先递归解scale而挂住，也不能用父例的有效几何覆盖新输入。负例truth保存可证明的事实及失效范围，完整model不存在。

## 6. 分步实施与每步停点

只有用户后续明确授权实施后才执行下列步骤；它们都属于01票内部顺序，不增加12票之间的阻塞边。

| 步骤 | 实施内容 | 产物与停点 |
|---|---|---|
| A 冻结来源与合同 | 固定profile、13/36清单、Q、源文档hash及工具内部格式；将原票四条验收映射到本方案 | 先有sources.lock/manifest结构与审阅依据，任何未定父例/语义不靠生成器猜 |
| B 物化基础正例 | 先做P01和P04，按已审阅有序账本生成完整文本并复核HEADER/记录/引用/有向身份 | 得到真实51/145记录输入；数学真值与实际字节分开审核，不调用CAD |
| C 物化其余正例 | 按父例顺序展开P02—P13，核对完整变更、记录数、角色和单位真值 | 每个正例独立完整；F3、P06/P07、P11/P13的不同目的不可混淆 |
| D 物化36负例 | 在精确父hash/span上应用一次mutation，输出完整坏文件和receipt；检查未改范围 | 保留N02重复、N03悬空、N08/N28孤立和N34环；不做schema修复 |
| E 实现独立核算 | 将审阅真值表录为exact值，按数学方法复核区域/身份/单位/距离，并自检oracle | 核算报告给方法、精确值/区间和版本；原truth不由actual结果重写 |
| F 独立材料复核与复现 | 对真实字节和账本核hash/出现/引用/单mutation，在两个全新工作目录重复物化 | 49输入与确定性truth/assertions一致；时间/运行ID只影响证据外壳 |
| G 单case与全语料证据 | 按case选择运行材料/oracle检查，出机器证据和HTML；无SUT时显式NotRun | P01正常、N20精确反证、N02重复ID三例先验证证据路径，再汇总49项 |
| H 工具负控制与交接 | 在临时副本做篡改hash、缺产物等自检，核报告状态和Partial处理，再对原49项验收 | 01自身的验收证据完整后才可提出完成；02所需输入包和身份合同可直接复用 |

建议先完成B＋E＋G的P01一条可核验路径，再扩展到盒体、派生例和36负例；这样各步都有可检查结果。未覆盖的项保持NotRun，不为获得首个绿色报告填充默认成功。

## 7. 具体独立真值与特殊控制

| 控制 | 必须核对的独立事实 | 要阻止的误判 |
|---|---|---|
| P01/P02 | 有向外面积+96，孔−8；净面积96/88、边界40/52；孔内点不属于face | 总面积碰巧接近代替孔/方向/身份检查 |
| P03 | 世界边界不变，源UV外window为[-6,6]×[-4,4] | 把plane O当window左下角或移动几何 |
| P04/P05 | 同8/12/6及边界；P04材料体积6000，P05无材料体积资格 | 同形或有向积分6000等同于solid |
| P06/P07 | 只改声明为10倍物理形状／协调换单位才同形；LINE t不随length scale重复乘 | 把合法放大当坏单位；由输入生成器错误scale同时污染expected |
| P11/P13 | 精确127/5比例及U1面积14193.52/体积98322.384；P13独立保留mm锚 | 截断非有限inch小数；N34变更后先产生孤立mm而遮挡单位环 |
| N02 | 52条DATA出现、51个唯一ID，两处#100均有span | 以dictionary覆盖重复，或把语料的预期坏输入当材料准备失败 |
| N08/N28 | 五个孤立RI或十一个孤立RI为同因后果；主阻断分别为聚集UNIQUE和profile孤立内容 | 把单mutation说成只违反一条规则，或在expected中静默删孤立块 |
| N20/N21/N24 | 8mm、1/1000mm、12mm精确反证；N24 PATH真而EL收尾假 | 错轴、错误规则标签，或用native工作tol取代Q |
| N31/N32/N33 | 维数错误触发源WHERE；零/负基本LENGTH_MEASURE factor的拒绝为项目正scale条件 | 把不同责任都标为conversion.WR1失败 |
| N34 | 精确引用环#19→#18→#19，#10仍由UMWU使用 | 无限递归、猜inch默认值、要求得到不存在的有效世界模型 |

负例里的父例几何只作未改定义的参照，不能要求坏输入必须还能接受后再查询。例如N21只可证明trim边不在新plane，不能仍声称拥有合法面积96的同一face。

## 8. 预期、材料检查与被测结果的状态

这些字段是内部证据语义，不冻结产品公开enum。每项必须分别记录；不能只有一个含混的pass布尔值。

| 维度 | 可用状态／01的含义 |
|---|---|
| expected | Positive预期Accepted或Negative预期Rejected，带范围、阶段、语义诊断和精确反证；始终是设计期望 |
| preparation | NotRun／Passed／Failed；指真实输入/谱系/材料hash/账本是否与该case合同一致。预期重复或悬空的负例可在这一维Passed |
| oracle | NotRun／Passed／Failed／Indeterminate；指独立真值核算/反例事实证据及误差界是否成立，不是产品结果 |
| subject configuration | NotConfigured／Configured；01无产品adapter时为NotConfigured，并记录原因 |
| subject execution | NotRun／Completed／Aborted；01产品执行为NotRun，被测结果与实际模型观测字段为null并带原因 |
| expectation match | NotChecked／Matched／Mismatched／Indeterminate；无subject观测必须NotChecked，不能因expected为Rejected而填Matched |
| evidence completeness | Complete／Partial；报告及hash文件是否齐备。Complete仍可含失败或NotRun，不等于Accepted |

01最终摘要可以写“49项材料对账完成，oracle核算/控制按实测计数；产品导入NotRun=49”。不能写“49通过、成功率100%”。未来负例准确拒绝可作为测试Matched，但依旧不是模型导入成功。

01不记录虚构的native调用数0、内核误差0或泄漏0。未执行的生产reader/kernel路径、DLL hash、native观测和model状态均标未执行/不适用；已有基线DLL身份不能冒充本次实际加载身份。后续票有实际adapter和观测证据后才填这些字段。

例如负例的expected.model_present为false，但01没有subject时observed.model_present为null、match为NotChecked；不能因为没有创建任何模型就宣称产品已经正确拒绝它。

## 9. 证据文件的最小合同

| 文件 | 至少记录 |
|---|---|
| run | run/attempt身份、开始/结束UTC、corpus manifest实际hash、case选择、profile/Q/工具资源配置hash、repair=None、源码/工具/解释器版本、执行模式、SUT配置/未配置原因、完整/部分状态 |
| input.receipt | 实际读取路径或包内副本、字节数/hash、case/parent身份、选择、读取完整性；路径不是输入内容身份 |
| preparation | 各材料hash检查、原始记录出现/唯一ID数量、预期缺陷是否精确保留、mutation旧span命中/改动外字节不变、结果与失败证据 |
| oracle | 所用exact truth hash、方法/实现版本、独立复核来源、每项精确结果或有理区间/单位/适用性、核算及误差状态 |
| observed | 实际执行者和能力、调用是否发生、真实终态/模型角色/原始诊断/实测值。无执行留null及NotRun，不能复制assertions |
| comparison | 期望合同与观测映射、已比较检查及其前提、结果、未比较原因；源字段/规则/observed-limit/因果证据，而非只匹配错误字符串 |
| diagnostics | scope为Preparation／Oracle／Subject／Evidence，严重度与原因、case/源出现/span、证据；工具问题不伪装为源STEP错误 |
| artifacts、completion与summary | 必需产物hash与缺失/截断说明；completion最终绑定清单而不产生自引用hash；按各维度计数、选择范围和缺口；HTML只展示这些数据，不重新计算另一套判定 |

规则阶段是逻辑责任，不强迫实现调用同一顺序。允许实际诊断比设计key更细，但必须有明确版本化映射与同一反证；只报笼统Unsupported或Failed不能满足确定源错误的断言。较早门槛已阻断时，不要求不安全的后续几何调用或native构建来补齐全部诊断。

## 10. 01票验收步骤与失败判据

以下是**未来验收**，当前均未执行。四条原票条件不变，V编号用于细化证据追踪，不增加独立票或伪称新增已通过测试。

| 验收 | 操作与明确通过条件 | 失败／未判定时的处理 |
|---|---|---|
| V01 清单完整 | 精确P01—P13、N01—N36，49个唯一ID；每项材料、父例、profile/Q和来源齐全 | 缺号、混别名计数、缺parent/声明不明使准备失败 |
| V02 字节复现 | 在两个全新输出目录独立物化全部49项；输入及确定性truth/assertions字节/hash相同；固定HEADER不随日期变 | 差异定位到字段/格式，不能先重写golden为当前输出 |
| V03 记录与图账本 | 对所有正例的N与引用/角色对账；N02保留52次出现/51唯一ID；其他负例对照其预期坏图 | 错把故意坏输入修好、丢重复、错用最高ID作N均失败；这不授AP合法性 |
| V04 单mutation | 每负例匹配固定父hash和唯一old span，操作后只有登记范围改变，所有同因后果记录 | 无匹配/多匹配/额外整理/从错误父例派生都失败；N34不得从无mm锚的P11偷改 |
| V05 基础数学 | F1/F2/F3、B1/B2、U1的坐标/面积/方向/距离/身份与独立来源一致，两条可用数学方法交叉核算 | disagreement为oracle失败或待裁决，不用被测内核投票裁定 |
| V06 单位与资格 | P06/07/11/13按不同物理含义核算；P05的material volume为N/A；source uncertainty不改Q | 误缩t、截断inch、sr/rad混用、表面体积填0或改Q皆失败 |
| V07 数值可信 | √37/2、√21等真值有包含证明且误差预算足够；没有subject时comparison为NotChecked | 不充分区间为未判定；不以round、抽样或浮点巧合通过 |
| V08 单case路径 | 分别选择P01、N20、N02得到完整case证据和可读报告，49其他选择规则无暗加 | 选择一个却把其他case标通过，或只给截图/无机器证据均失败 |
| V09 状态防误报 | 无adapter时49项产品执行NotRun、match NotChecked；expected不会流入observed | 任何“准备通过→导入通过”、未测填0或Partial当Complete都是证据错误 |
| V10 工具阳性/阴性控制 | 在临时副本中篡改输入而不更新hash应报身份错；改truth96→95并更新摘要仍须被数学复核发现；另把输入和truth协调错成11×8也须对可信P01锚点报冲突；换parent／删产物等按CTL01—CTL12逐项控制 | 不能只用hash错误或共享错误参数的两种公式冒充数学独立。若工具仍绿，或污染冻结语料，则01不能验收；这些控制不另计入49样件 |
| V11 完整性与续跑 | 模拟证据写到一半停止、身份不匹配及旧失败记录；Partial不被复用，完整新attempt另有ID和清单 | 凭文件存在跳过、覆写旧run、必需证据被截断后继续完整提交皆失败 |
| V12 全组交接 | 完成49项材料与oracle核算/控制，导出可读summary及四条原票的证据对应；真实subject仍可全部NotRun | 有失败/未判定/缺证据不能将01四框勾完；不把01完成解释为02—12或P0—P7完成 |

V10/V11验证的是材料与报告工具自身，不模拟产品worker/OOM/损坏native图，也不承诺任意故障清理；对应工程资格仍在11票和总体P6。本轮不编写或运行上述控制。

### 10.1 四条原票条件怎样闭合

| 01原验收条件 | 本方案的实施与证据 |
|---|---|
| 49项固定输入、父例/精确mutation及记录对账 | A—D、F；manifest/case/recipe/mutation与真实input；V01—V04 |
| 真值独立，不从被测链回填 | E与独立复核；truth/oracle-review、exact/区间结果；V05—V07 |
| 配置/来源/工具可追溯，未运行不伪通过 | A、G/H；sources.lock/Q/run及分维度状态；V09—V11 |
| 单case可读证据且可由后续票复用 | G/H；P01/N20/N02单case、全组summary和观察adapter合同；V08、V12 |

## 11. 当前交付与后续门槛

当前只保存本实施/验收方案，01仍ready-for-agent、Assignee=None、Execution=on-hold，四条原验收框均未勾选；后续11张票的依赖关系不变。未来开工后才依A—H物化文件、实现工具并按V01—V12收集真实证据。

01的退出交付为冻结的合成语料/独立真值/断言、真实材料与oracle验收证据、单case/汇总可读报告及可复用身份合同。01未证明的源解释、几何转换、native/资源/发布行为继续精确NotRun；总体规格0.1字节不变。
