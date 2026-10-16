# AP203e2首profile：单缺陷反例的输入与预期诊断

版本：203e2-SampleCases-0.1，2026-10-15。本文与[样件主册](ap203e2-sample-design.md)、[平面全部记录](ap203e2-sample-plane.md)、[盒体全部记录](ap203e2-sample-box.md)合用。**36项全部Designed/NotRun；没有生成STEP／expected文件、编写实现或运行验收。** 稳定样件ID为S203-N01—N36；审计册、PF-N/B-N及既有C203建议只是关联依据，不叠加为新样件数。

每行输入都是指定父例＋唯一mutation，其余原始数据、HEADER、Q、profile绑定、选根和repair=None不变。记录替换不保留旧行，只有N02明确追加重复ID；不能将多个反例合在一起。父例的精确记录和独立数学真值由主册13项正例固定。

独立真值分三种：仍然明确的原始定义／拓扑事实；改变后可解析计算的值；因输入自相矛盾而不存在一个可交付的合法shape。不能一律要求坏输入仍能“算出父例面积/体积”。文档中阶段D0—D5与诊断语义见主册；这些不是已存在或已冻结的公开错误码。

## 1. 文件、引用与属性反例

| ID／父例 | 唯一输入变化：精确old→new | 独立真值／仍成立的前提 | 预期首个阻断与必要证据 |
|---|---|---|---|
| S203-N01／P01 | FILE_SCHEMA字符串中OID最后分量`{1 0 10303 403 2 1 2}`→`{1 0 10303 403 2 1 3}`，名称不改 | 全部DATA、F1数值/拓扑未改；声明不再绑定已固定制品 | D2／UnsupportedSchemaBinding；完整源名/OID及期望profile，不能靠APD或年份猜回e2 |
| S203-N02／P01 | DATA结束前追加`#100=CARTESIAN_POINT('',(3.,-2.,5.));`，原`#100`保留 | DATA行数增加1，实体编号歧义；不能选最后一条解释几何 | D0／DuplicateInstanceId；必须给出两处位置及ID100，不称坐标重复或PRODUCT唯一键问题 |
| S203-N03／P01 | `#9.definition`，参数1：`#8→#99999` | 无`#99999`；几何记录原样，但产品shape关联无法解析 | D1／UnresolvedRequiredReference；#9字段1及缺失ID；不倒猜底层solid/面作为根 |
| S203-N04／P01 | `#3.name`，参数1：`''→$` | label必需；其他主链及F1数值没改，合法空串不同于缺失 | D1／RequiredAttributeMissing；PRODUCT_CONTEXT.name，不误归mechanical discipline条件 |
| S203-N05／P01 | `#204`参数2：`*→$` | OE底层edge引用不变；此槽是显式转派生占位，不是optional | D1／DerivedSlotMarkerRequired；#204槽2及继承声明，不伪造顶点为空/PATH失败 |
| S203-N06／P01 | `#204.orientation`，参数5：`.T.→.U.` | BOOLEAN不接受LOGICAL Unknown；底层VP/EC/LINE仍完整 | D1／BooleanValueInvalid；未得到合法方向，不计算derived端点后再报几何问题 |
| S203-N07／P01 | `#203.edge_start`，参数2：`#110→#100` | #100存在且为CP，而字段要求VERTEX；CP坐标与原VP点相同也不等价 | D1／ReferenceTypeMismatch；expected VERTEX、observed CARTESIAN_POINT及精确角色 |
| S203-N08／P01 | `#300.edge_list`：`(#204,#214,#224,#234)→(#204,#204,#224,#234)` | 同一OE实例在LIST重复；还破坏连接并使#210—#214五个RI失去使用路径，原VP/CP仍被邻边/顶点使用 | D1／AggregateUniqueViolation，PATH.edge_list；D2孤立、D3 RI.WR1/PATH.WR1是同因后果，不能去重或删孤立块后继续 |
| S203-N09／P04 | `#22.cfs_faces`在原六成员后再追加一次`#507` | SET中同一AF身份重复，全部原面仍被引用；空间定义未变 | D1／SetDuplicateMember；#22字段2及重复成员位置，不静默SET去重 |
| S203-N14／P01 | `#10`的NAMED_UNIT局部槽：`*→$` | SI_UNIT将dimensions重声明派生；prefix/name仍有效 | D1／DerivedSlotMarkerRequired；#10.NAMED_UNIT.dimensions；不是单位缺失或自动mm |
| S203-N15／P01 | `#11.SI_UNIT.prefix`：`$→.QUETTA.`，name仍为`.RADIAN.` | enum token可解析，但固定N2635的si_prefix枚举没有该值；无默认prefix可代入，其他几何记录不变 | D1／EnumerationValueInvalid；#11的prefix及固定枚举依据。不能因没有物理角消费者就跳过，不能扩展旧schema或擅自当无prefix |

## 2. 源规则反例

这些反例的语法、字段类型和首profile主要范围保持可解释；已有明确源约束反证，不能只报无法判定或等待native失败。

| ID／父例 | 唯一输入变化 | 独立真值与条款 | 预期首个阻断及连带后果 |
|---|---|---|---|
| S203-N10／P01 | `#2`参数2的原APD schema-name追加`_mim_lf` | HEADER仍绑定；唯一APD不匹配G02原常量，匹配数量为0 | D3／SourceRuleViolation：application_protocol_definition_required.WR1；记录AC#1/APD#2。不是未知HEADER/schema |
| S203-N11／P01 | `#200.direction_ratios`：`(0.6,0.8,0.)→(0.,0.,0.)` | 三分量全0，DIRECTION.WR1确定False | D3／SourceRuleViolation：DIRECTION.WR1；不归一零向量，不降为“近零数值误差”或先拟合AB |
| S203-N12／P01 | `#201.magnitude`：`12.→-12.` | REAL字段可解析；VECTOR.WR1要求≥0，确定False | D3／SourceRuleViolation：VECTOR.WR1；不能取绝对值或当合法方向反转；LINE后续不可用是同因后果 |
| S203-N13／P01 | `#132.direction_ratios`：`(0.6,0.8,0.)→(0.,0.,1.)` | #131/#132都是非零Z，显式axis与ref叉积模长为0；#132仍被#133消费，无孤立RI | D3／SourceRuleViolation：AXIS2_PLACEMENT_3D.WR4；不切默认X。LINE定义未动，plane frame无法成立 |
| S203-N23／P01 | `#214.orientation`：`.T.→.F.` | 内部路径变A→B、C→B、C→D、D→A；两处邻接身份错，LIST仍UNIQUE，首尾仍A | D3／SourceRuleViolation：PATH.WR1；给#204→#214及#214→#224邻接。不能标成仅面法向错 |
| S203-N24／P01 | `#233.edge_end`，参数3：`#110→#111` | 路径A→B→C→D→B，全部相邻身份仍接，PATH.WR1为True；第一start A≠最后end B | D3／SourceRuleViolation：EDGE_LOOP.WR1；e3终点B离其LINE12mm是D4后果，不替代收尾反证 |
| S203-N29／P08 | `#14.name`，参数3：`distance_accuracy_B→distance_accuracy_A` | 同一GUUNC内两不同UMWU身份、相同name；值/单位均仍正且有效 | D3／SourceRuleViolation：consistent_uncertainty.WR1；#12→#13/#14。不要求跨context名字唯一 |
| S203-N30／P08 | `#13.value_component`：`LENGTH_MEASURE(0.0001)→LENGTH_MEASURE(0.)` | LENGTH_MEASURE本身可为0；UMWU.valid_measure_value数字分支要求>0，确定False | D3／SourceRuleViolation：UNCERTAINTY_MEASURE_WITH_UNIT.WR1；不是GUUNC缺失，不补默认精度 |
| S203-N31／P11 | `#17.length_exponent`，参数1：`1.→0.` | 声明维数变全0，factor基单位仍mm、推导维数仍length | D3／SourceRuleViolation：LENGTH_UNIT.WR1与CONVERSION_BASED_UNIT.WR1均有反证；给#19/#17/#18/#10因果链。不能凭label inch改回 |

N24专门隔离PATH相邻与EDGE_LOOP收尾的不同责任。它不是“只有一个表达式失败”：同一端点变更还造成几何不一致，但在源规则阶段就已有足够拒绝证据。

## 3. 合法表达越出profile的反例

此处预期是Rejected／Unsupported原因，不授予源整体合法性；已识别但不准入的路径不继续进入未证明的分支。

| ID／父例 | 唯一输入变化 | 独立事实 | 预期首个阻断 |
|---|---|---|---|
| S203-N16／P01 | `#6.name`：`part definition→part occurrence` | 普通PDC字段仍是合法label；物理几何不改，产品角色解释已改变 | D2／UnsupportedDefinitionContextRole；保留新值，不能继续沿part-definition的不触发证明放行，也不冒称PDC局部WHERE失败 |
| S203-N17／P01 | `#301`类型名：FACE_OUTER_BOUND→FACE_BOUND，三槽不改 | 显式FOB数量为0；FACE.WR2的≤1仍成立；矩形loop和世界定义不动 | D2／UnsupportedExplicitOuterBoundRequired；不猜第一个普通bound为外环，不误报两个外环源错误 |
| S203-N18／P01 | `#302`类型名：ADVANCED_FACE→FACE_SURFACE，四槽不改 | plain FS字段布局已知，但首profile只准AF；MSSR普通面分支存在已登记原文访问问题 | D2／UnsupportedFaceSubtype；不把同槽数当可转换，不修原schema后偷偷接纳 |
| S203-N35／P09 | `#15.name`：`part→ply` | 分类文字改变、主几何不改；会使原来被排除的复合材料条件可能触发 | D2／UnsupportedProductCategory；不把category当无影响元数据，不执行未展开的ply图后发布部分结果 |
| S203-N36／P01 | 普通`#3.discipline_type`：`mechanical→electrical` | PRODUCT_CONTEXT本体没有mechanical WHERE，旧MECHANICAL_CONTEXT型未出现 | D2／UnsupportedProductContextDiscipline；这是首片项目范围，不误报不存在的MECHANICAL_CONTEXT.WR1 |

## 4. 独立几何与单位解释反例

| ID／父例 | 唯一输入变化 | 独立真值／保持的前提 | 预期首个阻断及禁止的“修复” |
|---|---|---|---|
| S203-N19／P01 | `#201.magnitude`：`12.→0.` | VECTOR.WR1的≥0仍True；LINE成为恒点A，B离此定义12mm，不能承载非零边 | D4／NonzeroDefiningLineRequired；禁止伪报VECTOR.WR1失败或直接连接VP重建LINE |
| S203-N20／P01 | `#202.pnt`，参数2：`#100(A)→#102(C)` | 无限LINE等价沿源平面Y轴`(-0.8,0.6,0)`平移8mm；A/B到新LINE距离均8mm，仍在原PLANE上；源投影参数−1→0与same_sense=T一致 | D4／EndpointCurveMismatch，observed=8mm >1e−7mm；给EC#203/LINE#202及VP链，不误报离平面或仅域非[0,1] |
| S203-N21／P01 | `#130.coordinates[3]`：`5.→5.001` | 独立plane原点上移1/1000mm；4条原LINE、全部长度8或12mm的边及VP不动，每条边到新plane距离0.001mm | D4／EdgeSurfaceMismatch，observed=0.001mm >1e−7mm；不snap顶点/边，不用native较宽tol放行；source不存在同一合法裁剪面 |
| S203-N22／P01 | `#203.same_sense`，参数5：`.T.→.F.` | EC起终VP仍A→B，对原LINE的t仍0→1；False却声明与递增相反，方向契约矛盾；PATH身份未变 | D4／EdgeCurveSenseMismatch；给t_start=0、t_end=1及source sense。不单凭False拒绝，不能sort参数后忘关联 |
| S203-N25／P02 | 孔`#311.orientation`：`.T.→.F.` | 外环有向面积+96不变；孔有效路径面积由−8变+8，face.same_sense仍True，所有源曲线/点/身份闭合不改 | D4／InnerBoundaryOrientationMismatch；不是BOOL或PATH错误，不填孔、不自动翻孔，不把104当合格face面积 |
| S203-N26／P01 | `#22`类型名：OPEN_SHELL→CLOSED_SHELL，faces不改 | MSSR/SBSM可以指CLOSED_SHELL；所有RI仍可达，但唯一面有4条自由边、总长40mm | D4／ClaimedClosedShellHasBoundary；不得改回OPEN后发布，不把CLOSED类型名当闭合证明或请求solid-volume |
| S203-N27／P04 | 底面`#507.same_sense`：`.T.→.F.` | 支撑面/点/LINE不变；所选底面法向−Z→+Z，与期望外法向点积−1；bound/face及壳定向可能同时不一致 | D4／FaceBoundaryOrShellOrientationMismatch；要有方向链/外侧证据，不要求唯一某个体积错误值，不按字段False直接拒绝 |
| S203-N32／P11 | `#18.value_component`：`LENGTH_MEASURE(25.4)→LENGTH_MEASURE(0.)` | dimensions、LMWU名义长度类型仍正确；LENGTH_MEASURE本体无正值约束，但单位scale严格为0，违背项目正可逆尺度契约 | D4／UnitScaleNotPositive，observed=0；不伪报conversion.WR1或LENGTH_MEASURE WHERE失败；不展开成塌缩native盒体 |
| S203-N33／P11 | 同一字段：`LENGTH_MEASURE(25.4)→LENGTH_MEASURE(-25.4)` | D3量纲关系仍成立；scale为−127/5，明确违反项目正尺度；不据此当镜像occurrence | D4／UnitScaleNotPositive，observed=−25.4mm/unit；不得取绝对值、按inch标签覆盖或放宽预算 |
| S203-N34／P13 | `#18.unit_component`，参数2：`#10→#19` | 显式维数仍length，MWU类型仍正确；factor依赖环为#19→#18→#19；#10仍由UMWU#13使用，无新增孤立记录 | D4／CyclicUnitDefinition；输出具体环和记录字段，不无限递归。无唯一正SI终点scale，不假定inch=25.4或将其伪报文件语法环 |

“最早D4”表示源语法、范围和已列schema约束仍有确定前提，项目的独立几何／单位解释已有充分反证。具体实现可在建立typed单位视图时先检这些数值；报告应归同一逻辑责任，不能用调用顺序把项目尺度要求伪称成原文WHERE。

## 5. 单注入但被更早门槛遮挡的控制

| ID／父例 | 唯一输入变化 | 独立因果链 | 预期主诊断与附加证据 |
|---|---|---|---|
| S203-N28／P04 | 仅从`#22.cfs_faces`删除`#507`这个成员，其他记录全保留 | N仍145；底面#501—#507及OE#400—#403共11个RI失去using-representation路径；剩余5面面积2000、缺口边界长60mm，但不再是一个合法闭壳输入 | D2／UnownedOrOrphanProfileContent是最早范围阻断；D3 RI.WR1有独立后果证据；若进行安全几何诊断还能发现4条自由边。不得强制首诊断只有NonManifold，也不把它称“schema全过的纯缺面” |

N28保留在目录是为了验证完整记录闭包和诊断因果。若未来要只测几何闭壳谓词而不受孤立RI遮挡，使用N26；不悄悄删除N28孤立块使它变成另一个未登记输入。

## 6. 执行前必须固定的报告断言

所有36项预期均为完整请求不交付模型，`model=None`、`repair=None`；源阶段阻断不启动native构建，也不改变原有已发布模型。只有已经证明的反证才能标SourceInvalid；Unsupported不证明整个源合法，NumericalIndeterminate不满足已有精确反证样件的预期。

每行至少断言：ID/父例与未来真实hash、单mutation定位、正确逻辑阶段、语义诊断、涉及实体及源字段/聚集索引、对应schema条款或项目合同、确切/有界observed和limit、source因果链、没有模型。诊断文案、公开enum数值和同层附加错误顺序不作本轮承诺。

只有13个正例和这些反例实际生成、字节/图与设计逐项一致、预期诊断执行完成后，才能记录Passed/Failed；现在每项都是NotRun。源码/基线不因本目录完成而改变，不把拒绝错误输入等同于正常输入构建或失败清理已经通过。
