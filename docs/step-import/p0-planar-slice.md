# P0 附件：首个平面／盒体切片的输入与参数契约

版本：P0-A0.4，2026-10-15。依据[已确认设计基线0.1](../step-import-repair-spec.md)。本附件细化P0文档交付，不修改已确认政策，不代表P0/P1/P2实现或验收完成。**暂不编码：仅在Markdown内列样件记录和字段设计，未生成STEP文件、parser或测试程序，未编译、未调用CAD读取器、未读取新的真实模型。**

阅读入口：

- 本文：范围、文件/schema登记、清单索引、单位/参数/方向、验收顺序。
- [独立真值与用例表](p0-planar-cases.md)：明确坐标、环/面、面积/体积/分类真值和NotRun用例。
- [实体与属性来源表](p0-entity-source-audit.md)：38类几何/拓扑/表示/产品记录的精确simple属性顺序、类型、optional/derived、集合基数与来源。
- [单位与context来源表](p0-unit-source-audit.md)：15类单位/context记录、complex局部字段、源单位和参数定义。
- [SMLib目标契约审计](p0-smlib-source-audit.md)：固定源码下的目标构造、域/UV/所有权规则及待运行门槛。
- [精确schema候选与冻结门槛](p0-schema-profiles.md)：五份MBx-IF文件、AP203e1修改副本、版次差异和仍未关闭的规则闭包。
- [AP203e2首个profile及完整静态闭包](ap203e2-planar-profile.md)：36种simple叶、六complex签名、全47条RULE、产品/单位/几何/反向函数依赖与验收控制。
- [AP203e2最小样件与反例设计](ap203e2-sample-design.md)：固定封套、平面/盒体记录、13正/36反输入、独立真值、分阶段诊断与因果遮挡，全部NotRun。

前两份来源表共同构成本附件的逐实体属性清单；不另复制一套容易漂移的属性表。源语义以原版schema/资源定义为依据，OCCT只作参数序列与实现交叉证据，SMLib源码只决定target契约。

<a id="p0-envelope"></a>
## 1. 首个切片的能力包络

| 项目 | 本切片接受候选 | 准确拒绝的边界 |
|---|---|---|
| 根 | 单零件定义经PDS/SDR直接关联具体ABSR或MSSR；可事先选择独立根 | 不能从任意未知包装里扫描出一个solid冒充成功；装配/变换relationship路径尚未实现 |
| 空间 | 已明确context的三维点、方向、向量、轴放置 | 按精确profile区分非法维数和合法但非3D；不能只看dimension_count基类型，漏掉AP层维数规则 |
| 定义几何 | 直接LINE、PLANE；有限、非零直线边段 | SURFACE_CURVE中的LINE也不能提取后丢pcurve/master；TRIMMED_CURVE、曲线/曲面族扩展留P3 |
| 环与面 | EDGE_LOOP、一个显式FACE_OUTER_BOUND、零或多个普通FACE_BOUND孔；简单非自交直线多边形 | 无显式外环可属合法源但本切片Unsupported；两个显式外环违反已核规则；孔接触/相交/嵌套不自动修 |
| 开放模型 | MSSR→SBSM→OPEN_SHELL，面为FACE_SURFACE或适用ADVANCED_FACE | 保持开放，无材料体积资格，不自动封口 |
| 闭合实体 | ABSR→MANIFOLD_SOLID_BREP→CLOSED_SHELL，所有面ADVANCED_FACE、PLANE | 首片不含void、oriented shell、非流形或奇异边界；后续能力不能被静默忽略 |
| 闭合表面控制 | MSSR→SBSM→CLOSED_SHELL | 即使外形同盒体，也不提升为material solid；按表面模型报告 |
| 方向 | EC.same_sense、OE.orientation、bound.orientation、face.same_sense分别保留 | source ORIENTED_FACE/ORIENTED_SHELL层暂不展开；不能把其方向漏掉后接受 |
| 修复 | 关闭；等价参数/目标有限window单独记录规范化 | 不焊接、不移动顶点、不改曲线/面、不启用stitch/heal |

ABSR为ADVANCED_BREP_SHAPE_REPRESENTATION，MSSR为MANIFOLD_SURFACE_SHAPE_REPRESENTATION，SBSM为SHELL_BASED_SURFACE_MODEL。其必需item、面类型和shell角色见[来源表的表示/拓扑部分](p0-entity-source-audit.md)。这只是首个窄切片，不缩减主规格的最终首版目标。

上表是家族范围索引，**具体profile可更窄**。最新[AP203e2-N2635首profile](ap203e2-planar-profile.md)将所有壳面限为ADVANCED_FACE；普通FACE_SURFACE仅作为祖先，未准入。原因是固定N2635的MSSR部分普通面WHERE存在路径访问问题；不修写原schema、不借意图解释宣称等价。开放面、带孔面与盒体的独立数学目标均仍可覆盖。

### 1.1 完整根链

```text
PRODUCT_DEFINITION → formation → PRODUCT → product_context → application_context
PRODUCT_DEFINITION → definition_context ────────────────────→ application_context
PRODUCT_DEFINITION_SHAPE.definition → PRODUCT_DEFINITION
SHAPE_DEFINITION_REPRESENTATION.definition → PRODUCT_DEFINITION_SHAPE
SHAPE_DEFINITION_REPRESENTATION.used_representation → ABSR / MSSR
ABSR / MSSR → items: solid / surface model + 合法辅助placement
ABSR / MSSR → context: GEOM context + units + uncertainty
```

箭头表示源引用，PDS/SDR和protocol记录需要反向索引查到。PRODUCT.id/name、formation.id和PRODUCT_DEFINITION.id不是文件级实体ID，不得按名字合并revision或零件。AP203旧MECHANICAL_CONTEXT/DESIGN_CONTEXT的原类型须保留，不能因为现代合并字典省略它们就跳过。

具体表示items中的合法AXIS2_PLACEMENT_3D辅助item保留其角色，**不自动作为全零件变换再应用一次**。generic SHAPE_REPRESENTATION加relationship包装不是任意容器；本切片无完整路径解释时拒绝，不猜第一个representation。多definition/多SDR无法唯一解释时报告选择歧义。

<a id="p0-schema"></a>
## 2. 文件与schema登记：识别不等于资格

### 2.1 HEADER结构

| 记录 | 位置序列 | 首切片规则 |
|---|---|---|
| FILE_DESCRIPTION | description列表；implementation_level | 保留全部description与级别原文，不能把级别当AP版本 |
| FILE_NAME | name；time_stamp；author列表；organization列表；preprocessor_version；originating_system；authorization | 7项，不从producer/name猜单位或后端；列表和字符串原样保留 |
| FILE_SCHEMA | schema_identifiers列表 | 保留原字符串及OID；多schema组合须有明确映射，不能只取第一项 |

这些HEADER记录不是DATA中的几何实体，不使用DATA实体属性表分派。列表的基数/字符串限制按已选Part21资料核对；不在未核规则处自行添加UNIQUE。结构交叉来源为[STEP Tools的header定义](https://www.steptools.com/docs/stpython/Design.html)和固定OCCT的[FileName reader](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWHeaderSection/RWHeaderSection_RWFileName.cxx:31)。

首组普通文件用`2;1`；多DATA/命名DATA的设计用例须使用与相应Part21扩展相符的载体profile，不仅在`2;1`文件后随意追加DATA。`3;1`及命名/多DATA的精确语法、CC2全external mapping和ed3字符/扩展组合分别登记，不由某个库能容错读取推导支持。P1仍须完成主规格要求的全部DATA处理；ZIP、外部引用和签名等延后范围不变。[载体/映射说明](https://www.steptools.com/docs/roselib/read_write.html)

### 2.2 AP识别登记

| 标识族 | 已核识别材料 | 当前资格/动作 |
|---|---|---|
| CONFIG_CONTROL_DESIGN | AP203第一版名称，旧context及管理实体差异需保留 | 家族可识别；P0所用原版schema/全局规则未因此自动通过 |
| AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF | AP203第二版名称；不把2005临时schema/测试别名静默等同 | 按精确名称/OID单列registry，不抹平版次 |
| AUTOMOTIVE_DESIGN | AP214家族；官方说明列e1/e2/e3的OID；e3常用`{1 0 10303 214 3 1 1}` | 无OID时记录edition未声明，不默认e3；尾部多一个分量等历史变体单独待核 |
| AUTOMOTIVE_DESIGN_CC2 | 历史schema名线索，不等于Part21 conformance class 2 | 不能与AUTOMOTIVE_DESIGN简单改名；首片未取得对应语义则UnsupportedSchemaVariant |
| AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF | 相同名称可能带不同edition OID，既有HEADER画像见e1/e2线索；须按完整OID核对 | 精确OID保留；不能把OID分量直接当edition，也不能用现行merged定义替全部旧版背书 |

识别依据：[AP203说明](https://www.steptools.com/docs/stp_aim/notes_ap203.html)、[AP214说明](https://www.steptools.com/docs/stp_aim/notes_ap214.html)、[AP242说明](https://www.steptools.com/docs/stp_aim/notes_ap242.html)。既有HEADER画像只提供出现线索，不提供符合性；不重新读取真实模型。

当前STEP Tools字典是跨AP合并模式，包含较新的AP242及扩展；本轮属性核对不等于三类AP各edition的完整认证。后续已从[MBx-IF现行入口](https://www.mbx-if.org/home/mbx/resources/express-schemas/)取得AP203e2、AP214e3及AP242e1/e2/e3五份longform文件并固定hash；文件各有TS/DIS/修订等自述，不能统称未修改的ISO最终原件。AP203e1目前只有明确修改过的STEPcode副本。详情见[profile附件](p0-schema-profiles.md)。

实施前，每个可接受profile必须固定来源/hash、本片实体及SELECT/WHERE/必要RULE、合法complex签名和拒绝原因。**来源文件已取得不等于profile已核定**；尚未核定时不得交Accepted。

APPLICATION_PROTOCOL_DEFINITION的年份/应用字符串保留并与适用规则对账；年份不是独立版次真值，不可覆盖FILE_SCHEMA/OID或仅凭producer年份推断edition。

### 2.3 本轮补证造成的设计修正

| 项目 | 应采用的规则 | 对首片的影响 |
|---|---|---|
| AP242 OID | MBx-IF列e1为`442 1 1 4`、e2为`442 3 1 4`、e3为`442 4 1 4`（前缀均`1 0 10303`） | 既有画像中的`442 3 1 4`按该来源应归e2线索；不能把分量3当e3 |
| AP214 OID | 发布页、文件头与供应商说明存在冲突，逐项保留证据 | 未证明等价的尾部变体不能自动接纳 |
| conversion dimensions | AP214e3文件为派生`*`；AP203e2/AP242e1—e3为显式引用 | 按精确profile解释，同一段文本不能仅改HEADER跨AP复用 |
| optional description | AP203e1修改副本与较新版不同 | `$`/空串用例必须绑定具体属性及来源版本 |
| APD的schema-name值 | 各版RULE比较的字符串不总等于HEADER名，大小写/`_mim_lf`也有差异 | 不用通用lowercase或去后缀规则“修正”源记录 |
| 分类和管理关系 | 某些全局RULE要求分类、id owner、审批等反向图 | 当前38类主清单不是完整文件闭包；未证明无影响的内容不可当metadata跳过 |

逐项原文行号与hash见[AP203](p0-ap203-provenance.md)、[AP214](p0-ap214-provenance.md)、[AP242](p0-ap242-provenance.md)。本轮修正字段级事实，不扩大产品读取链路，也不授予全AP符合性。哪些管理规则属于所选profile的必要闭包、哪些不触发，必须有逐项依据和反例，不能靠几何库读成功决定。

<a id="p0-catalog"></a>
## 3. 实体与属性清单的使用合同

### 3.1 按角色落实的清单

| 组 | 对应实体 | 逐位置属性/来源 |
|---|---|---|
| 定义几何 | CARTESIAN_POINT、DIRECTION、VECTOR、AXIS2_PLACEMENT_3D、LINE、PLANE | [实体来源表 §3](p0-entity-source-audit.md) |
| 边界图 | VERTEX_POINT、EDGE_CURVE、ORIENTED_EDGE、EDGE_LOOP、FACE_BOUND、FACE_OUTER_BOUND | [实体来源表 §4](p0-entity-source-audit.md) |
| 面/壳/形状 | FACE_SURFACE、ADVANCED_FACE、OPEN_SHELL、CLOSED_SHELL、MANIFOLD_SOLID_BREP、SBSM | 同上，区分solid与surface-model路径 |
| 表示 | REPRESENTATION、SHAPE_REPRESENTATION及ABSR/MSSR具体类型 | [实体来源表 §5](p0-entity-source-audit.md)；基类知识不等于任意实例可直接转换 |
| 产品/shape关联 | APPLICATION_CONTEXT/APPLICATION_PROTOCOL_DEFINITION、产品/definition context、PRODUCT、formation两变体、PRODUCT_DEFINITION、PDS、SDR | [实体来源表 §6](p0-entity-source-audit.md) |
| 历史context与分类 | MECHANICAL_CONTEXT、DESIGN_CONTEXT、PRODUCT_CATEGORY、PRODUCT_RELATED_PRODUCT_CATEGORY | [实体来源表 §7](p0-entity-source-audit.md)；识别、保留并执行适用规则，不据字符串改几何 |
| 单位/context | REPRESENTATION_CONTEXT、GEOMETRIC_REPRESENTATION_CONTEXT、GUAC/GUUNC；NAMED/SI/LENGTH/PLANE_ANGLE/SOLID_ANGLE_UNIT；CONVERSION_BASED_UNIT；DIMENSIONAL_EXPONENTS；measure/typed measure及UNCERTAINTY | [单位来源表 §1](p0-unit-source-audit.md)，共15类记录的simple与complex局部字段 |

每个manifest条目需要：源profile、实体/合法constituent签名、位置及声明origin、显式/继承/derived/optional、类型/SELECT、LIST/SET/UNIQUE与基数、局部WHERE/适用RULE、必需依赖、转换或保留角色、拒绝code、关联真值/用例及NotRun/通过状态。不能只有一个“实体名支持=true”。

### 3.2 不得混淆的字段

- `ORIENTED_EDGE(name,*,*,edge_element,orientation)`是5槽；2/3为原显式端点转derived后的占位，不能写`$`或顶点引用。新增derived属性不随意增加槽。
- `EDGE_LOOP.edge_list`有顺序且OE身份UNIQUE；不同OE仍可引用同一底边。`FACE.bounds`/shell faces是SET，不据存储顺序选外环/面方向。
- `FACE_OUTER_BOUND`不加新属性，`ADVANCED_FACE`仍是4槽；继承字段被重声明为显式SELECT时不是`*`。
- `VECTOR.magnitude`本体允许0；用于非退化LINE时另要求有效非零，不能把项目检查冒充字段的positive类型。
- 首片要求3D不等于EXPRESS的dimension_count只允许1—3。合法但超范围的数据准确Unsupported，不伪报语法错误。
- formation等description仅在所选profile声明optional时可用`$`；AP203e1修改副本有required差异。必需label中的空字符串不等于`$`。BOOLEAN不能用LOGICAL的`.U.`代替false。
- SI complex的NAMED_UNIT dimensions为`*`；conversion-based须按profile区分：AP214e3文件也是`*`，AP203e2/AP242e1—e3文件为显式引用。不能照merged或固定writer统一布局。

完整字段类型、基数及局部来源由上方两份来源表逐行给出；它们包含OCCT reader比schema宽或过强的差异，不能照搬其容错行为。

### 3.3 canonical complex片段

以下是局部说明，不是完整STEP样件：

```text
#1=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));
#2=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));
#3=(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT());
```

局部属性只放在声明它的constituent，父context字符串不在每个子型重复出现。不能漏读未知constituent后仍称完整解释；首组canonical只用已登记的单位/context组合，其余合法组合在未具备类型契约时准确Unsupported。

<a id="p0-parameters"></a>
## 4. 单位与源参数规则

### 4.1 单位和量纲

令`sL`为源长度单位到mm的正因子。SI metre为`1000×10^prefixExponent`，无prefix与`.MILLI.`等有明确区别；conversion-based链逐级乘实际typed measure因子，单位name不是换算权威。

- 点坐标与VECTOR.magnitude使用长度换算；direction ratios不乘长度scale，先保留原比值再稳定归一化。
- LENGTH/PLANE_ANGLE/SOLID_ANGLE及ratio按语义类型区分；后几者即使七维指数均零，也不相互替换。
- conversion链检查量纲/typed value、正有限因子、循环/资源和目标数值范围。不能只取REAL而丢掉LENGTH_MEASURE包装。
- uncertainty用其自己的单位换算，保留声明与缺失状态；不是工作tol、接受门槛或修复许可。
- 按所选schema执行单位全局规则。已核AP214规则要求GUAC中恰好一个长度和一个平面角单位，不能因没有角度几何而省略；这不等于所有AP都强制有sr。[全局规则来源](https://www.steptools.com/stds/stp_aim/html/t_global_length_and_angle_units_2d_or_3d.html)
- 正例明确给长度/rad/sr及有效uncertainty，另用反例检查缺失/冲突。缺uncertainty constituent与存在零/负的非法值分开，不能把后者偷偷当缺失。

精确schema未核定时不宣称已验证该AP的所有单位规则。必要属性或规则影响不明，按Unsupported/Indeterminate阻止接受，而不是补造默认mm/rad。

### 4.2 轴放置

先按STEP规则建正交基，而不是把SMLib的自动正交化当源解释：

```text
Z = normalize(axis)，缺省用全局+Z
R = normalize(ref_direction)
    ref缺省：Z恰为±X时取全局+Y，否则取全局+X
X = normalize(R - dot(R,Z) Z)
Y = normalize(Z × X)
```

非正交ref需要投影，是定义解释；非法零方向或给出的平行ref不能按optional缺省补救。数学合法但极近退化导致无法达到数值配置时，报告数值承载/证据不足，不编造ISO规定的epsilon。所用函数与默认分支见[单位参数来源表](p0-unit-source-audit.md)。

### 4.3 LINE：保留源参数和target参数的对应

设源LINE原点P、方向比值r、单位方向d=r/‖r‖、VECTOR magnitude m>0：

```text
Csource(t) = P + t m d
Cmm(t) = sL P + t (sL m) d
t(Q) = dot(Q-P,d)/m
```

源`t`是无量纲参数；换长度单位同时换P、m后，t不再乘`sL`。端点残差先对源定义检查，不能直接连两个偏离LINE的顶点“修成”直线边。

same_sense为真须`t_end>t_start`，为假须相反；保存每个源顶点与参数关联，不只排序两个数。首片直线边必须非零。

target保持源LINE递增方向：a=min(t_start,t_end)、b=max(...)，A=Cmm(a)、B=Cmm(b)，取native线段`q∈[0,1]`，映射`t=a+(b-a)q`。已核SmLine工厂默认提供该域，但构建后仍读取实际自然域核对，不能对任意曲线假定[0,1]。

### 4.4 PLANE：形式参数与数值基准分开

源平面为`S(u,v)=O+uX+vY`；u/v形式上无量纲，X/Y长度基准来自plane context。canonical选择每参数单位1mm时：

```text
Omm=sL O，un=sL us，vn=sL vs
Sn(un,vn)=Omm+un X+vn Y
un(Q)=dot(Qmm-Omm,X)，vn(Q)=dot(Qmm-Omm,Y)
```

这里PLANE参数数值随基准变化，LINE的t不变；不把所有UV/knot/parameter_value统一缩放。规范依据和source→canonical推导见[单位参数来源表](p0-unit-source-audit.md)；目标scale=1和实际SmPlane定义见[native审计](p0-smlib-source-audit.md)。

target自然域取能够包含完整已裁剪直线边界/区域的有限window。全部段的UV坐标是仿射函数，其极值在端点；使用有界、向外舍入的包围矩形，不改变原始无限PLANE或新增一个trim环。只适用于本片已验证的简单多边形；不推广到曲边或周期面。

SmPlane求值可能夹取域外UV后仍成功，因此先独立检查window包含，再求值/验收。不得靠clamp隐藏少包一条边或孔，window零宽/非有限/不能可靠求界时拒绝。

<a id="p0-orientation"></a>
## 5. 首片受限的方向映射与UV规则

依据公开资源定义：face的topological normal与支撑面法向由same_sense关联，边界按该法向定义材料所在侧；bound为反向时需要反转其使用路径。见[ISO10303-42拓扑定义](https://www.steptools.com/stds/smrl/data/resource_docs/geometric_and_topological_representation/sys/5_schema.htm#topology_schema.face)。下式是本片前提下的推导，**不是已通过的全方向实现**。

前提：direct FACE_SURFACE/ADVANCED_FACE、PLANE、LINE；无ORIENTED_FACE/ORIENTED_SHELL/void/源pcurve；target平面保持源自然法向，target曲线保持源参数递增，native StartLoop固定SAME。

令布尔真/假分别为+1/-1，分别保留：s=EC.same_sense、o=OE.orientation、b=bound.orientation、f=face.same_sense。

| 层 | 相对源LINE递增方向/法向的关系 |
|---|---|
| 源底边拓扑方向 | s |
| 源loop中OE方向 | s·o |
| 源bound对face的有效路径 | s·o·b；b为负时逆序并逐use反向 |
| 源face法向 | f·Nsurface |
| native SAME主侧的EU方向 | s·o·b·f；还须按b/f实际反转循环路径 |
| 从选定FU=f读取 | 再反向f，恢复源bound有效路径s·o·b |

native primary FU始终是SAME；StartFace(f)选择盒体外壳使用的侧。开放片两FU同shell，必须在来源映射中单独保留源面对应的selected FU，不能只看shell判断方向。

在这个固定native loop策略下，StartEdge顶点按curve min/max给，方向按明确primary路径给，外环先建、孔后建。反转必须**逆序＋逐use反向**，闭合循环起点可旋转，不能要求同一head索引。source face/bound方向冲突不能被“按面积翻正”偷偷修复。

所选face侧的外环应按该面法向保留材料在左，孔相反；转换到primary surface侧后UV外环面积正、孔负。面积仅为核对，不是选择源语义的替代。完整16组合、孔加f=-1、盒体混合sense仍是NotRun，须由P2逐层证明；target参数反向或额外oriented层不适用本公式。

派生UV按target 3D边递增q参数化：`uv(q)=uvA+q(uvB-uvA)`，不因某次use反向而反转曲线本体。建图后每occurrence用fresh对象经setter装到SAME成员，并核对实际owner；不使用constructor直接UV安装分支，不能传同指针期待迁移。

在精确实数下，平面lift与3D直线均为仿射函数；确认构造确实仿射后，全区间残差由端点残差凸组合给界。浮点运算/存储及表征物化还要加各自可靠误差界，不能仅抽样两端就对任意native曲线作保证。

<a id="p0-verification"></a>
## 6. 独立验收、质量配置与未证门槛

1. 先完成文件/schema/引用/属性/类型闭包，再解释单位与源几何；准确区分schema非法、合法但超片范围和数值/证据不足。
2. 在源/中立层证明线段、简单多边形、孔包含、共享身份、区域及方向。四层sense分别保留，不丢失derived端点关联。
3. 由固定参数映射材料化native单次候选。source同一edge共享topology，native曲线/面/UV按owner一次移交，不共享已owned几何指针。
4. 独立核对window、实际参数/域、点/方向/线/面、selected FU、loop/edgeuse、SAME UV owner、shape-role、每个实体tol及有效阈值。
5. ValidatePointers收全部reports，满足安全前提后LEVEL2/WALK；返回SM_SUCCESS不覆盖reports。检查前后再对账副作用。
6. 与[真值/用例表](p0-planar-cases.md)的解析几何和组合拓扑比较，满足测试配置才交完整owning快照；诊断/取消/任何必需根失败均无本次模型。

测试专用Q-P0-mm-01及其数值只在用例表定义，不是生产默认。误差界足以证明通过/失败才裁决；跨预算边界为Indeterminate。修复关闭；源uncertainty只记录/告警，不改变Q。

AP203e2-N2635的首个窄profile现已具有完整静态闭包及13正/36反的具体输入设计；平面51条、带孔面81条、盒体145条DATA为图/文档计数，原始文件字节hash待实际生成后冻结。其他AP/profile不由它自动获得资格。仍未完成：ISO最终出版附件身份、HEADER/载体规则与映射实现、真正fixture文件和独立oracle、全方向/孔/window/owner/tol/失败资源的P1/P2运行。已有native探针不算本片STEP端到端通过，完整AP符合性认证数量仍为0。

本轮已得到字段级来源、明确数学真值和可执行前应满足的合同，**没有将文档完成标成P0阶段或测试通过**。后续必须先消除上述与待编码能力有关的源/验收缺口，再按用户后续授权实施。
