# 03票验收：单零件、类型与SI源语义

2026-10-16。按用户明确授权实现[03票](../../.scratch/ap203e2-planar-import/issues/03-product-context-si-units.md)，
继续不依赖OCCT读取，按01冻结样件、精确诊断和独立来源验收。本票五条原条件已完成；
04及后续票未开始，总体规格0.1和01/02已发布材料保持原样。

交付：[STEP_SEMANTICS静态库与CLI](../../source/STEP_SEMANTICS/README.md)、
[独立验收工具](../../tools/step_semantic_validation/README.md)。

| 实际执行 | 结果 |
|---|---|
| 49项当前阶段合同 | 49项匹配，不是49项完整导入通过 |
| Checked | 26项：名称/OID、字段/祖先、单零件和SI解释完成 |
| 源阻断 | 17项：8个字段Rejected、7个profile Unsupported、N02/N03的2个底层SourceFailure |
| Deferred | 6项：P11/P13/N31—N34，必需conversion解释留后续票 |
| 独立类型事实 | 72个声明的祖先/属性来源、36个simple叶，与固定N2635独立提取事实逐项一致 |
| MSVC Debug/Release | 各10组原始源层＋10组新语义层C++契约检查通过 |
| Python 3.14.3/3.10.5 | 分别执行Debug/Release真实CLI，各35测试通过 |
| 完整模型导入 | 全部NotRun／合同NotChecked，import_success_count=null |

所有源原值、ID、使用关系和位置仍由原始SourceSnapshot持有。语义快照只共享该owner，
保存紧凑类型位图/人口索引和少量根/单位解释；属性视图直接指向原值，没有复制第二套全文件实体/值图。
只读视图在保留语义快照、释放原始outcome后仍有效，实际C++指针同一性和生命周期已验证。

## 原五条条件的证据

| 原条件 | 实际实现与验证 |
|---|---|
| 精确名称/OID及独立APD | FILE_SCHEMA完整名＋`{1 0 10303 403 2 1 2}`绑定c68dd...N2635制品；裸名、错OID、错名、错误括号位置/后缀拒绝；APD claim和INTEGER年字面量原样保留，不以年份推edition或覆写claim |
| 主链、祖先及constituent | 唯一AC/APD/PC/PDC/product/formation/PD/PDS/SDR/具体representation/context按identity关联；全部72类人口和继承/diamond来源有独立对账；source角色不授几何资格 |
| SI与槽、参数含义 | 全局context局部字段及derived重声明精确核验；16个固定prefix和无prefix逐个用有理数核对；长度用mm、角用rad、sr保留名义类型；坐标/向量长度与direction ratio/LINE t/PLANE参数分别标明 |
| 反例和范围责任 | 下表17项均按01 key/stage及原字段/反证匹配；未知实体/facet、缺祖先、错槽、ONEOF冲突、缺/多单位和额外根均有拒绝测试，不跳过必需内容 |
| 完整反向人口/未完成标记 | AC的context_elements为#3/#6，APD不混入；REP_CONTEXT表示使用者为#20；RI使用闭包过滤管理/单位边；未知类型/role查询拒绝或返回未注册，不伪造空集；后续检查明确NotRun |

全局RULE、其他实体WHERE、named-measure谓词、conversion、uncertainty、几何一致性及SMLib构建没有因此完成。
仅在当前解释处检查dimension_count正值及两个旧context的局部谓词；这不是通用EXPRESS evaluator。
普通PC/PDC的角色拒绝是profile边界；旧MECHANICAL_CONTEXT/DESIGN_CONTEXT的确定反证归其D3局部WHERE，责任不混淆。

## 17项准确阻断

| 样件 | 首个阻断／责任 |
|---|---|
| N01 | D2 UnsupportedSchemaBinding：完整HEADER字符串及固定registry，不从APD猜回 |
| N02/N03 | 原D0 DuplicateInstanceId／D1 UnresolvedRequiredReference，底层位置和全文件引用证据保留 |
| N04 | D1 RequiredAttributeMissing：#3.name，祖先APPLICATION_CONTEXT_ELEMENT；空串不同于$ |
| N05 | D1 DerivedSlotMarkerRequired：#204.edge_start，EDGE显式槽被OE重声明，不造空顶点 |
| N06 | D1 BooleanValueInvalid：#204.orientation，BOOLEAN不接LOGICAL U |
| N07 | D1 ReferenceTypeMismatch：#203.edge_start，expected VERTEX／observed CARTESIAN_POINT；不因坐标相同替代identity |
| N08 | D1 AggregateUniqueViolation：PATH.edge_list，第2次#204与第一次相关；不先去重或报几何连接 |
| N09 | D1 SetDuplicateMember：CONNECTED_FACE_SET.cfs_faces，第7次#507；原六面保留 |
| N14 | D1 DerivedSlotMarkerRequired：#10.NAMED_UNIT.dimensions，SI重声明必须* |
| N15 | D1 EnumerationValueInvalid：#11.SI_UNIT.prefix；固定旧枚举不含QUETTA，未消费物理角也必须查 |
| N16/N17/N18 | D2 UnsupportedDefinitionContextRole／UnsupportedExplicitOuterBoundRequired／UnsupportedFaceSubtype |
| N28 | D2 UnownedOrOrphanProfileContent，完整11项#400—#403/#501—#507证据；不叫作已执行RI.WR1 |
| N35/N36 | D2 UnsupportedProductCategory／UnsupportedProductContextDiscipline；保留ply/electrical原值，不伪报不存在的普通context WHERE |

其他负例在03完成结构检查不意味着它们合法。N10的错误APD后缀、N11—N13等规则反证、
N19—N27的连接/方向/几何错误，以及N29/N30的uncertainty约束仍由后续票验证。
N31—N34连同conversion正例明确Deferred，canonical长度尺度为null，退出码7，
reason为`ConversionLengthInterpretationPendingTicket09`；不据inch名称猜127/5，不假造D4循环/正尺度检查已执行。

## 独立依据与控制

原制品仍为MBx N2635实现稿SHA
`c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`，没有声称ISO最终出版认证。
独立见证文件为
[n2635-reference-v2.json](../../tools/step_semantic_validation/contracts/n2635-reference-v2.json)，
SHA `95eaed220c9e638f8f94c6e9429c2b562668ad643070efa77f26b8b305b29a58`。
提取工具读取固定schema的声明事实，不读取C++catalog或native结果作为expected。

初版开发提取器将字段内`OF UNIQUE`误作段落终点，独立对账因此失败；已将段落识别锚定到声明行，
从相同schema字节重提取为v2，原失败原型隔离在仓库外，全部对账重跑。没有改schema、
STEP样件或真值使其迁就实现。当前见证保留PATH.edge_list和多继承；产品运行不依赖提取器。

额外真实CLI测试覆盖OID歧义、APD年份不推版、SI所有prefix、无/重复/错误名义单位、rad/sr、
optional与derived、原始transport kind、普通与旧context、重复标签的多product拒绝、
稀疏重编号/记录反序、未知facet、单位/引用环延后、转换dimensions仍显式、依赖访问N/N−1、
总报告字节N/N−1及不发布部分语义图。六个checker控制分别篡改祖先、SI尺度、几何资格、
反向用户、字段来源和后续RULE状态，均被独立比较拒绝。

## 正式运行与身份

证据根由调用者指定；本机路径记录在本地HANDOFF，其`index.html`为入口。以下相对该根：

- 全49项：`runs/20261016T002648Z-9730cc108765/`，格式`t03.evidence.v1`。
- 构建/两个配置的C++检查/DLL审计：`qualification/build-20261016T002502Z-c63dc293989e/`。
- Debug/Python3.14：`qualification/tests-Debug-python14-20261016T002647Z-4acb1ebdc747/`。
- Release/Python3.10：`qualification/tests-Release-python10-20261016T002647Z-f5e99e1ad1bd/`。
- 汇总与二进制/来源：`qualification/acceptance.json`；Release EXE保存在`qualification/native/`。

Release EXE SHA256：`aa051ceca19c5753a1aeb7374faf43015db3f563bcade0606dd15f2038e64e5b`。
组件/验证代码/见证集合SHA：`0b26fd45d1e38caae500071d10faf856961b01fd892b9e7a7eb1a1fed1536595`
（acceptance.json的sources映射按sorted compact JSON计算）。
MSVC19.50.35728；实际命令、exit、日志hash、解释器和构建配置均已记录。
Release DLL依赖只有标准C++/C运行库及KERNEL32，无OCCT、SMLib或USD。

每项保存真实输入副本、冻结inventory/expected、未编辑native stdout、调用及二进制hash、
独立比较及可读HTML。checker重核来源、必需角色、原值、完整人口、字段声明和scope，
staging校验后才发布新run。Complete指证据完整，不指完整模型已接受。

01工具SHA `b9823048879844ec657220ecbfbbaf3edf6d25b7cbcaf070c25bb810a0c6f152`、
02源层/adapter集合SHA `d0c2aed3a49e7787ec0088ff32f163b7a3363ebd266a14cd328ca5e81519132a`、
01的303项材料及总体规格0.1 SHA
`9a6f364e38eeb43fee5bb7b05e151eb7102cc80f402ae24f508adc55a566d6fa`均未变。

本轮没有全仓构建/门禁、模型库运行、跨平台或sanitizer资格、任意OOM/worker取消证明、
几何/native建模或网格修复。没有提交、推送或远端issue；04及以后仍on-hold。
