# AP203e2 首个 profile：F1／F2／F3 平面样件记录设计

版本：203e2-P0-PlaneSamples-0.1，2026-10-15。状态：**文档设计／Defined／NotRun**。这里列的是拟作为最小合法阳性输入的记录文本、独立真值及阴性变化；没有创建 `.step`、生成器、loader、测试程序或 CAD 对象，也没有执行 EXPRESS 或 native 校验。完整载体及 `#1`—`#12` 共用产品／单位封套由[样件总册](ap203e2-sample-design.md)提供；本册只给出拼接在同一 DATA 中的 `#20` 以后记录。不能把这一段单独交给读取器并称为完整文件。

约束依据固定为 [AP203e2 首 profile](ap203e2-planar-profile.md)、[产品／context 闭包](ap203e2-planar-context-closure.md)、[几何／拓扑闭包](ap203e2-planar-geometry-closure.md)及[全局规则矩阵](ap203e2-planar-global-rules.md)。原文为固定 hash 的 N2635 制品，不是任意 AP203 版本。数值及几何真值全部沿用 [P0 独立真值 F1／F2／F3](p0-planar-cases.md#p0-face-truth)。本册不增加生产默认容差。

本册S203-F1／F2／F3是几何别名，分别对应统一样件目录S203-P01／P02／P03，不另外计为三个样件。

## 1. 输入包络、ID 与“最小”的含义

共用封套必须具备 `#9=SHAPE_DEFINITION_REPRESENTATION(#8,#20)`；`#10` 为 SI-mm、`#11` 为 SI-rad、`#12` 为唯一 3D GEOMETRIC_REPRESENTATION_CONTEXT + GLOBAL_UNIT_ASSIGNED_CONTEXT + REPRESENTATION_CONTEXT complex，单位集合只含 `#10/#11`。本册的 `#20` 使用这个 context。本组不增加 category、GUUNC、uncertainty、solid-angle 单位、representation.items 辅助 axis 或额外产品／表示。

“最小”指**没有这些可选分支的完整闭包**，不声称在所有合法 EXPRESS 表达中记录数全局最少。例如平面原点 `#130` 与顶点点 `#100` 在 F1/F2 中同坐标而保持不同源身份，方便单独改变平面参数原点；四条边各有自己的 DIRECTION，未按值合并。所有这些记录都实际被主图使用，没有孤立附加节点。

| ID | 角色 | F1 | F2／F3 |
|---|---|---|---|
| `#1`—`#12` | 总册固定的单产品／单位封套 | 原样使用 | 原样使用 |
| `#20/#21/#22` | MSSR／SBSM／OPEN_SHELL | 一条完整表面模型路径 | 不变 |
| `#100`—`#103`；`#110`—`#113` | 外环 CP；VP | A/B/C/D 的四个源顶点 | 不变 |
| `#130`—`#134` | 平面 O、N、X、A2P3D、PLANE | 基准平面 | F2 不变，F3 只改 O |
| `#200`—`#204`、`#210`—`#214`、`#220`—`#224`、`#230`—`#234` | 每块依次 DIRECTION、VECTOR、LINE、EC、OE | AB、BC、CD、DA | 不变 |
| `#300/#301/#302` | 外环 EDGE_LOOP／FOB／ADVANCED_FACE | 单环 | F2/F3 的 AF 增加孔 bound |
| `#104`—`#107`；`#114`—`#117` | 孔 CP；VP | 不出现 | E/F/G/H |
| `#240`—`#244`、`#250`—`#254`、`#260`—`#264`、`#270`—`#274` | 孔边 DIRECTION／VECTOR／LINE／EC／OE | 不出现 | EF、FG、GH、HE |
| `#310/#311` | 孔 EDGE_LOOP／FACE_BOUND | 不出现 | 加入 `#302.bounds` |

所有 simple 记录只出现一个继承的 name；OE 的两个继承后被派生重声明的端点槽必须为 `*`。空字符串不是缺值。表示名 `'body'` 为普通名称，不触发特殊表示角色。`#133` 是 PLANE 必需的 placement，**没有**作为 representation.items 中的附加 axis item。

## 2. S203-F1：旋转矩形开放面完整几何记录

此段与共用封套合并后作为 S203-F1 的唯一正例基准。记录书写顺序只为阅读；引用以 ID 为准，`#22` 对后方 `#302` 的前向引用是设计的一部分。

```text
#20=MANIFOLD_SURFACE_SHAPE_REPRESENTATION('body',(#21),#12);
#21=SHELL_BASED_SURFACE_MODEL('',(#22));
#22=OPEN_SHELL('',(#302));

#100=CARTESIAN_POINT('',(3.,-2.,5.));
#101=CARTESIAN_POINT('',(10.2,7.6,5.));
#102=CARTESIAN_POINT('',(3.8,12.4,5.));
#103=CARTESIAN_POINT('',(-3.4,2.8,5.));
#110=VERTEX_POINT('',#100);
#111=VERTEX_POINT('',#101);
#112=VERTEX_POINT('',#102);
#113=VERTEX_POINT('',#103);

#130=CARTESIAN_POINT('',(3.,-2.,5.));
#131=DIRECTION('',(0.,0.,1.));
#132=DIRECTION('',(0.6,0.8,0.));
#133=AXIS2_PLACEMENT_3D('',#130,#131,#132);
#134=PLANE('',#133);

#200=DIRECTION('',(0.6,0.8,0.));
#201=VECTOR('',#200,12.);
#202=LINE('',#100,#201);
#203=EDGE_CURVE('',#110,#111,#202,.T.);
#204=ORIENTED_EDGE('',*,*,#203,.T.);

#210=DIRECTION('',(-0.8,0.6,0.));
#211=VECTOR('',#210,8.);
#212=LINE('',#101,#211);
#213=EDGE_CURVE('',#111,#112,#212,.T.);
#214=ORIENTED_EDGE('',*,*,#213,.T.);

#220=DIRECTION('',(-0.6,-0.8,0.));
#221=VECTOR('',#220,12.);
#222=LINE('',#102,#221);
#223=EDGE_CURVE('',#112,#113,#222,.T.);
#224=ORIENTED_EDGE('',*,*,#223,.T.);

#230=DIRECTION('',(0.8,-0.6,0.));
#231=VECTOR('',#230,8.);
#232=LINE('',#103,#231);
#233=EDGE_CURVE('',#113,#110,#232,.T.);
#234=ORIENTED_EDGE('',*,*,#233,.T.);

#300=EDGE_LOOP('',(#204,#214,#224,#234));
#301=FACE_OUTER_BOUND('',#300,.T.);
#302=ADVANCED_FACE('',(#301),#134,.T.);
```

### 2.1 全部定义与参数真值

毫米长度上下文中，平面为 `S(u,v)=(3+(3u-4v)/5,-2+(4u+3v)/5,5)`。四条方向的范数恰为 1；不可把方向 ratio 未写成整数当作“已归一可不检查”的实现假设。源 LINE 参数 `t` 是 dimensionless，不能因毫米到其他长度单位的变换而缩放 `t`。

| LINE／EC | 源 `C(t)`，`t∈[0,1]` | 对应端点／长度 | PLANE UV 沿 LINE 的映射 |
|---|---|---|---|
| `#202/#203` | `(3+36t/5,-2+48t/5,5)` | A→B，12 mm | `(12t,0)` |
| `#212/#213` | `(51/5-32t/5,38/5+24t/5,5)` | B→C，8 mm | `(12,8t)` |
| `#222/#223` | `(19/5-36t/5,62/5-48t/5,5)` | C→D，12 mm | `(12-12t,8)` |
| `#232/#233` | `(-17/5+32t/5,14/5-24t/5,5)` | D→A，8 mm | `(0,8-8t)` |

全部 EC.same_sense、OE.orientation、bound.orientation、AF.same_sense 为 True。外环有效路径 A→B→C→D→A 相对 N=+Z 为 CCW；有向 UV 面积为 +96。源 EC 与 native 曲线递增方向、OE/use 路径及目标 UV occurrence 必须分别记录；不能仅因本基准都为 True 就省略这些映射。目标线采用 `q∈[0,1]` 时本例 `t=q`；这不覆盖 L1 非默认源参数的另一个控制。

### 2.2 独立真值和预期交付

| 项目 | S203-F1 独立真值／预期 |
|---|---|
| 区域 | `0≤u≤12,0≤v≤8`，不含孔；面积 96 mm²，周长 40 mm |
| 形心 | `(17/5,26/5,5)` mm |
| 世界 bbox | `[-17/5,51/5]×[-2,62/5]×{5}` mm |
| 源身份／使用数 | 4 VP、4 EC、4 OE、1 EDGE_LOOP、1 FOB、1 AF、1 OPEN_SHELL |
| 无规范化增拓扑时的 native 结构期望 | V/E/F=4/4/1，EU/FU/LU=8/2/2；仍须检查具体身份与方向 |
| 形状角色 | SurfaceModel／OpenShell，所选面侧 +Z；材料体积 NotApplicable，不把 0 作为实体体积成功 |
| 来源一致性 | 每个端点在其 LINE 和 PLANE 上精确残差 0；每条 LINE 全段 lift 残差 0，按系数证明 |
| 报告及发布 | 将来所有门槛通过才有 Accepted 模型；归一参数／目标有限 window 属有记录的等价表示，RepairReport 无修复动作；缺 GUUNC/sr/category 合法且不补造记录 |

本基准预计不产生源错误诊断。缺 uncertainty 不等于错误；若产品要求信息性消息，明确记 source declaration absent，不能伪报已采用某默认 uncertainty。几何、源规则与 profile 守卫的结果应各有证据。目标有限 window 至少包含 `[0,12]×[0,8]`，保护扩展不得改 trim；实际数域、容差及 `SmPlane` clamping 的控制另行验证。

## 3. S203-F2：只增加一个矩形孔的确切增量

从 **S203-F1** 出发，增加以下 30 条记录，只把原 `#302` 替换为末行的新记录。不能同时保留两条 `#302`。其余 F1 几何和封套文本不变；孔是 FACE_BOUND，外环继续是唯一 FACE_OUTER_BOUND。

```text
#104=CARTESIAN_POINT('',(2.6,0.8,5.));
#105=CARTESIAN_POINT('',(1.,2.,5.));
#106=CARTESIAN_POINT('',(3.4,5.2,5.));
#107=CARTESIAN_POINT('',(5.,4.,5.));
#114=VERTEX_POINT('',#104);
#115=VERTEX_POINT('',#105);
#116=VERTEX_POINT('',#106);
#117=VERTEX_POINT('',#107);

#240=DIRECTION('',(-0.8,0.6,0.));
#241=VECTOR('',#240,2.);
#242=LINE('',#104,#241);
#243=EDGE_CURVE('',#114,#115,#242,.T.);
#244=ORIENTED_EDGE('',*,*,#243,.T.);

#250=DIRECTION('',(0.6,0.8,0.));
#251=VECTOR('',#250,4.);
#252=LINE('',#105,#251);
#253=EDGE_CURVE('',#115,#116,#252,.T.);
#254=ORIENTED_EDGE('',*,*,#253,.T.);

#260=DIRECTION('',(0.8,-0.6,0.));
#261=VECTOR('',#260,2.);
#262=LINE('',#106,#261);
#263=EDGE_CURVE('',#116,#117,#262,.T.);
#264=ORIENTED_EDGE('',*,*,#263,.T.);

#270=DIRECTION('',(-0.6,-0.8,0.));
#271=VECTOR('',#270,4.);
#272=LINE('',#107,#271);
#273=EDGE_CURVE('',#117,#114,#272,.T.);
#274=ORIENTED_EDGE('',*,*,#273,.T.);

#310=EDGE_LOOP('',(#244,#254,#264,#274));
#311=FACE_BOUND('',#310,.T.);

#302=ADVANCED_FACE('',(#301,#311),#134,.T.);
```

上面最后一条是**替换行，不计入新增 30 条**。孔有效路径 E→F→G→H→E 的 UV 分别为 `(2,2),(2,4),(6,4),(6,2)`，方向相对 N 为 CW；有向面积 −8。

| LINE／EC | 源 `C(t)`，`t∈[0,1]` | 长度／UV 映射 |
|---|---|---|
| `#242/#243` | `(13/5-8t/5,4/5+6t/5,5)` | 2 mm，`(2,2+2t)` |
| `#252/#253` | `(1+12t/5,2+16t/5,5)` | 4 mm，`(2+4t,4)` |
| `#262/#263` | `(17/5+8t/5,26/5-6t/5,5)` | 2 mm，`(6,4-2t)` |
| `#272/#273` | `(5-12t/5,4-16t/5,5)` | 4 mm，`(6-4t,2)` |

独立区域为外矩形扣掉孔的内部；面积 `96−8=88` mm²，全部边界长 `40+12=52` mm，形心 `(189/55,297/55,5)` mm。世界 bbox 不变。源计数为 8 VP、8 EC、8 OE、2 EDGE_LOOP、1 FOB+1 FB、1 AF、1 OPEN_SHELL；无额外规范化拓扑时 native V/E/F=8/8/1、EU/FU/LU=16/2/4。仍无 material-solid 体积资格。

| 查询输入，mm | 独立输出 |
|---|---|
| `(21/5,48/5,5)`；UV `(10,6)` | InsideFace |
| `(3,3,5)`；UV `(4,3)` | InsideHole／OutsideFace；不能因在支撑平面内就算面内 |
| `(9/5,7/5,5)`；UV `(2,3)` | OnHoleBoundary |
| `(10,9,5)`；UV `(13,1)` | OutsideOuterBoundary |
| `(21/5,48/5,8)` 到面 | 最近点 `(21/5,48/5,5)`，距离 3 mm |
| `(14/5,19/10,8)` 到面 | 最近点 `(16/5,8/5,5)`，距离 `√37/2` mm；直接垂投进孔不是答案 |

预期源错误诊断仍为空；源和目标检查通过后才允许 Accepted。尤其应记录两个 bound 的源 ID 和有效方向，孔不得被填充、按正面积累加或变成第二外环。目标 UV 可以派生，但其 owner、完整覆盖、全段三维误差和区域必须经独立门槛验证，不是源 pcurve 正例。

## 4. S203-F3：负 UV 的单字段等价重参数

从 **S203-F2** 出发，只替换：

```text
#130=CARTESIAN_POINT('',(3.4,5.2,5.));
```

`#130` 只供平面 placement；它不是 `#106`，即使新坐标相等也不合并。所有顶点、LINE、EC 和 use 的世界定义、身份及方向保持原样。新平面为 `S'(u',v')=S(u'+6,v'+4)`，参数关系 `u'=u−6,v'=v−4`；这是一项源参数原点变化，严格导入不需修复。

| 环 | F3 源 UV |
|---|---|
| 外 A/B/C/D | `(-6,-4),(6,-4),(6,4),(-6,4)` |
| 孔 E/F/G/H | `(-4,-2),(-4,0),(0,0),(0,-2)` |

全部 F2 世界空间、面积、形心、bbox、分类和距离真值不变；有限 target window 至少覆盖 `[-6,6]×[-4,4]`。预期没有源错误或修复，参数映射被报告。若 native plane 将 O 错作 window 左下角而产生位移／clamp，即使 API 返回成功也应在目标几何验收拒绝；这属于构建实现反例，不修改此源正例的真值。

## 5. 单缺陷变化：输入、真值与预期原因

下面是[统一反例册](ap203e2-sample-cases.md)的来源设计，只冻结**语义级预期原因**，不预先承诺 C++ error enum、首条诊断排序或最终 API 名称。PF 编号用于本册审计定位，不额外增加统一目录的用例数量。所有阴性期望均无本次模型；已有发布模型不变。主原因须有原记录、字段／LIST位置、引用链及规则／项目门槛证据。一个缺陷可导出多个反证，不能为了“单缺陷”伪称它只影响一条 WHERE。表中明确哪些早期错误使后面的数值检查无需执行。

| ID／基准 | 唯一变化及确切输入 | 独立真值或反证 | 预期结果／诊断定位 |
|---|---|---|---|
| PF-N01／F1 | 只将 `#204` 写为 `ORIENTED_EDGE('', $,*,#203,.T.)` | 原世界定义不变；OE 的派生 start 槽必须是 `*`，`$` 不是合法替代 | Rejected／源映射或派生属性槽错误；定位 `#204` 第2槽。不得补端点后接纳；无需调用 native |
| PF-N02／F1 | 只将 `#300.edge_list` 第3项 `#224` 改 `#214`，即 `(#204,#214,#214,#234)` | 同一 OE 在 LIST 重复，违反 PATH 聚集 UNIQUE；也导致相邻端点不衔接，`#224` 失去使用 | Rejected／源聚集唯一性为直接反证；可以附 PATH/RI/孤立项连带原因，不能用去重或删除旧记录修复 |
| PF-N03／F1 | 只将 `#214.orientation` 改 `.F.` | EC/LINE/VP 世界定义均不变；OE214 变 C→B。204→214 是 B≠C，214→224 是 B≠C；首 OE.start 与尾 OE.end 仍为同一 A | Rejected／PATH.WR1、`path_head_to_tail`；定位 `#300` 第1/2及第2/3相邻项，不能误说本例还违反 EL.WR1 |
| PF-N04／F1 | 只将 `#203.same_sense` 改 `.F.` | VP#110/#111 在 LINE#202 上的参数仍为 0/1；与 EC 声明的反向关系冲突。OE 派生端点与闭环身份不变 | Rejected／项目源边方向一致性；定位 `#203.same_sense` 及端点参数 0、1，不伪报 EC 局部 WHERE（该实体无局部 WHERE） |
| PF-N05／F1 | 只将 `#201.magnitude` 由 `12.` 改 `0.` | VECTOR.WR1 的 `magnitude≥0` 仍 True；LINE 退化成 A 常值，B 到它距离 12 mm，区间无法形成非零边 | Rejected／本 profile 非零 LINE／端点一致性；定位 `#201/#202/#203`，不得报“负模长”或伪造微小边 |
| PF-N06／F1 | 只将 `#201.magnitude` 改 `-12.` | `magnitude≥0` 明确 False；不必先解释负向量形成的几何 | Rejected／VECTOR.WR1；定位 `#201.magnitude`，无需 native |
| PF-N07／F1 | 只将 `#132.direction_ratios` 改 `(0.,0.,1.)` | N 与给定 ref 平行，叉积模长 0；不能把非法给定值当作缺省 | Rejected／AXIS2_PLACEMENT_3D.WR4，引用 `#133.axis=#131/ref_direction=#132`；派生 axes 未定是连带结果 |
| PF-N08／F1 | 只将 `#131.direction_ratios` 改 `(0.,0.,0.)` | DIRECTION.WR1 False；平面轴定义已无效，其它曲线和顶点仍原样 | Rejected／DIRECTION.WR1，定位 `#131`；不得替换成默认 +Z |
| PF-N09／F1 | 只将 `#130` 改 `CARTESIAN_POINT('',(3.,-2.,5.000001))` | 支撑 PLANE 移到 z=5.000001，全部 VP／LINE 仍 z=5；独立点面和全边面距离都是 `10^-6` mm，超过 Q 的 `10^-7`。各源字段及方向规则可仍满足 | Rejected／项目边面几何一致性；定位 `#134/#133/#130` 与 `#302` 的各边使用；报告实际残差，不能依 native 的更大工作 tol 接纳或 snap |
| PF-N10／F1 | 只把 `#301` 叶名从 FACE_OUTER_BOUND 改成 FACE_BOUND，参数不变 | 仍只有一个边界，FACE.WR2 的 FOB≤1 成立；本 profile 必需的明确 FOB 数变 0。原几何矩形定义不变 | Rejected／ProfileUnsupported：需要显式外环，定位 `#302.bounds/#301`；不伪报 STEP 普遍要求恰一 FOB，不自行猜外环 |
| PF-N11／F1 | 只把 `#302` 叶名从 ADVANCED_FACE 改成 FACE_SURFACE，四槽不变 | 几何字段仍表达同一平面区域，但该具体叶超出本 profile；不能沿 AF 类型守卫证明 MSSR 的后续 WHERE | Rejected／ProfileUnsupported：plain FACE_SURFACE；不称其因四槽非法，也不修改 N2635 缺陷分支后放行 |
| PF-N12／F2 | 只把 `#311` 叶名改成 FACE_OUTER_BOUND，参数不变 | 同一 face 有两个显式 FOB，数量 2，FACE.WR2 False；原坐标不能消除该源角色冲突 | Rejected／FACE.WR2；定位 `#302.bounds=(#301,#311)`，不把任一 FOB 按面积改成孔 |
| PF-N13／F2 | 只将 `#311.orientation` 改 `.F.` | 孔有效路径反向；在 AF.same_sense=True／N=+Z 下，外环有向面积 +96，原孔变 +8，不能形成已声明的外正孔负区域；各局部字段与身份环仍可合法 | Rejected／项目面区域／bound方向一致性；定位 `#311.orientation` 及投影有向面积。不要把 104 当成一个合法新面面积或自动翻回 |

### 5.1 区域接触反例需协调依赖，不是假装单字段

**PF-N14／F2** 定义一个缺陷：将整个孔沿 UV 的 u 方向平移 −2，使其与外环接触。为保持其余定义正确，必须同步改四个 CP；这是一项几何缺陷、四处字面量变化，不声称只修改一个源槽。LINE 的 pnt 已引用这些 CP，其 DIRECTION／VECTOR 不变，因而每条边的端点／支撑线、面一致性仍精确成立。

```text
#104=CARTESIAN_POINT('',(1.4,-0.8,5.));
#105=CARTESIAN_POINT('',(-0.2,0.4,5.));
#106=CARTESIAN_POINT('',(2.2,3.6,5.));
#107=CARTESIAN_POINT('',(3.8,2.4,5.));
```

新孔 UV 为 `(0,2),(0,4),(4,4),(4,2)`，其 EF（EC `#243`）整段与外环 DA（EC `#233`）在 u=0、v∈[2,4] 重合。两个边的源身份依然不同，不得焊接。独立反证是**孔／外边界接触且重合长度 2 mm**，不是端点偏线或面外偏差。期望 Rejected／项目区域非简单／孔接触诊断，定位 `#302.bounds` 中的 `#301/#311`、`#233/#243` 和精确接触 UV 段。源组合规则通过也不代表区域可接纳；不得删除重合段、开槽或填孔。

PF-N09 和 PF-N14 用于区分几何一致性与区域验收，不能以更早注入的拼写／引用／字段错误替它们验收。PF-N10/PF-N11 是能力边界控制，不计入“已证明源 schema 非法”的数量。

更直接隔离 **3D LINE 与其源顶点冲突**的控制：从 F1 出发，仅将 `#202` 改为 `LINE('',#102,#201)`。LINE 平行移至平面 UV 的 v=8，其参数为 `u=12+12t,v=8`；A/B 到这条无限 LINE 的独立距离均为 8 mm，投影参数分别为 −1/0，仍与 EC.same_sense=True 的递增次序一致。全部 VP、PLANE、边身份及闭环保持原样，`#100` 仍由 VP#110 使用，不产生孤立记录。这一输入必须因顶点／LINE 残差拒绝，不能用 VP 两点另造原 AB 直线让其悄悄通过；统一册可优先采用此控制定位纯曲线冲突。

## 6. 手工闭包／计数复核表

以下是对本册设计文本的静态清点，**不是 parser 运行证据**。N 表示 DATA 实体记录数，不计 HEADER 记录、空行或注释。F2 替换 AF 后只计一个 `#302`；F3 只替换 `#130` 后仍只计一个。

| 实体叶型／角色 | F1 数量 | F2／F3 数量 |
|---|---:|---:|
| 共用封套 `#1`—`#12` | 12 | 12 |
| MSSR／SBSM／OPEN_SHELL | 1/1/1 | 1/1/1 |
| CARTESIAN_POINT（含平面 O） | 5 | 9 |
| VERTEX_POINT | 4 | 8 |
| DIRECTION（含 N/X） | 6 | 10 |
| AXIS2_PLACEMENT_3D／PLANE | 1/1 | 1/1 |
| VECTOR／LINE／EC／OE | 4/4/4/4 | 8/8/8/8 |
| EDGE_LOOP | 1 | 2 |
| FACE_OUTER_BOUND／普通 FACE_BOUND | 1/0 | 1/1 |
| ADVANCED_FACE | 1 | 1 |
| 本册 `#20` 以后的记录小计 | 39 | 69 |
| 完整 DATA 计划 N | **51** | **81** |

前向主路径为 `#9→#20→#21→#22→#302→bounds→loops→OE→EC→VP/LINE`，每条 LINE 再到 CP/VECTOR/DIRECTION；面支撑路径是 `#302→#134→#133→#130/#131/#132`。因此本册所有 39／69 条记录都从 `#20` 可达；唯一外出引用是 `#20.context_of_items=#12`，回到共用封套，不悬空。没有未列出的 `#135`、孔记录或第二个 AF。所有边用同一个 VP 对象连接，而非依坐标相同连接。

反向验证仍须使用全 DATA：全部 RI 有到同一 `#20` 的 RI 使用路径；两个/一个 EDGE_LOOP 均被对应 bound 使用；`#12` 的 representations_in_context 由 `#20` 填入；APD／AC、产品/context inverse、唯一键、派生 getter 的空属性集合及全部全局 RULE 守卫由共用封套与规则总册共同核对。不能仅凭本段几何可达就宣称全文件反向闭包已验证。

将来实现前需冻结完整文件字节、UTF 编码／换行、记录总数与 hash，再将这里的 S203-F1/F2/F3、PF-N01—PF-N14 映射到执行用例清单。当前位置仍为设计：没有输入文件 hash、运行输出、Accepted 模型或通过比例；数学真值与源记录设计已分开，不以被测实现的输出反推 oracle。
