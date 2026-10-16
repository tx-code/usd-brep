# P0 单位、坐标与直线/平面参数：来源核对

状态：2026-10-15，设计研究材料；未编写读取器、测试代码或 STEP fixture，未读取真实模型。本文补充[已确认规格 0.1](../step-import-repair-spec.md)，不授予任何运行时能力或生产精度默认值。

## 来源和适用边界

- 初次属性/继承核对采用 STEP Tools 公开的 `step_merged_ap_schema` EXPRESS HTML。它是跨 AP 合并资料，实体页生成时间也可能不同；**不能据此宣称 AP203/AP214/AP242 的任意版次属性、WHERE/global rule 都完全相同**。后续已取得 MBx-IF 当前发布的五份 longform 文件；具体来源等级、文件自述与 hash 见 [AP203](p0-ap203-provenance.md)、[AP214](p0-ap214-provenance.md)、[AP242](p0-ap242-provenance.md)。下表的 merged 结构须受第 1.2 节版次差异覆盖。
- 几何公式核对采用 STEP Tools 公开的 ISO 10303-42:2021 几何/拓扑资源正文。它提供定义解释；实际文件仍须匹配自己的 AP/edition 所用资源版本。
- Part21 复合实体组成及序列另以本机 OCCT `3d097a0328e71b826377d4814ab05ec3c3d23871` 的 RWStepBasic/RWStepGeom 写出源码交叉核对。这里只观察序列，OCCT 的容错、缺省、阈值和 heal 策略不是 ISO 规则。
- 本文表格按“简单实体的继承展开后显式属性”及“复合 external mapping 的局部属性”分别列出；DERIVE/INVERSE 不额外添加到简单实体参数尾部。完整 Part21 lexical/encoding 规则和 AP/edition 适用矩阵仍须单独锁定。

## 1. 单位和 context 属性清单

`required` 表示 EXPRESS 非 OPTIONAL；`$` 仅可用于对应 OPTIONAL，`*` 表示派生/重声明占位，二者不能互换。

| 实体/局部 constituent | 继承展开后的简单实体属性顺序 | 复合 external mapping 本 constituent 局部属性 | 必查项与来源 |
|---|---|---|---|
| REPRESENTATION_CONTEXT | context_identifier, context_type | 同左 | 两个 required 字符串；representations_in_context 是 INVERSE，不序列化。[定义](https://steptools.com/stds/stp_aim/html/t_representation_context.html) |
| GEOMETRIC_REPRESENTATION_CONTEXT | context_identifier, context_type, coordinate_space_dimension | coordinate_space_dimension | 首切片 3D context 要求维数 3；P0 不把 2D context 的点当世界 3D 点。[定义](https://steptools.com/stds/stp_aim/html/t_geometric_representation_context.html) |
| GLOBAL_UNIT_ASSIGNED_CONTEXT | context_identifier, context_type, units | units | SET [1:?] OF unit；不按 SET 成员位置识别长度/角单位。[定义](https://steptools.com/stds/stp_aim/html/t_global_unit_assigned_context.html) |
| GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT | context_identifier, context_type, uncertainty | uncertainty | SET [1:?] OF uncertainty_measure_with_unit；该 constituent 可按 schema 组合缺席，但出现时不允许空 SET。[定义](https://steptools.com/stds/stp_aim/html/t_global_uncertainty_assigned_context.html) |
| NAMED_UNIT | dimensions | dimensions | required dimensional_exponents 引用；SI_UNIT 子型将此属性重声明为派生。[定义](https://steptools.com/stds/stp_aim/html/t_named_unit.html) |
| SI_UNIT | dimensions*, prefix, name | prefix, name | prefix OPTIONAL si_prefix；name required si_unit_name；dimensions 从 name 派生，继承位置为 `*`。[定义](https://steptools.com/stds/stp_aim/html/t_si_unit.html) |
| LENGTH_UNIT | dimensions | 无局部显式属性 | 七维指数应为 (1,0,0,0,0,0,0)。[定义](https://steptools.com/stds/stp_aim/html/t_length_unit.html) |
| PLANE_ANGLE_UNIT | dimensions | 无局部显式属性 | 七维指数全零，但不能据全零把它等同 SOLID_ANGLE_UNIT 或 ratio。[定义](https://steptools.com/stds/stp_aim/html/t_plane_angle_unit.html) |
| SOLID_ANGLE_UNIT | dimensions | 无局部显式属性 | 七维指数全零；保留 subtype/SI name 的语义区别。[定义](https://steptools.com/stds/stp_aim/html/t_solid_angle_unit.html) |
| CONVERSION_BASED_UNIT | dimensions 或 dimensions*, name, conversion_factor，按 profile | name, conversion_factor | AP203e2/AP242e1—e3 文件继承显式 dimensions，并用 WHERE 检查一致；AP214e3 文件重声明为 DERIVE，原槽必须 `*`。不能套用同一布局。name 不授权猜换算率。[merged 定义](https://steptools.com/stds/stp_aim/html/t_conversion_based_unit.html)、[版次差异](p0-ap214-provenance.md) |
| DIMENSIONAL_EXPONENTS | length_exponent, mass_exponent, time_exponent, electric_current_exponent, thermodynamic_temperature_exponent, amount_of_substance_exponent, luminous_intensity_exponent | 同左 | 七个 required REAL，顺序固定。[定义](https://steptools.com/stds/stp_aim/html/t_dimensional_exponents.html) |
| MEASURE_WITH_UNIT | value_component, unit_component | 同左 | value 为 measure_value SELECT，unit 为 unit SELECT；保留 LENGTH_MEASURE 等 typed value 名称并校验 valid_units，不能把所有 wrapper 扔掉只取 REAL。[定义](https://steptools.com/stds/stp_aim/html/t_measure_with_unit.html)、[valid_units](https://steptools.com/stds/stp_aim/html/t_valid_units.html) |
| LENGTH_MEASURE_WITH_UNIT | value_component, unit_component | 无局部显式属性 | 继承 MEASURE_WITH_UNIT；其 unit_component 必须属于 LENGTH_UNIT。[定义](https://steptools.com/stds/stp_aim/html/t_length_measure_with_unit.html) |
| PLANE_ANGLE_MEASURE_WITH_UNIT | value_component, unit_component | 无局部显式属性 | 继承 MEASURE_WITH_UNIT；其 unit_component 必须属于 PLANE_ANGLE_UNIT。[定义](https://steptools.com/stds/stp_aim/html/t_plane_angle_measure_with_unit.html) |
| UNCERTAINTY_MEASURE_WITH_UNIT | value_component, unit_component, name, description | name, description | 前两项继承 measure；name required。五份 MBx-IF 文件中 description OPTIONAL；AP203e1 修改副本则 required，不据 merged 向 e1 放宽。合并 schema 的 valid_measure_value 要求数值大于零；不能自动把零/负 uncertainty 当“未提供”。[定义](https://steptools.com/stds/stp_aim/html/t_uncertainty_measure_with_unit.html)、[valid_measure_value](https://steptools.com/stds/stp_aim/html/t_valid_measure_value.html)、[e1 限制](p0-ap203-provenance.md) |

上表是有来源的属性记录，不是完整 EXPRESS evaluator；子型合法组合、AP 特有全局规则、SELECT 所有分支仍按确切 schema 条目启用。对未支持的 unit SELECT 分支，返回 Unsupported，不按 named_unit 强转。

### 1.1 自拟的短序列示意

以下仅为文档片段，不是可加载 fixture，不含完整 AP 产品与形状图。编号是局部示意。uncertainty description 的 `$` 仅用于已核 optional 的 profile；不能照搬到 AP203e1 修改副本。

```text
#1=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));
#2=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));
#3=(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT());
#4=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0001),#1,'distance_accuracy',$);
#5=(GEOMETRIC_REPRESENTATION_CONTEXT(3)
    GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#4))
    GLOBAL_UNIT_ASSIGNED_CONTEXT((#1,#2,#3))
    REPRESENTATION_CONTEXT('local','3D'));
```

SI 复合中 `NAMED_UNIT(*)` 的 `*` 不能写成 `$` 或指向某个显式 dimensions。省略 prefix 用 `$`，并不省略 SI name。context 的两个继承字符串只出现在 REPRESENTATION_CONTEXT constituent，不在每个 subtype 中重复写。SI metre/radian/steradian 的维数来源见 [dimensions_for_si_unit](https://steptools.com/stds/stp_aim/html/t_dimensions_for_si_unit.html)。复合序列交叉证据为 OCCT [RWSiUnitAndLengthUnit](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWSiUnitAndLengthUnit.cxx:122) 与 [context writer](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepGeom/RWStepGeom_RWGeomRepContextAndGlobUnitAssCtxAndGlobUncertaintyAssCtx.cxx:151)。

AP203e2/AP242e1—e3 文件的 conversion-based 长度示意：

```text
#10=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);
#11=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#1);
#12=(CONVERSION_BASED_UNIT('inch',#11) LENGTH_UNIT() NAMED_UNIT(#10));
```

这些 profile 的 NAMED_UNIT 是 `#10`，不是 `*`。但已取得的 AP214e3 文件采用派生 dimensions，同一行必须改为 `NAMED_UNIT(*)`；不能因 OCCT writer 一律写引用就覆盖原 schema。conversion_factor 表达每一个自定义长度单位等于 25.4 个 `#1` 单位；name 改名不改数学换算率。因子 12、基单位 `#12` 可表达另一级长度单位，仍须检测循环和资源限额。[固定 writer 仅作交叉证据](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWConversionBasedUnitAndLengthUnit.cxx:99)。

### 1.2 已取得文件的版次差异

| 字段/规则 | AP203e2 MBx 文件 | AP214e3 MBx 文件 | AP242e1/e2/e3 MBx 文件 | AP203e1 STEPcode 修改副本 |
|---|---|---|---|---|
| conversion dimensions | 显式 `NAMED_UNIT(#dim)` | 派生 `NAMED_UNIT(*)` | 显式 `NAMED_UNIT(#dim)` | 显式；只作研究证据，尚未启用 |
| SI dimensions | 派生 `*` | 派生 `*` | 派生 `*` | 派生 `*`；同上 |
| uncertainty description | OPTIONAL | OPTIONAL | OPTIONAL | required，不许 `$` |
| 3D GUAC 单位规则 | 未见e1的同名三单位规则；消费需求和闭包另查 | 长度、平面角各恰一个 | 三份文件未见AP214的长度/角计数RULE；消费需求和闭包另查 | 恰三项且长度/平面角/立体角各一 |

该表记录具体文件内容，**不是已批准运行 profile**。来源等级和完整规则闭包未冻结前，不能仅凭本表开始接纳文件。AP203e1 修改副本不能证明 ISO 原始发布文件所有限制；也不能把 e1 的三单位要求推广到其他 AP。详见三份版次报告与[profile 冻结门槛](p0-schema-profiles.md)。

## 2. 单位解释规则

以下为本项目首切片的解释/拒绝规则；其中循环、正有限因子和质量约束属于项目明确要求，不冒充某条尚未核实的 ISO WHERE。

1. 从所选 representation 的 context 解析单位，不从文件名、产品 name、`context_type='3D'`、某个零件的单位或几何 bbox 猜测。相同 STEP 点记录若通过不同 context 被使用，解释身份必须包含该 context；冲突不能靠全文件一个 scale 掩盖。
2. 长度基准 mm，物理平面角基准 rad；立体角若出现保留 sr 及来源。SI METRE 的 scale-to-mm 为 `1000 × 10^prefixExponent`；RADIAN 到 rad 为 `10^prefixExponent`。不把 angle 前缀解释为长度前缀。[SI prefix 枚举](https://steptools.com/stds/stp_aim/html/t_si_prefix.html)、[SI name 枚举](https://steptools.com/stds/stp_aim/html/t_si_unit_name.html)
3. 已核枚举从 EXA 到 ATTO：18、15、12、9、6、3、2、1、-1、-2、-3、-6、-9、-12、-15、-18；无 prefix 为 0。当前资料没有 quetta/ronna 等新前缀，不因现实 SI 增加名称就向旧 STEP schema 静默添加枚举。
4. conversion-based 单位递归解 `scale(U)=numeric(conversion_factor) × scale(baseUnit)`，保留完整链、typed measure 和声明维数。因子必须正且有限，链无环；超限资源/目标数域不可安全表达要与源量纲错误分开。name 从来不是换算权威。
5. 角单位和 ratio 的七维指数同为零；不能只凭维数把 degree/radian、steradian、ratio 混用。首切片转换链支持须终止于匹配用途的已知 SI unit，遇语义不明或尚未证明合法的复合组合拒绝/Unsupported；不把所有无量纲项强行当 rad。
6. 对所选精确 3D 几何，长度单位始终必需。角度属性/角参数若被消费，平面角单位必需。**此外必须执行所选 schema 的额外单位规则**：已取得 AP214e3 文件的 `global_length_and_angle_units_2d_or_3d` 要求 GUAC 中恰有一个 LENGTH_UNIT 和一个 PLANE_ANGLE_UNIT，即使只有直线/平面也不能略过；该条不强制 sr。AP203e1 修改副本却要求三类单位各一，须分别登记。AP203e2/AP242 所取得文件的规则与触发条件见各专题报告，不从名字猜相同实现。[merged 交叉来源](https://steptools.com/stds/stp_aim/html/t_global_length_and_angle_units_2d_or_3d.html)
7. 首切片正式正例建议明确列 mm/rad/sr（以及换算长度变体），避免把单位缺失容错混入正常路径。缺失角度单位反例按 schema 分桶：已核规则要求则 Rejected/InvalidSource；确切 schema 尚未核准时不能宣称 Accepted，记 UnsupportedSchemaRule/待核；若将来某个已核 schema 允许且未消费角度，可诊断“无此上下文单位”，但不补造 rad。
8. 同一 context 出现两个有效长度单位，即使二者换算率相同，也不能擅自去重并抹掉 schema 数量问题。适用规则要求唯一时准确拒绝。冲突、缺失、非法 typed value、dimensions 不符分别定位至源记录/属性。
9. uncertainty 用它自己的 unit_component 换算；与几何 context 的长度单位可不同，但类型和换算必须明确。保留全部 uncertainty 条目及名称，不凭第一个 SET 成员选工作 tol；不能将 uncertainty 自动用作验收阈值、位移预算或内核 tol。缺整个 uncertainty constituent 与出现空/非法条目分别诊断。质量配置仍由调用者显式选定。

## 3. 坐标和轴构造

| 实体 | 简单 Part21 的属性顺序 | 首切片检查 |
|---|---|---|
| CARTESIAN_POINT | name, coordinates | 世界几何使用 3 个坐标；长度 scale 仅作用坐标数值；2D 参数空间点须有独立上下文身份。[定义](https://steptools.com/stds/stp_aim/html/t_cartesian_point.html) |
| DIRECTION | name, direction_ratios | EXPRESS 为 LIST [2:3] OF REAL，至少一项非零；世界几何首切片为 3 项。保留原 ratios，计算时稳定归一化；正倍数同向、负倍数反向，不乘长度 scale。[定义](https://steptools.com/stds/stp_aim/html/t_direction.html) |
| VECTOR | name, orientation, magnitude | magnitude 为 length_measure 且 >=0；零 VECTOR 本身不由此 WHERE 自动非法，但零 VECTOR 不可充当非退化 LINE 方向。[定义](https://steptools.com/stds/stp_aim/html/t_vector.html) |
| AXIS2_PLACEMENT_3D | name, location, axis, ref_direction | axis/ref_direction OPTIONAL；location/已给方向均须 3D；已给轴不能零、平行或反平行。[定义](https://steptools.com/stds/stp_aim/html/t_axis2_placement_3d.html) |
| LINE | name, pnt, dir | dir.dim=pnt.dim；本切片 3D、dir magnitude >0、端点裁剪长度非零。[定义](https://steptools.com/stds/stp_aim/html/t_line.html) |
| PLANE | name, position | 继承 elementary_surface.position，参照合法 3D placement；unbounded plane 和有限 face 的区域分别记录。[定义](https://steptools.com/stds/stp_aim/html/t_plane.html) |

建轴按 [build_axes](https://steptools.com/stds/stp_aim/html/t_build_axes.html) 和 [first_proj_axis](https://steptools.com/stds/stp_aim/html/t_first_proj_axis.html) 推导：

```text
Z = normalize(axis)；axis 缺省则 (0,0,1)
R = normalize(ref_direction)；ref 缺省则：
    Z 恰为 ±(1,0,0) 时取 (0,1,0)，否则取 (1,0,0)
X = normalize(R - dot(R,Z) Z)
Y = normalize(Z × X)
```

ref_direction 不必正交；投影是源定义的标准解释，不是移动几何的修复。例如 Z=(0,0,2)、ref=(2,0,3) 得 X=(1,0,0)、Y=(0,1,0)。ref 与 Z 平行/反平行导致 X 不存在，应拒绝，不能默默改用默认 X；axis 缺省而 ref 与默认 Z 平行也不能偷换默认。

数学上非平行但极近，若目标数域不能可靠构造并证明误差，归为数值承载/无法判定；不要用某个未经定标的全局 epsilon 冒充 ISO 规定，把输入随意修正。已给非法零方向不能按 optional 缺省补救。

OCCT 当前 [MakeAxis2Placement](E:/Coding/OCCT/src/DataExchange/TKDESTEP/StepToGeom/StepToGeom.cxx:169) 对失败/近平行输入存在缺省方向 fallback；此处明确不继承其容错政策。源未提供 ref 的标准默认规则和 OCCT/gp 自选方向也不可自动等同。

## 4. LINE 与边端点的参数规则

令源长度单位到 mm 的正因子为 `sL`，原点数值 `P`，原方向 ratios 为 `r`，`d=r/|r|`，VECTOR magnitude 数值为 `m>0`。ISO10303-42:2021 §4.5.24 的直线为 `C(t)=P+tV`，其中 V 的长度和 magnitude 影响参数化。[几何定义](https://steptools.com/stds/smrl/data/resource_docs/geometric_and_topological_representation/sys/4_schema.htm#geometry_schema.line)

据此，本项目中立层精确映射为：

```text
Csource(t) = P + t m d
Cmm(t) = sL P + t (sL m) d
t(Q) = dot(Q-P,d)/m
perpendicular residual = Q - (P + t(Q) m d)
```

`t` 为无量纲参数，不能把 m 丢掉，再假设参数就是源/目标弧长；长度单位换算同时作用 P、m 后，原 t 不另乘 sL。

EDGE_CURVE 简单属性依次 name, edge_start, edge_end, edge_geometry, same_sense。对直线，先求 `t_start/t_end` 并验证两个残差，要求：same_sense TRUE 时 `t_end>t_start`，FALSE 时 `t_end<t_start`。保存有向端点关联和有序区间，不能将两个数 sort 后遗失归属；同点零区间不是合法非退化直线边。此结论由 increasing curve parameter 与 edge start→end 的定义推导。[EDGE_CURVE 属性](https://steptools.com/stds/stp_aim/html/t_edge_curve.html)、[ISO10303-42:2021 §5.5.5](https://steptools.com/stds/smrl/data/resource_docs/geometric_and_topological_representation/sys/5_schema.htm#topology_schema.edge_curve)

ORIENTED_EDGE.orientation TRUE 保持 edge 的端点顺序，FALSE 交换本次使用的两端及参数；不改 EDGE_CURVE 身份、LINE 定义或相邻面中的其他 occurrence。此处只处理直线边与使用的关系，不用一个 XOR 代替 face_bound、face.same_sense 和壳方向的全部证明。

OCCT 的 [MakeLine](E:/Coding/OCCT/src/DataExchange/TKDESTEP/StepToGeom/StepToGeom.cxx:1649) 会构造单位方向 Geom_Line；[trim 转换](E:/Coding/OCCT/src/DataExchange/TKDESTEP/StepToGeom/StepToGeom.cxx:2376) 另乘 magnitude×length factor。这证明跨内核参数不能仅同名照搬；不作为 SMLib 使用相同参数的证据。

若目标用有限一次 B 样条段 `T(q)=(1-q)A+qB`、q∈[0,1] 表达，必须明记 `t=t_low+q(t_high-t_low)` 并保留 topological sense；这只是目标表示设计选项，当前没有据此授予任何 SMLib 参数映射资格。

## 5. PLANE UV 的单位规则

ISO10303-42:2021 §4.5.57 将 surface 参数 u/v 记为无量纲；§4.5.59 指定平面 `S(u,v)=O+uX+vY`，X/Y 的长度单位取自 plane context。[几何定义](https://steptools.com/stds/smrl/data/resource_docs/geometric_and_topological_representation/sys/4_schema.htm#geometry_schema.plane)

应区分形式参数和数值单位：平面 UV 可视为以源长度单位计数的参数数值，但其 EXPRESS 类型不是 length_measure，不能推广成“所有 UV 都是物理长度”。若中立 canonical plane 的 X/Y 基向量每单位为 1mm：

```text
Omm = sL O
umm = sL u_source，vmm = sL v_source
Smm(umm,vmm) = Omm + umm X + vmm Y
u_source = dot(Qmm-Omm,X)/sL
v_source = dot(Qmm-Omm,Y)/sL
```

因此：LINE 的 t 不随长度单位转换，而此 canonical 定义下的 PLANE UV 数值随 sL 转换。这正是不能全局对所有 parameter_value 使用同一规则的首个反例。

平面 3D 直线投到 UV 是可直接推导的仿射函数；可以验证系数等价与 lift 的全段残差界，而不以几个采样点取代全段证据。沿整个平面域的正确性仍需加 region/环/孔验证。将来处理 authored pcurve 时，其参数空间上下文及母 surface 参数语义分别解释，不能用“pcurve 是 2D CARTESIAN_POINT”就套世界长度 scale。

SMLib 原生平面/一次曲面可能有不同 basis、自然域和 native UV；P0 只冻结 source→canonical 规则，P2 编码前须将 chosen target construct 的 `target_parameter_map` 连同逆映射、导数、域、端点与法向方向写明并验证。不能从“也是平面”推出 UV 相等。

## 6. 可纳入首切片验收矩阵的单位/参数用例

以下是验收设计，全部为 NotRun；不暗示已有 `.step`、expected 文件或运行结果。数值仅为合成数学真值，不是生产默认精度。

| 设计用例 | 输入事实/变化 | 独立期望 |
|---|---|---|
| U-mm-cm-m | 同一 10×20×30mm 盒体依次以 mm、cm、m 重述，点与 VECTOR magnitude 同时按源单位改写 | 世界几何、共享身份和区域不变；体积 6000mm³，面积 2200mm²；记录每个 context 的换算 |
| U-inch-chain | 自拟 inch=25.4mm，另定义 foot=12inch；更改 conversion name | 换算 25.4/304.8mm，不受 label 改名影响；实际源 decimal value 作为真值，不把近似 degree 规定为精确 π/180 |
| U-SI-derived-slot | SI以`*`为正，改为`$`/显式#dim为负；conversion按profile：AP203e2/AP242以#dim为正、`*`为负，AP214e3恰好相反 | 正确片段通过对应属性解释，错误占位准确拒绝；不得忽略NAMED_UNIT constituent或借其他profile容错 |
| U-missing-conflict | 缺长度、两长度、typed LENGTH_MEASURE 配角单位、负/零因子、单位链环 | 全选请求无模型；分别给出缺失/冲突/量纲/因子/循环定位，不能默认 mm |
| U-angle-rule | AP214e3 GUAC只有长度，或只有长度+sr，无曲面角参数；与其他已取得profile对照 | AP214e3依据已核规则拒绝，不偷偷补rad；其他profile按其实际规则及消费需求判断，不移植AP214计数要求 |
| U-uncertainty | 长度几何 mm，uncertainty 以 metre 给出；另缺 uncertainty constituent | 换算源 uncertainty、保持显式质量配置；缺失诊断不等于授权更大 tol |
| A-default | axis/ref 都缺省；Z=+X 且 ref 缺省；Z=-X 且 ref 缺省 | 分别得标准坐标架；第二例 Xlocal=+Y,Ylocal=+Z,Zlocal=+X；第三例 Xlocal=+Y,Ylocal=-Z,Zlocal=-X |
| A-oblique-ref | Z=(0,0,2),ref=(2,0,3)，以及 direction ratios 乘正数 | 正交化后 X/Y/Z 与标准坐标架相同；是定义解释，不记几何修复 |
| A-invalid | 零 ratios、平行 ref、缺 axis 但 ref 平行默认 Z、错维数 | 拒绝/定位；不继承 OCCT fallback；极近但数学合法另测目标数值承载 |
| L-magnitude | mm 下 P=(1,2,3), r=(2,0,0), m=4；Q0=(5,2,3),Q1=(13,2,3) | t_start=1,t_end=3，长度 8mm；换 cm 后 t 仍 1/3；误把 magnitude 丢掉应被抓住 |
| L-sense | 同上线，互换两个顶点并 same_sense=FALSE；再反转 ORIENTED_EDGE | 底层 t 关联为 3→1；反转 use 为 1→3；若端点不换却 same_sense=FALSE 则严格失败 |
| P-scale | cm 平面 O=(1,2,3),axis=+Z,ref=+X；源 UV=(2,3) | 世界点=(30,50,30)mm；canonical UV=(20,30)；source UV 逆映射回 2/3 |
| P-line-lift | 有理/有限精度系数可控的平面直边，分别换长度单位/正缩放 ratios/反转 use | UV 仿射系数、三维 lift、全段覆盖和法向/区域保持；必须记录目标实际参数图 |

独立真值实现须与 loader 的单位解析、轴构造和参数映射代码分开；不能把同一 helper 在 importer/expected 两侧重用后称独立。P0 可采用人工审阅的精确有理表、轴代数及公式；未来 checker 应保留舍入界及测不出的状态。

## 7. 进入编码前仍须关闭的限定项

1. 已取得文件的来源/hash 和部分差异已有专题记录，仍须冻结实际启用 profile 的证据版本、单位/SELECT/global rule 闭包及合法 complex 签名；发布网页标签与文件内 DIS/TS/修订声明不能抹平。这阻塞相应能力晋升，不阻止继续撰写 P0 设计。
2. Part21 external mapping 顺序/合法复杂组合，须由确切 schema 与 Part21 版次验证；本文短片段仅给已核常见组合，不承诺所有排列都合法。
3. 目标数值承载门槛、近零/近共线可证明误差策略与资源配置；不在 P0 制造生产常数。
4. P2 chosen SMLib target plane/line 的实际参数图及 topology sense 接口映射，特别是 min/max 参数与有向 use 端点的不同顺序；当前已有构建探针不等于 STEP 映射测试。
5. 首切片若只接受直接 LINE 而拒绝 SURFACE_CURVE/PCURVE/TRIMMED_CURVE，须把拒绝写成增量能力限制，不能从这些 wrapper 中擅自抽出 LINE 后丢掉源 master 或 trims。

