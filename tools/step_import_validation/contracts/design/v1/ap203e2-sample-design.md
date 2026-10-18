# AP203e2首profile：最小样件与单缺陷验收设计

版本：203e2-Samples-0.1，2026-10-15。依据[首profile合同](ap203e2-planar-profile.md)、固定N2635闭包及[独立数学真值](p0-planar-cases.md)。**只细化文档中的预期输入与判据，全部Designed/NotRun；未生成`.step`、expected数据或测试程序，未调用CAD读取器、规则执行器或内核。**

“最小”指选定几何的必要产品／表示／单位／拓扑闭包，不附加非必要语义。不是证明文件字节数或记录数达到数学上的全局最少；允许为可审阅性给不同几何角色保留独立记录。下文“合法”“Accepted”均是待验证的设计预期，不是已执行的AP符合性或导入结论。

## 1. 完整输入如何唯一确定

一个预期文件＝下方固定HEADER与12条共同DATA记录＋[平面册](ap203e2-sample-plane.md)或[盒体册](ap203e2-sample-box.md)的全部记录＋两条固定结束标记。派生正例在其指定父例上应用本册完整变更集；每个反例只在指定且未修改的正例上应用[反例表](ap203e2-sample-cases.md)的一条变更，不累计不同反例。

首次实现使用ASCII子集的UTF-8、无BOM、LF，所有数值按表中精确十进制／有理数来源解释。文件名与实体名不决定单位或几何；预期case ID保存在验收清单中，不靠PRODUCT.name区分模型身份。实际字节hash、字节位置及输入长度待生成后冻结；本轮不填虚构hash或offset。

### 1.1 固定HEADER与结束标记

以下是样件文本的设计内容，未保存为可加载文件。作者、组织和producer均为合成测试标签，不描述真实生产来源：

```text
ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('AP203e2 planar profile design fixture'),'2;1');
FILE_NAME('p0-analytic-design.step','2026-10-15T00:00:00',('P0'),('local'),'P0 design only','synthetic analytic oracle','');
FILE_SCHEMA(('AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}'));
ENDSEC;
DATA;
```

完整DATA记录之后为：

```text
ENDSEC;
END-ISO-10303-21;
```

这里固定单DATA、`2;1`及本profile的simple／六种已列complex映射；多DATA、CC2和其他载体控制属于P1及其他样件，不能据此宣布其通过。HEADER不计入DATA记录数；最大`#id`也不是记录数。

### 1.2 所有基础正例共用的12条DATA记录

```text
#1=APPLICATION_CONTEXT('configuration controlled 3d design of mechanical parts and assemblies');
#2=APPLICATION_PROTOCOL_DEFINITION('international standard','ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011,#1);
#3=PRODUCT_CONTEXT('',#1,'mechanical');
#4=PRODUCT('P0-PART','P0 analytic fixture',$,(#3));
#5=PRODUCT_DEFINITION_FORMATION('A',$,#4);
#6=PRODUCT_DEFINITION_CONTEXT('part definition',#1,'design');
#7=PRODUCT_DEFINITION('D1',$,#5,#6);
#8=PRODUCT_DEFINITION_SHAPE('',$,#7);
#9=SHAPE_DEFINITION_REPRESENTATION(#8,#20);
#10=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));
#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));
#12=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) REPRESENTATION_CONTEXT('local','3D'));
```

其中`#2`的APD schema-name按N2635原RULE使用小写且**没有`_mim_lf`**；HEADER是另一项声明，不用HEADER全文覆写APD。`international standard`和2011为合成文件中明确选择的协议声明值，不认证研究制品与ISO最终附件的字节关系。字段依据见[产品/context清单](ap203e2-planar-context-closure.md)及[全47条规则](ap203e2-planar-global-rules.md)。

`#8.name`为空串，使surface-condition函数按规定返回UNKNOWN、外层失败QUERY为空；不是不检查SDR/PDR。APD/PC/PDC指向同一AC，AC.inverse有两个context element；`#12`被唯一representation使用。单位只有长度与平面角，无sr／uncertainty／category／额外axis item；这些是profile允许省略的内容，不是隐式默认值。

### 1.3 基础正例的构成与记录数

| 样件ID | 确定输入 | DATA记录数N | 独立形状角色 |
|---|---|---:|---|
| S203-P01 | 共同12条＋F1平面册39条，全部AF／LINE／PLANE | 51 | 单个开放旋转矩形 |
| S203-P02 | P01＋F2孔扩展的30条，替换AF.bounds为外环＋孔 | 81 | 开放带孔面 |
| S203-P03 | P02，仅替换`#130.coordinates`为`(3.4,5.2,5.)` | 81 | 同一带孔面，源UV原点改变 |
| S203-P04 | 共同12条＋B1盒体册133条 | 145 | ABSR→MSB材料实体 |
| S203-P05 | P04，仅用盒体册所列MSSR/SBSM的`#20/#21`合法角色替换集 | 145 | 同几何的闭合表面模型 |

这些记录数按设计图逐项计数，不来自读取器。带孔增量、角色替换集都是完整正例定义，**不是**“单处缺陷反例”；它们需要协调多项记录且不丢旧记录的引用。P01—P05的具体顶点、边、OE、plane frame、loops和全部关联由两份样件册固定。

## 2. 正例的独立真值与预期结果

所有请求显式绑定`203e2-P0-Planar-0.1`、选择唯一`#7`产品定义、使用测试候选Q-P0-mm-01、修复None。Q的定义误差`1e-8 mm`、几何一致性`1e-7 mm`、派生UV三维误差`1e-7 mm`与工作tol分开；不是生产默认。source uncertainty缺席只记录“未声明”，不补native默认精度。下表的接纳指未来P2完整路径；未来P1 inspect只产源检查结果，不交付BRep。

| ID | 几何／组合真值 | 关键查询、参数或资格 | 预期诊断／结果 |
|---|---|---|---|
| S203-P01 | F1面积96、边界40；4V/4E/1F、1loop；bbox `[-3.4,10.2]×[-2,12.4]×{5}`，形心`(3.4,5.2,5)` | plane `S(u,v)=O+uX+vY`，外域`[0,12]×[0,8]`；每边源t=`[0,1]`，N=+Z | 预期Accepted，开放表面，无材料体积资格；无Error，无repair；派生UV/window按规范化记录 |
| S203-P02 | F2面积88、总边界52；8V/8E/1F、2loops；bbox同P01；形心`(189/55,297/55,5)` | UV(10,6)在面内，(4,3)在孔内，(2,3)在孔边；近点`(2.8,1.9,8)`到面距离`√37/2` | 预期Accepted；孔不填补；内孔角色及方向完整，不因总面积相等就放行 |
| S203-P03 | 世界几何、全部距离、面积及拓扑与P02相同 | `u'=u-6,v'=v-4`，外window`[-6,6]×[-4,4]`；O不是window左下角 | 预期Accepted；记录源→canonical→native参数对应，不移动trim世界几何 |
| S203-P04 | 盒体`[2,12]×[-3,17]×[5,35]`；8V/12E/6F、24有向uses；面积2200、体积6000、形心`(7,7,20)` | (7,7,20)内、(1,7,20)外；(15,7,20)最近`(12,7,20)`、距离3；每edge两反向面use | 预期Accepted材料实体；无静默翻面、缝合或tol增长。截面/建模是另行执行资格，不凭本表授予 |
| S203-P05 | 世界边界、面积、8/12/6及闭合性同P04；角色不同 | 表面模型无材料InsideSolid／volume资格；即使有向体积积分可算6000，也不变成solid | 预期Accepted闭合表面模型；volume资格NotApplicable，不以数值0冒充查询成功 |

主真值来源是显式坐标、平面方程和有向身份表；不由任何CAD reader、mesh或native CreateBox返回值生成。源码规则通过与几何正确分栏，已有native探针也不算这些样件已运行。

### 2.1 合法变化的正控制：防止误报

| ID及父例 | 完整输入变化集 | 独立真值 | 预期诊断 |
|---|---|---|---|
| S203-P06，父P01 | 仅`#10.SI_UNIT.prefix`由`.MILLI.`改`.CENTI.`，其他数据不改，N=51 | 物理模型关于世界原点放大10倍：bbox `[-34,102]×[-20,124]×{50}`、形心`(34,52,50)`、面积9600、边界400；V/E/F不变 | 预期Accepted新的物理形状；不能因标签未改或与P01体量不同报单位错误，更不能自动恢复mm |
| S203-P07，父P01 | `#10`改CENTI；`#100..103`和`#130`的全部坐标除10；四个VECTOR `#201/#211/#221/#231`的magnitude除10，其余不改，N=51 | 世界几何及面积/距离与P01完全相同；源LINE t仍`[0,1]`；源plane UV变`[0,1.2]×[0,0.8]`，canonical仍`[0,12]×[0,8]` | 预期Accepted；单位归一化与参数变化有来源。此为协调的等价重述，不是单缺陷 |
| S203-P08，父P01 | 增加下节`#13/#14`两条UMWU，`#12`加入GUUNC((#13,#14))，N=53 | F1几何不变；uncertainty分别`1e-4/2e-4 mm`，名字不同；Q仍`1e-7 mm`一致性阈值 | 预期Accepted；保留两声明及较宽源uncertainty告警，不自动涨tol或放宽Q |
| S203-P09，父P01 | 增加`#15=PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#4));`，其余不改，N=52 | F1几何与产品主链不变，增加一项明确分类 | 预期Accepted；分类保留，不补id-owner/approval，不移植e1或AP214的规则 |
| S203-P10，父P01 | `#3`类型改MECHANICAL_CONTEXT；`#6`类型改DESIGN_CONTEXT；`#5`改指定source子型并追加`.NOT_KNOWN.`，其余字段不改，N=51 | F1几何不变；三个源子型及采购/自制声明发生明确变化，不能称整个产品metadata不变 | 预期Accepted；执行旧型局部WHERE、保留原型与显式source，不因deprecated拒绝 |
| S203-P11，父P04 | 按第3节重述U1英寸盒体，增加`#17/#18/#19`，替换8点、12个magnitude与GUAC长度引用，N=148 | 世界尺寸`25.4×50.8×76.2 mm`，面积14193.52、体积98322.384、形心`(12.7,25.4,38.1)`；8/12/6 | 预期Accepted；conversion dimensions显式引用，按factor的127/5求scale，不按`inch`文字猜 |
| S203-P12，父P01 | 增加`#16=(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT());`；`#12.units`变为`(#10,#11,#16)`，N=52 | F1几何不变；明确增加sr声明，原P01缺sr仍合法于本profile | 预期Accepted；sr保持立体角类型，不与rad互换；不反推e2普遍强制三单位 |
| S203-P13，父P11 | 增加`#13=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001),#10,'distance_accuracy_A',$);`；`#12`加入GUUNC((#13))，主units仍为`(#19,#11)`，N=149 | U1世界几何与P11相同；新增源uncertainty为`1e-4 mm`，其单位引用把`#10`锚定在主闭包内 | 预期Accepted并记录uncertainty告警；作为单位环反例的独立合法父例，避免环变更顺带造成毫米unit孤立 |

以上每个子例均从指定父例单独派生，不叠加P08、P09等其他可选记录。正例类别中的多记录变化是定义完整的协调变换，不能把它们混记成“一个token的单缺陷”。

P08的确切新增记录与context替换为：

```text
#13=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001),#10,'distance_accuracy_A',$);
#14=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0002),#10,'distance_accuracy_B',$);
#12=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#13,#14)) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) REPRESENTATION_CONTEXT('local','3D'));
```

这里的`#12`替换旧记录，不追加重复ID。P10中`#5`的完整替换为`PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('A',$,#4,.NOT_KNOWN.)`；`not_known`是明确枚举值，不能由缺失自动补出。

P13的context替换为`(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#13)) GLOBAL_UNIT_ASSIGNED_CONTEXT((#19,#11)) REPRESENTATION_CONTEXT('local','3D'))`。它只属于P13，不悄悄加入P11或其他最小父例。

## 3. S203-P11：conversion单位正例的全部增量

保留P04的盒体图、OE路径、各plane的axis/ref和placement引用；各plane的location已经引用相应顶点点，因此重述坐标时无需另改平面原点记录。按下表替换全部8个CARTESIAN_POINT.coordinates，其他点记录不新增：

| 点记录 | 新坐标，源inch | 点记录 | 新坐标，源inch |
|---|---|---|---|
| #100 | (0.,0.,0.) | #104 | (0.,0.,3.) |
| #101 | (1.,0.,0.) | #105 | (1.,0.,3.) |
| #102 | (1.,2.,0.) | #106 | (1.,2.,3.) |
| #103 | (0.,2.,0.) | #107 | (0.,2.,3.) |

替换12个VECTOR.magnitude：`#201/#221/#241/#261=1.`；`#211/#231/#251/#271=2.`；`#281/#291/#301/#311=3.`。LINE的pnt及direction引用不改，所有源edge t仍为0→1。注意盒体的`#301/#311`是VECTOR，平面册同号的FOB/孔bound属于另一独立文件，不能跨文件混用。

新增以下三条，保留原`#10`毫米SI unit作为factor基单位；GUAC只把主长度成员`#10`替换成`#19`，原`#10`仍经factor可达，不是孤立unit：

```text
#17=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);
#18=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10);
#19=(CONVERSION_BASED_UNIT('inch',#18) LENGTH_UNIT() NAMED_UNIT(#17));
#12=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#19,#11)) REPRESENTATION_CONTEXT('local','3D'));
```

尺度严格为`25.4=127/5 mm/inch`；采用U1而不把B1的10mm等长度写成截断的`50/127 inch`。数学真值从整数inch坐标与127/5的精确尺度推导；不从被测converter输出回填expected。转换链的负／零／循环反例由该父例构造，详见反例目录。

## 4. 单缺陷的定义、检查顺序和诊断合同

单缺陷指**只向已固定父例注入一个声明的根因**，可以是一处字段、一个LIST成员或一个记录删除；不保证只违反一条约束。例如翻一个OE.orientation会同时破坏相邻身份连接和收尾，删shell引用可能使许多RI失去使用者。预期主诊断必须绑定最早有足够证据的失败，其他已证明后果以关联诊断记录；不能用任意后果冒充唯一错误。

诊断名称在本轮只是语义标签，不冻结公开C++错误枚举或checker内部顺序。下面规定的是可比较的逻辑门槛；同一门槛内不要求遍历次序或只准报一条。较早失败后不允许调用native去“帮助决定”源是否错误。

阶段编号表示诊断责任及前提，不强迫某个实现先后调用所有阶段。例如HEADER的schema绑定可在D0后立即判定；未绑定时不得先按猜定的N2635执行typed检查，即使其诊断责任列为D2。只有前提齐备的源规则/数值反证才要求相应诊断。

| 阶段 | 必要证据 | 本组典型问题 |
|---|---|---|
| D0 文件捕获/词法/记录索引 | HEADER与DATA语法可确定、原始位置/ID无歧义 | 缺分号、重复ID、损坏字符串；不能靠选根绕开 |
| D1 引用/属性/类型/complex | 所有引用可解析、参数数、required/optional/derived、BOOLEAN/SELECT、聚集及签名合法 | OE星号错、dangling引用、PLANE当curve、重复SET成员 |
| D2 精确profile范围 | 已知来源绑定、唯一主图、允许的叶/六complex、角色值/附属归属 | 未支持schema/普通FS、第二产品、特殊context/category名、无显式FOB |
| D3 源局部/全局/反向规则 | 真实TYPEOF总体、INVERSE/UNIQUE、WHERE、全47条RULE与函数谓词 | APD比较值错误、零方向、负VECTOR、PATH身份断开、同GUUNC重name |
| D4 独立几何/有向区域 | 源定义及身份完整，计算残差/参数/方向/区域/壳的证据可靠 | 零LINE、EC sense矛盾、LINE平移8mm、plane离边、孔方向或闭壳面侧错误 |
| D5 目标构建与交付 | 定义/域/UV/owner/tol/validator、资源清理和发布条件满足 | 正例预计完整owning快照；本轮不做native故障注入样件 |

D2只处理类型、角色及范围，不把所有数值检查提前塞入“支持范围”。能否计算有效轴、非零LINE、误差是否超Q需按其源规则或D4证据裁决。若同一缺陷还违反较早门槛，目录明确写出遮挡关系；局部规则控制与端到端主诊断不混同。

反例的共同结果为Rejected且本次模型为空，原有已发布结果不变；原因区分`SourceSyntax/Reference/Attribute/Rule`、`UnsupportedProfile`、`GeometryMismatch/InvalidRegion`、`Indeterminate`。Unsupported是Rejected的原因类别，不是第四种可部分发布结果；真正IO/内部故障的Failed与用户取消Cancelled不靠伪造STEP缺陷替代验证。

每个预期诊断至少含：case及父例、source profile/hash、源`#id`及字段/聚集索引、阶段、语义原因、适用schema条款或项目限制、实际值/期望或残差/单位/阈值、因果关联、原始source span。尚未生成的byte offset写未定；无证据的相关诊断不填“通过/失败”。

表中的8mm、0.001mm等为数学真值；未来数值observed必须附误差界，区间包含该真值且足以证明越过Q。无需要求浮点数与有理数逐bit相等，也不能以任意round替代反证。

## 5. 验收如何保持独立

1. 正例输入由本册固定封套和两样件册重建；生成后先按数据表对账，不由native CreateBox／STEP writer反导出作为唯一输入来源。
2. 几何oracle用明确有理数坐标、归一方向、LINE/PLANE方程、二维有向面积/相交/包含及轴区间；组合oracle用固定VP/EC/OE/loop身份与面侧表。误差证明与Q-P0-mm-01分开，不拿reader多数票替代。
3. 每个反例与父例的差异必须只包含目录定义的mutation；相关定义冻结，不能顺便“修正”单位/方向/容差。若输入不能隔离目标诊断，应改设计或记为更早门槛控制，不能改expected来遮掩。
4. 源阶段正例、native正确、查询/截面/建模资格分别记录。没有volume资格的表面模型不请求成功solid-volume；没有运行的样件不填零误差/零泄漏。
5. 在未来生成时固定N、实际字节B、解析深度D和hash，再做N/N−1、B/B−1、D/D−1资源边界；本轮有图计数N，无实际B/D或运行时间证据。所有失败仍执行无部分发布契约。

## 6. 本轮完成边界

已把13个正例、统一封套、全部具体几何输入／合法增量、独立几何与拓扑真值以及[单缺陷反例目录](ap203e2-sample-cases.md)设计到可逐项复写的程度。输入记录表和诊断因果另经[反例审计](ap203e2-sample-negative-audit.md)复核；该审计建议不额外计入fixture数。

仍未做：生成真正STEP/expected、解析/规则执行、native物化、故障注入或模型库读取。文件语法、完整profile及数学预期均待未来执行证据验证；已确认主规格0.1保持原字节，不授予正式AP认证或运行时接纳资格。
