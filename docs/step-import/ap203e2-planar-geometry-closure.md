# AP203e2 首个平面／盒体 profile：几何、拓扑与表示闭包

版本：P0-GC0.2，2026-10-15。本文只核对 `203e2-N2635` 候选的几何／拓扑／表示局部约束；与产品、单位、context、全局 RULE 的闭包合并后才能形成完整 profile。**仅文档，全部待运行；没有编码、STEP 样件或校验执行。**

唯一 EXPRESS 依据为 [MBx-IF 发布包中固定的 AP203e2 N2635 文件](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:1)，SHA256 `c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`。文件自述为 2009-09-10 revision 1.43、ISO/TS 10303-403、WG3 N2635；本文不是对所有 AP203 版本或最终 ISO 出版附件的符合声明。来源身份见 [AP203 专题](p0-ap203-provenance.md)，既有数学真值见 [P0 用例](p0-planar-cases.md)。下文 `L` 均是该固定文件的一基行号。

## 1. 检查边界与记录语义

本片具体类型仅限直接 `LINE`／`PLANE`，由 `VERTEX_POINT`／`EDGE_CURVE`／`ORIENTED_EDGE`／`EDGE_LOOP`／`FACE_BOUND`／`FACE_OUTER_BOUND`／`ADVANCED_FACE` 组成面。ABSR、MSSR 两条路径均只接 ADVANCED_FACE；FACE_SURFACE 保留为必要祖先，plain FACE_SURFACE 作为后续 Unsupported 分支，不独立准入。全 DATA 采用封闭单 product 链、一个具体 shape representation、一个单位几何 context；ABSR 恰含一个 MSB，或 MSSR 恰含一个 SBSM 且后者恰含一个 open/closed shell；可有多个实际使用的辅助 `AXIS2_PLACEMENT_3D` item。所有输入记录都属于此根及明确白名单的 APD/category/unit 等反向附件，不能用孤立的已知实体扩出另一个根。

表示 name 仅普通值；`centre of mass`／`treatment result` 特殊名称暂 Unsupported。PDS.name 排除配套产品闭包列出的六个 surface-condition 专名；PRPC可缺，或恰一个 `part`；其它记录及 complex partial 均限白名单。产品/context 路径及这些名称触发的全局条件由配套册定义。普通 `SHAPE_REPRESENTATION` 只在继承链出现，不能因为它能容纳 item 就越过首片的具体表示限制。下文描述某规则在更广 EXPRESS 输入上的分支，是为解释边界，不表示本片开放对应记录。

映射准入也封闭：几何、拓扑、表示、产品和 measure 记录只接已列叶型的 simple/internal mapping；complex 仅接 [context 册](ap203e2-planar-context-closure.md)明确列出的两种 context 签名和四种 unit 签名。相同叶型采用其它合法 external complex 映射仍是首片 Unsupported，不增加未登记 constituent，也不自动视作错误源。本文没有实现通用 schema 编译器／原式 evaluator，不承诺整份 N2635 能通用编译或执行。

以下分类不是“可以不解析该记录”的借口：

| 分类 | 含义与执行要求 |
|---|---|
| 必查 | 当前具体类型或其祖先的约束；每一适用实例均检查，局部表达式返回未判定时不得当 True |
| 条件必查 | 实例满足表内触发谓词时检查；同时检查谓词和集合覆盖，不以未实现的布尔短路掩盖必需类型 |
| 本片不进入 | 已证明确切类型／属性约束使某查询集为空，或函数在更早确定的分支返回；记录不触发的证明 |
| 首片 Unsupported | 该类型可能合法，但落在能力外；保留类型、位置及原因，不能剥掉包装后继续 |
| 源约束失败 | 对本文件明确的字段／聚集／WHERE／身份条件已有反证；与 Unsupported 和未判定分开 |

所有 `SET` 及 `LIST ... UNIQUE` 按 EXPRESS 实例身份与类型规则核对；`:=:` 是实例身份，不是坐标在容差内相近。一个 shared edge 的两次使用可以是不同 OE 指向同一 EC；不能以同一 EC 为由去重 OE，也不能把重复 OE 的 LIST 当合法。[PATH L8847–8853；EDGE_LOOP L5319–5326](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:8847)

## 2. 实体与祖先闭包：37 种声明

共 19 种本片准入具体实体、18 种祖先／保留声明；FACE_SURFACE 由具体输入转为祖先，37 种声明总数不变。`RI`、`GRI`、`TRI` 分别是 `representation_item`、`geometric_representation_item`、`topological_representation_item`。下表“局部字段”不重复继承字段；所有几何／拓扑 simple 记录都只有一个继承的 `RI.name:label`，多继承不增加第二个 name。表列 D/I 不是新的源显式槽。此声明清单不是允许输入祖先或任意 complex 的白名单，映射范围以 §1 为准。

| 声明（固定原文行） | 直接父类；SUPERTYPE 约束 | 局部显式／D 派生／I 逆向、聚集与局部约束 |
|---|---|---|
| RI L10029–10034 | 无父；列出 `ONEOF(...)` 子类 | E `name:label`；WR1 `SIZEOF(using_representations(SELF))>0` |
| GRI L6284–6293 | RI；`ONEOF` 包含本片 point/direction/vector/placement/curve/surface/edge_curve/face_surface/vertex_point/solid_model/SBSM 等 | D `dim:dimension_count=dimension_of(SELF)`；WR1 所有 using representations 的 context 都是 geometric context |
| TRI L11961–11964 | RI；`ONEOF(vertex,edge,face_bound,face,...,connected_face_set,(loop ANDOR path))` | 无新增字段／局部 WHERE；edge_loop 同时是 loop 与 path，不能错误按互斥子型拒绝 |
| point L9091–9094 | GRI；`ONEOF(cartesian_point,point_on_curve,point_on_surface,point_replica,degenerate_pcurve)` | 无新增；本片只有 cartesian_point |
| cartesian_point L3159–3162 | point | E `coordinates:LIST[1:3] OF length_measure`；本片 3 个坐标，非源基类型强制 3 |
| direction L4674–4679 | GRI | E `direction_ratios:LIST[2:3] OF REAL`；WR1 至少一个 ratio 不为零；本片恰 3 个 |
| vector L12204–12210 | GRI | E `orientation:direction,magnitude:length_measure`；WR1 `magnitude≥0`，不冒充字段本来必须正值 |
| placement L9013–9017 | GRI；`ONEOF(axis1_placement,axis2_placement_2d,axis2_placement_3d)` | E `location:cartesian_point` |
| axis2_placement_3d L2744–2756 | placement | E `axis,ref_direction:OPTIONAL direction`；D `p:LIST[3:3] OF direction=build_axes(...)`；WR1–WR4 见 §3 |
| curve L4055–4058 | GRI；`ONEOF(line,conic,pcurve,surface_curve,offset_curve_2d,offset_curve_3d,curve_replica)` | 无新增；本片直接 line |
| line L6889–6895 | curve | E `pnt:cartesian_point,dir:vector`；WR1 `dir.dim=pnt.dim` |
| surface L11283–11286 | GRI；`ONEOF(elementary_surface,swept_surface,bounded_surface,offset_surface,surface_replica)` | 无新增 |
| elementary_surface L5531–5535 | surface；`ONEOF(plane,cylindrical_surface,conical_surface,spherical_surface,toroidal_surface)` | E `position:axis2_placement_3d` |
| plane L9033–9035 | elementary_surface | 无新增；simple 是 `name,position`，没有独立半径／域字段 |
| vertex L12241–12243 | TRI | 无新增 |
| vertex_point L12252–12255 | vertex,GRI | E `vertex_geometry:point`；本片只接 cartesian_point，不把基类字段原类型改写 |
| edge L5251–5256 | TRI；`ONEOF(edge_curve,oriented_edge,subedge)` | E `edge_start,edge_end:vertex` |
| edge_curve L5312–5316 | edge,GRI | E `edge_geometry:curve,same_sense:BOOLEAN`；本片 direct LINE；无局部 WHERE |
| oriented_edge L8716–8729 | edge | E `edge_element:edge,orientation:BOOLEAN`；D 重声明 start/end，由 orientation 选择底层 edge 端点；WR1 禁止 edge_element 再是 OE |
| path L8847–8853 | TRI；`ONEOF(edge_loop,oriented_path)` | E `edge_list:LIST[1:?] OF UNIQUE oriented_edge`；WR1 `path_head_to_tail(SELF)` |
| loop L6950–6953 | TRI；`ONEOF(vertex_loop,edge_loop,poly_loop)` | 无新增 |
| edge_loop L5319–5326 | loop,path | D `ne=SIZEOF(edge_list)`；WR1 第一条 OE.start 与最后 OE.end 身份相同 |
| face_bound L5958–5962 | TRI | E `bound:loop,orientation:BOOLEAN` |
| face_outer_bound L5965–5967 | face_bound | 无新增 |
| face L5941–5949 | TRI；`ONEOF(face_surface,subface,oriented_face)` | E `bounds:SET[1:?] OF face_bound`；WR1 不混 poly_loop 与非 poly_loop；WR2 显式 FOB 数量≤1 |
| face_surface L5970–5976 | face,GRI | 本片仅 AF 祖先，不独立准入；E `face_geometry:surface,same_sense:BOOLEAN`；WR1 geometry 不能为 ORIENTED_SURFACE |
| advanced_face L2147–2181 | face_surface | **没有重声明 face_geometry 的属性/SELECT**；10 条 WHERE 在本文件限制几何、环、边、端点，见 §4 |
| connected_face_set L3856–3860 | TRI；`ONEOF(closed_shell,open_shell)` | E `cfs_faces:SET[1:?] OF face` |
| open_shell L8615–8617 | connected_face_set | 无新增字段／局部 WHERE |
| closed_shell L3441–3443 | connected_face_set | 无新增字段／局部 WHERE；名称及 schema 结构不是已经证明几何闭合 |
| solid_model L10767–10770 | GRI；`ONEOF(csg_solid,manifold_solid_brep,swept_face_solid,swept_area_solid,swept_disk_solid,solid_replica)` | 无新增 |
| manifold_solid_brep L7035–7038 | solid_model | E `outer:closed_shell`；无局部 WHERE；本片排除 void 与其它子型 |
| shell_based_surface_model L10461–10466 | GRI | E `sbsm_boundary:SET[1:?] OF shell`；WR1 `constraints_geometry_shell_based_surface_model(SELF)` |
| representation L10004–10018 | 无父 | E `name:label,items:SET[1:?] OF RI,context_of_items:representation_context`；D `description=get_description_value(SELF),id=get_id_value(SELF)`；WR1 id_attribute 反向数量≤1，WR2 description_attribute 反向数量≤1 |
| shape_representation L10442–10444 | representation | 无新增字段／局部 WHERE |
| advanced_brep_shape_representation L2122–2144 | shape_representation | 无新增；WR1–WR6 见 §5 |
| manifold_surface_shape_representation L7165–7330 | shape_representation | 无新增；WR1–WR14 见 §6 |

以上每一具体实体均同时继承其所有祖先的字段、聚集限制和 WHERE，不按“只执行最下层 WHERE”实现。这 37 种声明均没有声明自己的 `UNIQUE` 或 `INVERSE` 条款；不等于可以忽略聚集元素唯一性、REP 的反向 WHERE、context 的 inverse，或另册全局 RULE。

配套 TYPE：`label=STRING` L1134、`identifier=STRING` L1095、`text=STRING` L1822、`length_measure=REAL` L1142；`dimension_count=INTEGER` 且 WR1 `SELF>0` L839–842；`shell=SELECT(vertex_shell,wire_shell,open_shell,closed_shell)` L1696–1701；`vector_or_direction=SELECT(vector,direction)` L1928–1931；`founded_item_select=SELECT(founded_item,RI)` L998–1001。本片 `shell` 只选择 open/closed，余两项若实际出现则 Unsupported，不能按“壳”统一吞入。[TYPE 原文](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:839)

`SUPERTYPE OF` 的 ONEOF／ANDOR、实体多继承和 TYPEOF 完整集合必须按该固定 schema 解释；不得仅查最外层 simple 名。表中未列的合法 subtype 或复杂实例附加 partial 不会因命中一项祖先就自动得到本片支持。声明没有 `ABSTRACT` 不表示本项目支持单独构建任意祖先实体。

### 2.1 十九种准入具体类型的 simple 字段展开

以下顺序只用于本片准入的 Part 21 simple/internal mapping；本表不开放各实体的 external complex 映射。D `dim/p/ne/representation.description/id` 均不额外出现在 simple 参数中；OE 重声明的继承端点以 `*` 占位。

| 具体类型 | simple 参数序列 | 参数数 |
|---|---|---:|
| CARTESIAN_POINT | name,coordinates | 2 |
| DIRECTION | name,direction_ratios | 2 |
| VECTOR | name,orientation,magnitude | 3 |
| AXIS2_PLACEMENT_3D | name,location,axis,ref_direction（后两项缺省用`$`） | 4 |
| LINE | name,pnt,dir | 3 |
| PLANE | name,position | 2 |
| VERTEX_POINT | name,vertex_geometry | 2 |
| EDGE_CURVE | name,edge_start,edge_end,edge_geometry,same_sense | 5 |
| ORIENTED_EDGE | name,`*`,`*`,edge_element,orientation | 5 |
| EDGE_LOOP | name,edge_list | 2 |
| FACE_BOUND | name,bound,orientation | 3 |
| FACE_OUTER_BOUND | name,bound,orientation | 3 |
| ADVANCED_FACE | name,bounds,face_geometry,same_sense | 4 |
| OPEN_SHELL | name,cfs_faces | 2 |
| CLOSED_SHELL | name,cfs_faces | 2 |
| MANIFOLD_SOLID_BREP | name,outer | 2 |
| SHELL_BASED_SURFACE_MODEL | name,sbsm_boundary | 2 |
| ADVANCED_BREP_SHAPE_REPRESENTATION | name,items,context_of_items | 3 |
| MANIFOLD_SURFACE_SHAPE_REPRESENTATION | name,items,context_of_items | 3 |

保留的 plain FACE_SURFACE 若作 simple 记录，原字段顺序同样为 `name,bounds,face_geometry,same_sense` 四槽；这只是来源说明，不属于上表准入类型。相同槽数不代表它可替代 ADVANCED_FACE。

## 3. 几何、方向、环、面继承规则：逐条必查

| 规则 | 本片检查与“不触发”边界 | 证据 |
|---|---|---|
| RI.WR1 | 每个 RI 均有至少一个直接／间接 using representation。包括非 geometric 的 face_bound、OE、edge_loop、shell；不能只检查 representation.items 直接包含它 | L10029–10034；函数 L15647–15715 |
| GRI.dim、GRI.WR1 | CP/方向/vector 的 dim 由分量得出；其余 GRI 从使用它的 representation context 得出。所有使用处的 context 都必须是 geometric context；跨 context 共享还须对账 `compatible_dimension` 与单位解释 | L6284–6293、L14181–14218 |
| direction.WR1 | 非零方向；norm 的可表示性／近奇异证据另属数值验收，不把任意 epsilon 写成源规则 | L4674–4679 |
| vector.WR1、line.WR1 | magnitude≥0；dir.dim=pnt.dim。本片要求 LINE 实际非零且 3D 是额外几何／能力条件；magnitude=0 不能伪报成 vector.WR1 本身失败 | L12204–12210、L6889–6895 |
| axis2_placement_3d.WR1 | placement.location.dim=3 | L2751 |
| axis2_placement_3d.WR2 | axis 缺省时 OR 前项 True；有值时 axis.dim=3 | L2752 |
| axis2_placement_3d.WR3 | ref_direction 缺省时 OR 前项 True；有值时 ref_direction.dim=3 | L2753 |
| axis2_placement_3d.WR4 | 两者均有值才要求叉积 magnitude>0；任一缺省，源 WHERE 不调用“另一个必须非平行”的替代猜测。派生 p 的有效性仍检查，例如 axis 缺省但 ref 与默认 Z 平行会使 first_proj_axis 无法给确定轴，不得放行 | L2754–2755、L13494–13502、L14392–14427 |
| OE derived start/end、OE.WR1 | orientation=True 取底层 start/end，False 交换；simple start/end 两个 inherited-derived 槽为 `*`；edge_element 不能再是 OE。非 EC 的其它 edge 本片 Unsupported，不能自动展开或合并 | L8716–8729 |
| path.WR1 | 相邻 OE 的 end/start 身份一致；1 元素时相邻循环为空，这条本身 True，仍检查 edge_loop 收尾与本片多边形非退化 | L8847–8853、L15457–15471 |
| edge_loop.ne、WR1 | ne 是实际 LIST 大小；首 OE.start 与尾 OE.end 身份相同；不能只以首尾坐标相近通过 | L5319–5326 |
| face.WR1 | `mixed_loop_type_set(list_to_set(list_face_loops(face)))` 必须 False。本片全 EDGE_LOOP，分支中每项都非 POLY_LOOP，所以确定 False；多个 EDGE_LOOP 不构成混型 | L5946、L14962–14973、L15008–15019、L15046–15062 |
| face.WR2 | 显式 FOB≤1 是源规则；本片恰好 1 是项目能力条件。0 不伪报 schema 错误；2 个以上为源约束反证 | L5947–5948 |
| face_surface.WR1 | geometry 非 ORIENTED_SURFACE；direct PLANE 必查后满足。face.same_sense 可真可假，不能借此要求所有面 True | L5970–5976 |

plane 本身没有额外 WHERE 或参数窗。源无限平面的有限目标 window 是适配规范化，仍由 [SMLib 审计](p0-smlib-source-audit.md)验证；源 LINE 参数、源 PLANE 参数、单位转换、OE/EC/bound/face 四个方向因子分别保留，不把字段数和 WHERE 通过当作该映射已完成。

## 4. ADVANCED_FACE 的 10 条 WHERE

适用于每个具体 ADVANCED_FACE，无论由 ABSR 还是 MSSR 使用。MSSR 对 advanced face 的若干 OR 条件为 True，不会消除这些局部／继承检查。

| 规则／原文行 | 本片必查内容 | 被证明不进入的候选分支 |
|---|---|---|
| WR1 L2150 | face_geometry TYPEOF 与 {ELEMENTARY_SURFACE,B_SPLINE_SURFACE,SWEPT_SURFACE} 的交集恰 1；PLANE 命中 ELEMENTARY | B-spline、swept 若出现均 Unsupported |
| WR2 L2151–2154 | 每个 EDGE_LOOP bound 中每个 OE.edge_element 是 EC | 非 EDGE_LOOP 不是靠此条排除，另见 WR8／能力边界 |
| WR3 L2155–2158 | 每个上述 EC 的 geometry 命中 {LINE,CONIC,POLYLINE,SURFACE_CURVE,B_SPLINE_CURVE} 恰 1；direct LINE 满足 | 其余类型即使源可合法也 Unsupported；不能剥 SURFACE_CURVE |
| WR4 L2159–2162 | 上述 OE 的派生 start/end 均 VP，且各 VP.geometry 均 CP；orientation 不影响是否具备该类型 | POINT_ON_CURVE/SURFACE 等不进入 |
| WR5 L2163–2165 | EDGE_LOOP bound 不同时带 ORIENTED_PATH 类型；检查完整 TYPEOF，不只 simple 名 | 本片不接 oriented path |
| WR6 L2166 | 不是 SWEPT_SURFACE，左项 True；该适用性结论仍登记 | swept_curve 的 LINE/CONIC/POLYLINE/BSPLINE 类型检查不进入 |
| WR7 L2167–2169 | VERTEX_LOOP bound 查询集为空，量词通过 | 不进入其 loop_vertex→VP→CP 访问；若出现先 Unsupported |
| WR8 L2170–2171 | 每个 bound.loop 命中 EDGE_LOOP／VERTEX_LOOP 恰 1；本片全部 EDGE_LOOP | VERTEX_LOOP 合法候选但本片不支持；POLY_LOOP 违反此条 |
| WR9 L2172–2176 | 识别所有 EC.geometry；全 direct LINE，因此 SURFACE_CURVE 谓词为 False，违规集合为空 | associated_geometry 每项 PCURVE 的检查不进入 |
| WR10 L2177–2180 | 两半都检查适用性：面非 swept；边非 POLYLINE | swept-polyline 和 edge-polyline 的点数≥3 分支均不进入 |

SOURCE `advanced_face` 不定义 pcurve 一致性、面环自交、边长正值、面面积、壳几何闭合或材料体积的数值真值。以上通过后仍要执行本文 §9 的精确几何验收，不能把未明写在 WHERE 中的几何一致性省掉。

## 5. ABSR 的 6 条 WHERE

本片直接 material solid 分支；允许辅助 A2P3D，不允许 MAPPED_ITEM、FACETED_BREP 或 BREP_WITH_VOIDS 实际实例进入。具体类型筛选和源规则的区别如下。

| 规则／行 | 必查 | 本片不进入 |
|---|---|---|
| WR1 L2125–2126 | 每项 TYPEOF 与 {MSB,FACETED_BREP,MAPPED_ITEM,A2P3D} 交集恰 1；本片具体 MSB 或 A2P3D | 未支持的其它合法 item 不能删去；FACETED_BREP 同时是 MSB 的类型交集细节按原式检查，不能把四类写成宽松 any |
| WR2 L2127–2128 | 至少一个 item 命中 MSB／MAPPED_ITEM 恰 1；本片须有 MSB，只有轴不满足 | MAPPED_ITEM 路径不进入 |
| WR3 L2129–2133 | 每个 MSB 的 `msb_shells` 全部 face 都是 ADVANCED_FACE；本片只 outer | voids 扩展集不进入；普通 FACE_SURFACE 不能用于本片 ABSR |
| WR4 L2134–2136 | 每个 MSB.outer 不为 ORIENTED_CLOSED_SHELL | 同名 closed_shell 祖先不能掩盖该额外类型 |
| WR5 L2137–2140 | BREP_WITH_VOIDS item 查询集为空 | 不进入 voids 各 oriented shell.orientation 为 False 的检查；出现即 Unsupported |
| WR6 L2141–2143 | MAPPED_ITEM 查询集为空 | 不进入 mapped representation 必须 ABSR 的检查；出现即 Unsupported |

`msb_shells` 的 TYPEOF 检查为通配 `*BREP_WITH_VOIDS`，不是简单字符串后缀在源记录上的扫描。首片已排除该子型时返回集准确等于 `[outer]`。[函数 L15064–15076](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:15064)

## 6. MSSR 的 14 条 WHERE 与 SBSM

首片先证明所有 face 的具体叶型均为 ADVANCED_FACE。WR6–WR14 的各个 per-face 约束均有 `('...ADVANCED_FACE' IN TYPEOF(fa)) OR B`；第一项已确定 True，最终 OR 值为 True，外层失败 QUERY 不收入该 face。这是受限 profile 的最终判定证明，不是说原文 evaluator 必须短路，也不是执行或修订 plain-FS 的 B 分支。所有 AF 自身／祖先 WHERE 及项目几何条件仍必查。

| 规则／行 | 本片必查内容 | 不触发证明／条件 |
|---|---|---|
| WR1 L7168–7171 | items 每项命中 SBSM／MAPPED_ITEM／A2P3D 恰 1 | 实际只 SBSM 或辅助 A2P3D |
| WR2 L7172–7174 | 至少 1 项 SBSM／MAPPED_ITEM | 本片至少 1 SBSM，不能只有 axis |
| WR3 L7175–7183 | mapped query 为空 | 不进入 mapped representation 是 MSSR 且含 SBSM 的要求 |
| WR4 L7184–7191 | 每个 SBSM boundary 命中 OPEN_SHELL／ORIENTED_CLOSED_SHELL／CLOSED_SHELL 恰 1 | 首片 exact OPEN/CLOSED；有 oriented subtype 不按祖先吞入 |
| WR5 L7192–7198 | 每个 boundary face 都是 FACE_SURFACE；AF 继承满足 | plain FS 虽满足本条，仍因首片范围而 Unsupported；裸 FACE 不满足 |
| WR6 L7199–7207 | 每个 fa 的 AF 类型谓词 True，使 OR 与外层规则明确为 True | 不执行 `msf_surface_check`；本片不依赖其 plain-FS 支路 |
| WR7 L7208–7218 | 同一 AF 第一项 True 的最终判定证明 | 非 AF 的 bounds 类型检查 B 不用于本片判定；实际所有 AF 的环仍由其 WR8 检查 |
| WR8 L7219–7230 | AF 第一项 True | B 原文在 L7228 对 FACE_BOUND 直接取 `elp_fbnds\path.edge_list`，见缺陷表；不据此声称逐边 EC 检查已执行 |
| WR9 L7231–7252 | AF 第一项 True | B 原文 L7241 同类访问缺陷；保留8类曲线集合原文，不私自补`.bound`后评价 |
| WR10 L7253–7264 | AF 第一项 True | B 原文 L7262 同类访问缺陷；`msf_curve_check` 不在本片活跃执行闭包 |
| WR11 L7265–7280 | AF 第一项 True | B 原文 L7274 同类访问缺陷；本片 VP 约束从 AF.WR4／字段和项目条件证明 |
| WR12 L7281–7302 | AF 第一项 True | B 原文 L7290 同类访问缺陷；本片 CP 约束从 AF.WR4／字段和项目条件证明 |
| WR13 L7303–7314 | AF 第一项 True；另有全片无 VERTEX_LOOP 的范围证明 | B 原文 L7313 对 FACE_BOUND 直接取 `vlp_fbnds\vertex_loop.loop_vertex`；不进入、不修订 |
| WR14 L7315–7329 | AF 第一项 True；另有全片无 VERTEX_LOOP 的范围证明 | B 原文 L7328 同类 vertex-loop 访问缺陷；不进入、不修订 |

额外始终适用的 `SBSM.WR1` 调用 `constraints_geometry_shell_based_surface_model`，检查所有 boundary 是 open 或 closed shell。其 SELECT 还允许 vertex_shell/wire_shell，但函数明确排除，不能只用 SELECT 类型通过；本片 exact open/closed 均满足。MSSR.WR4 不能代替 SBSM 的自身 WHERE。[L10461–10466、L13802–13820](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/ap203/ap203e2-mbx.exp:13802)

### 6.1 保留原字节的 N2635 访问路径缺陷

以下变量都由 `QUERY(bnds <* fa.bounds | ... TYPEOF(bnds.bound))` 得到；其值是 FACE_BOUND，而不是筛选所引用的 loop。固定文件随后直接限定为 PATH／VERTEX_LOOP，没有中间 `.bound`。本册撤回 GC0.1 对 plain FACE_SURFACE 已逐边核查的泛化，不将原作者可能的意图当作可执行原式。

| WHERE／原文访问行 | 原文变量来源行 | 未补写的实际访问 |
|---|---|---|
| WR8 L7228 | L7226–7227 | `elp_fbnds\path.edge_list` |
| WR9 L7241 | L7238–7239 | `elp_fbnds\path.edge_list` |
| WR10 L7262 | L7260–7261 | `elp_fbnds\path.edge_list` |
| WR11 L7274 | L7272–7273 | `elp_fbnds\path.edge_list` |
| WR12 L7290 | L7288–7289 | `elp_fbnds\path.edge_list` |
| WR13 L7313 | L7310–7311 | `vlp_fbnds\vertex_loop.loop_vertex` |
| WR14 L7328 | L7322–7323 | `vlp_fbnds\vertex_loop.loop_vertex\vertex_point.vertex_geometry` |

首片通过 AF 类型守卫取得上述 OR 最终值的静态证明；不承诺通用 schema 编译器会容忍这些访问，不承诺对 plain FS 求值的行为，也不靠宿主语言遇错兜底。若未来接纳 plain FS／vertex loop，必须取得独立勘误或明确命名的修订契约、重新展开相关规则与测例，并显式执行几何／拓扑守卫；不能沿用“首片 AF 使最终值恒真”的证明。

CLOSED_SHELL 在 MSSR/SBSM 中仍是表面模型。不得因为闭合性或体积值恰似盒体就自动转换成 ABSR/MSB material solid；反向也不得因普通 FACE_SURFACE 方便而把实体转换成表面模型。

## 7. 局部函数依赖闭包：22 个静态引用，20 个活跃

以下是从本册实体的 DERIVE／WHERE 静态引用可达的22个命名函数。20个属于首片活跃闭包；`msf_surface_check`／`msf_curve_check` 两个由 AF 类型守卫屏蔽，不是必执行函数。EXPRESS 内置 TYPEOF、USEDIN、SIZEOF、QUERY、EXISTS、NVL、HIINDEX、SQRT、实例／聚集操作另属于必须具备的求值语义。`item_in_context` 是全局 context 检查的跨域入口，非下列局部实体直接新增调用，见 §8。此分支简化以受限 profile 的等价结果为目标，不要求或宣称已实现通用原式 evaluator。

| 函数／行 | 直接命名依赖 | 本片触发分支与不进入分支 |
|---|---|---|
| `using_representations` L15678–15715 | `using_items` | 先取直接 REPRESENTATION.ITEMS 的 USEDIN，再把所有间接 using item 所属 representation 加入；这两部分都必查，不能只用直接项 |
| `using_items` L15647–15676 | `bag_to_set`,`using_items` 自递归 | 全部 USEDIN 中 RI/founded_item 过滤；visited 集避免重复递归。首片几何／拓扑 RI 逆向链实际进入；若出现外来 founded/presentation user，必须纳入影响判断，不能假装没有 |
| `bag_to_set` L13423–13435 | 无 | 非空 bag 按实例去重；空集支路也要确定，不从原字段顺序推导 SET 顺序 |
| `dimension_of` L14181–14218 | `using_representations` | CP按coordinates数、direction按ratios数、vector按orientation ratios数；其余GRI取using_rep context。不得在未核 compatible_dimension 前随便取第一个 context 成为唯一真值 |
| `build_axes` L13494–13502 | `normalise`,`first_proj_axis`,`cross_product` | axis缺省取+Z；构造X、Y、Z。axis存在且非法不能与“缺省”混同 |
| `normalise` L15372–15419 | 无 | 缺失返回?；direction非零归一；vector分支对magnitude=0返回?。build_axes中常用direction；first_proj_axis等仍可能带入vector，不能删掉vector路径。错误路径不可静默默认化 |
| `first_proj_axis` L14392–14427 | `normalise`,`cross_product`,`scalar_times_vector`,`dot_product`,`vector_difference` | ref缺省时Z≠±X选+X，Z=±X选+Y；有ref时要求3D且与Z非平行；随后投影去Z分量再归一。缺失Z／坏维数／平行支路给?，不是修复许可 |
| `cross_product` L13938–13968 | `normalise` | 缺失或2D返回?；本片3D归一后叉积；非平行给正magnitude，平行给0。平行错误控制实际需要验证此分支 |
| `dot_product` L14284–14315 | `normalise` | 缺失或维数不一致给?；本片3D归一后内积，不能拿未归一ratio直接代入 |
| `scalar_times_vector` L15492–15519 | `normalise` | vec为direction或vector两支均保留；scalar负值翻方向并取绝对magnitude，零值生成零vector，是投影中合法中间结果，不能套LINE非零限制 |
| `vector_difference` L16329–16375 | `normalise` | 缺失／维数不等给?；direction隐含magnitude1，vector用其magnitude；差非零给结果，零差给0vector。投影中需按真实量值计算 |
| `boolean_choose` L13475–13482 | 无 | OE.orientation真／假两条均支持；决定派生端点身份 |
| `path_head_to_tail` L15457–15471 | 无 | 从第2项起逐对身份比较；1项空循环 True；首尾另查EL.WR1 |
| `list_face_loops` L14962–14973 | 无 | 将每个face bound.bound收入LIST；不能按孔方向排序修改源身份 |
| `list_to_set` L15008–15019 | 无 | loop实例身份去重后供混型判定；不等于允许合并源bound |
| `mixed_loop_type_set` L15046–15062 | 无 | 集合≤1直接False；多loop比较POLY_LOOP类型布尔值，全EDGE_LOOP为False；混poly控制需识别并拒绝 |
| `msb_shells` L15064–15076 | 无 | 本片只返回outer；BREP_WITH_VOIDS通配类型分支不进入 |
| `constraints_geometry_shell_based_surface_model` L13802–13820 | 无 | 每个boundary open/closed；其它shell立即False，覆盖本片负控制 |
| `msf_curve_check` L15078–15180 | 自递归、`msf_surface_check` | **静态引用、非活跃**：MSSR.WR10 的 AF 第一项 True；其 B 还有上述访问缺陷，不执行函数或递归。函数自身LINE→True的文字分析不证明原WHERE调用路径有效；所有其余支路本片也不进入 |
| `msf_surface_check` L15182–15221 | `msf_curve_check`、自递归 | **静态引用、非活跃**：MSSR.WR6 的 AF 第一项 True，不调用函数。函数自身ELEMENTARY→True及swept/offset/replica/BSPLINE支路不进入；不是本片几何正确性的证据 |
| `get_description_value` L14575–14586 | 无 | REP反向description_attribute恰1时取attribute_value；0返回?且不是缺失必需源字段；>1违反REP.WR2 |
| `get_id_value` L14588–14599 | 无 | REP反向id_attribute恰1时取attribute_value；0返回?且不强制补id_attribute；>1违反REP.WR1 |

函数派生构造用到 `dummy_gri` 常量 L46–47：它是函数值构造的 RI('')||GRI() 基底，不是要求输入文件另有一个 dummy GRI 实体，更不能把常量带入实体实例的 RI.WR1 扫描。这是值级派生过程，与输入实例登记分开。

`second_proj_axis`、`base_axis`、`build_2axes`、`orthogonal_complement` **不在**本片 A2P3D 的 `build_axes` 调用图；`conditional_reverse`、各 topology_reversed、face_bound_reversed／edge_reversed／path_reversed **不在**本片源 OE/FB/AF（含其FS祖先）的派生调用图。后续 native为保形而反转循环是适配实现，不能据此声称源schema已触发这些函数。`advanced_face_properties` 同样不是 ADVANCED_FACE 这10条WHERE直接调用；若全局RULE引入它，由RULE册另列入口和分支。

## 8. 全 DATA 反向影响闭包与跨册接口

前向图仅从 MSB/SBSM 走到曲线、点、轴不够。以下索引和作用域必须保留；它们是依赖闭包的一部分，即使目标只构建一个根。

| 入口 | 必须补齐的反向／跨域依赖 | 本册结论与交界 |
|---|---|---|
| RI.WR1/GRI.WR1/GRI.dim | 每个input item的USEDIN全部引用者；经RI/founded_item传递的representation集合；各representation的context | using_items防重复不等于允许几何定义循环；未知user无法证明无影响时不冻结profile。context类型/兼容维数/单位由context册及RULE册共同核 |
| representation.WR1/WR2与两个DERIVE | 全DATA的ID_ATTRIBUTE.IDENTIFIED_ITEM、DESCRIPTION_ATTRIBUTE.DESCRIBED_ITEM精确角色索引；涉及的attribute值与SELECT类型 | 本片封闭白名单不含这两类附加实例，计数均0，允许derived=?；仍核完整DATA，无隐藏附件。若实际存在则本片Unsupported，不能因它未被前向引用就静默忽略；扩展白名单须重新展开实体及WHERE |
| `item_in_context(item,cntxt)` L14923–14951 | 直接representation.items和context.representations_in_context求交；否则沿全部RI引用者递归 | 由需要该函数的全局规则触发；并非using_representations的内部调用。context册必须登记；源函数无显式visited参数，未来执行器需有资源/循环策略，不能无限递归后给假成功 |
| `representation_context` L10021–10026 | I `representations_in_context:SET[1:?] OF representation FOR context_of_items` | inverse最小1由实际全DATA使用者确定；context册核全部字段／complex；不为每个rep克隆context掩盖共享关系 |
| 额外表示/产品关系或presentation用户 | 可改变根、几何context、定义/实例或同一item使用语义的反向关系 | 本片无assembly/mapped/relationship装配/PMI定义链，不自动抽取最大MSB。全局RULE触发集合仍按原FILE作用域或明确影响规则检查，不能改成前向子图后宣称完整AP符合 |

已明确不接的几何包装／子型是 `SURFACE_CURVE/PCURVE/TRIMMED_CURVE`、其余曲线曲面族、plain `FACE_SURFACE`、`ORIENTED_FACE/ORIENTED_OPEN_SHELL/ORIENTED_CLOSED_SHELL/ORIENTED_PATH`、`VERTEX_LOOP/POLY_LOOP`、`BREP_WITH_VOIDS/FACETED_BREP/MAPPED_ITEM`。识别为能力外，并保留源身份；不得将其必需关联划为无影响元数据。上述名字出现在 WHERE 的允许集合或函数分支中，不会自动扩展本片支持范围。plain FS 延后仅针对该首 profile，不撤销总体首版对开放壳／精确表面模型的目标；所有既有平面/带孔/开放折面/盒体数学真值仍可用 AF 表达。

## 9. 几何必查与 EXPRESS 必查不能互代

以下仍全部必查，来源是已确认的精确BRep/无修复项目契约和 [独立数学真值](p0-planar-cases.md)，不是本文件新增的 EXPRESS WHERE。

| 项目必查 | 与源schema的边界 |
|---|---|
| 三维坐标、有限数、可算的非零方向／LINE段；单位与参数映射明确 | 基类型length_measure为REAL、vector magnitude≥0，不代替非零边与数值可表示性 |
| 源VP／LINE端点残差、边在PLANE上、一条EC各face use一致 | EC/FS没有局部WHERE要求端点落在curve或trim在surface；按显式质量配置判定，报告残差，不移动定义 |
| 环连接身份、无重复OE，简单多边形、非零面积、孔内含/不相交/不相触、不嵌套歧义 | EL只补收尾，PATH补相邻身份；FACEx2条不证明区域拓扑；SET不排序外孔 |
| 一个明确FOB、明确bound与face方向，EC/OE/FB/FS各sense保留 | 源FACE只限制FOB≤1，本片缺FOB为Unsupported；`.F.`不自动等于坏数据 |
| 共享edge身份及面邻接，开放壳保持开放，闭合实体闭合/定向/非自交/材料语义正确 | connected_face_set只有faces集合，open/closed entity无局部几何证明；MSB.outer引用CLOSED_SHELL不等于实际solid验收通过 |
| 目标owned geometry、UV occurrence、有限window、容差与清理/原子发布 | 原文schema不能证明SMLib适配、所有权或失败清理；沿既定P0/P2门槛，不执行repair |

独立控制沿用 L1、F1/F2/F3、S1、B1/B2、U1。B1要求 source共享12条EC、24个逻辑face-edge use、8个VP，不接受把相同坐标点当8个topology identity的替代做法；B2相同几何仍是MSSR表面模型。项目验证不授予完整AP符合资格。

## 10. 应绑定的规则控制：Defined，全部 NotRun

以下是本册的设计控制清单，供主用例目录分配稳定ID和合成样件；不是新增已运行测试数。每个阴性控制只改变一条原因，避免以更早无关错误掩盖目标规则。

| 控制组 | 阳性／单一变化阴性与预期 |
|---|---|
| GC01 GRI/RI反向 | 合法完整产品表示树中的所有RI均被间接使用；移去某内部实体唯一传递使用或加入不兼容context使用，必须给无使用／context证据，不能仅查directitems |
| GC02 聚集身份 | 多个不同OE引用同一EC可按语义使用；同一OE在同一PATH重复违反LIST UNIQUE；同坐标不同VP不满足PATH/EL身份连接 |
| GC03 PATH与EL | 连接的多边形；破坏中间邻接与只破坏首尾分别命中PATH.WR1与EL.WR1；1OE闭合退化的schema路径与项目非零面积拒绝分开 |
| GC04 轴与派生 | axis/ref分别缺省、均缺省、有效斜ref、Z=±X缺省ref；错误维数／零方向／明确平行拒绝；axis缺省+ref平行默认Z使p未判定，不假造X |
| GC05 LINE与VECTOR | 合法非单位ratio和magnitude；负magnitude命中vector.WR1；零magnitude满足该WHERE但不满足本片非零LINE；point与dir维数不等命中line.WR1 |
| GC06 FACE继承 | 多EDGE_LOOP外环+孔；0FOB为能力外，2FOB为源反证；混POLY_LOOP与EL命中face.WR1，AF单POLY_LOOP命中AF.WR8；不要混同两种原因 |
| GC07 AF局部10条 | 每条WHERE的触发/空集证据都登记；不同source sense的所有合法组合有相同区域/外法线真值。SURFACE_CURVE、swept、vertexloop只验证准确Unsupported，不伪称执行其内部完整语义 |
| GC08 ABSR | MSB+可选axis成功候选；只有axis命中WR2；普通FS替AF先因本片范围Unsupported，对原ABSR.WR3的反证可独立记录；带oriented外壳源失败／能力外原因分清；mapped/void/faceted不剥包装 |
| GC09 MSSR | AF+PLANE证明WR6–WR14各OR第一项True；每个AF本身10WHERE及继承检查仍完成。plainFS+PLANE准确Unsupported，不进入或修补缺陷分支；open与closed SBSM仍保持表面角色；只有axis、裸FACE、vertex/wire shell准确拒绝 |
| GC10 REP反向元数据 | 无id/description_attribute允许派生?；各1取值；相应2实例违反各WR；附件存在时不得仅因“不影响体积”跳过本地反向计数 |
| GC11 参数/几何 | 非默认LINE参数、斜平面、带孔、偏移planewindow、共享折面和B1/B2身份控制；EXPR全部True但curve/vertex/plane残差超预算仍拒绝 |
| GC12 函数资源 | using_items有visited且可追溯结果；跨context多use全部收集；数据循环、未知user、未判定、资源超限不交模型；不声称源函数visited已证明所有定义图无环。固定20活跃/2屏蔽引用，plainFS不能通过伪造AF类型或宿主异常兜底绕过 |

本册完成的是固定 EXPRESS 文件下受限 AF profile 的37声明（19准入具体/18祖先或保留）与22静态函数引用（20活跃/2屏蔽）依赖清点，并如实保留 MSSR.WR8–WR14 的原文访问问题。P0退出还要求：跨册全局规则/产品/单位/context闭包对账、记录布局及六种complex签名与simple记录白名单冻结、每个必查/不触发有主用例绑定、专用检查等价性及实现/执行证据另获授权后完成。没有编译或执行整份N2635，也没有声称plainFS原式能通用求值。本文全部检查均为文档定义、NotRun。
