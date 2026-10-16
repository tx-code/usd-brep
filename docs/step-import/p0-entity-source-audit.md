# P0 平面／盒体切片：STEP 实体与来源核对

日期：2026-10-15。性质：**设计研究材料，不是实现、生成的 STEP 样件或已通过的测试**。依据[已确认规格 0.1](../step-import-repair-spec.md)，本轮只细化首个 direct `LINE`／`PLANE` 切片，产品转换链路仍完全自研。

## 1. 证据范围与版次限制

主要结构依据是 STEP Tools 自行发布的 [EXPRESS HTML 字典](https://www.steptools.com/docs/stp_aim/html/schema.html)。该索引明确为合并 schema，列出 AP214 e3、AP203 e2、AP242 e4、部分 proposed extras 及其他 AP 来源；它不是某一 AP 原文。其[变更记录](https://www.steptools.com/docs/stp_aim/news_exp.html)也确认 21.0 更新到 AP242 e4。实体页的 `IN`、`DIFF` 和 `CASE DIFF` 标记是必须继续核对版次的线索，不能由同名实体推导所有版次等价。

第二证据是本机 OCCT 源码，固定 commit `3d097a0328e71b826377d4814ab05ec3c3d23871`，根目录 `E:/Coding/OCCT`。以下 RW reader/writer 仅用于交叉核对 simple Part 21 参数顺序，不引入产品依赖，也不照搬兼容性放宽、隐式修复或 C++ 容器类型。本轮未调用 OCCT reader。

本报告列出 **38 个需区分的记录类型**：6 个几何、12 个拓扑／形状、4 个表示、10 个产品链、2 个上下文、2 个 AP203 历史上下文、2 个已知分类元数据；单位复合实体由独立单位规则材料补齐。派生属性及基类另列，不把它们都算成要直接接受的实例。**本轮完成的是这些类型在公开合并字典／固定实现中的结构核对；完成的 AP edition 完整符合性认证为 0。**

后续已从当前 MBx-IF 入口取得五份 longform 并固定 hash，另有明确修改过的 AP203e1 STEPcode 副本，详见[profile 附件](p0-schema-profiles.md)。这些文件纠正了 merged 表的部分跨版次泛化；**下表以 merged 为结构索引，版次报告的差异优先**。取得文件不等于最终 ISO 原件认证或运行 profile 已冻结。

P0 退出前，仍须冻结实际启用 profile 的实体、SELECT、局部 WHERE、必要全局 RULE 和反向依赖闭包，不能把只改 `FILE_SCHEMA` 的同一几何文本当三种协议均已合格。AP203e1 的必要旧名称不能因合并字典不列而忽略。

## 2. 读表规则

- 表中参数按 **simple entity instance 的位置**排列，不是中立模型的字段顺序。`L` 是本实体显式字段，`I` 是继承的显式字段，`R` 是继承字段的显式重声明；只有注明 optional 的位置可用 `$`。
- `name` 在几何和拓扑类型中均来自 `representation_item`，属于必需字符串；空字符串与 `$` 不同。多重继承不额外重复一次 `name`。
- `label`／`identifier`／`text` 均保留其源类型和字符串原文；它们不是实体身份。实体身份来自文档及 `#id`，不能用重名标签合并。
- `LIST` 保持顺序及基数，`SET` 不授予顺序含义且不允许重复成员；不能靠排序 SET 决定外环或 shell。`UNIQUE` 的 OE 列表约束针对 OE 身份，不能据此合并共享同一底边的不同 use。
- `BOOLEAN` 只接受 `.T.`／`.F.`；`.U.` 是 LOGICAL 的未知值，不可替代 BOOLEAN。不能把未知或 `$` 转为 false。
- `dim`、`p`、`ne`、永久标识等**新增派生属性不新增 simple 参数槽**。但显式属性被重定义为派生属性时，原槽用 `*`。`ORIENTED_EDGE` 的两个端点是本切片的关键例子。
- 下面的“P0 限定”是产品能力范围，不能与源 schema 合法性混为一类；合法但超范围的表达应报 `Unsupported`。

## 3. 几何记录：6 类

| 类型、simple 参数顺序 | 字段类型、继承、基数 | P0 必需解释／限制 | 来源 |
|---|---|---|---|
| `CARTESIAN_POINT(name,coordinates)` | 1 I `label`；2 L `LIST[1:3]` 的 `length_measure` | 首切片只接受 3 个三维坐标；2D 并不因此全局非法。按其 representation context 的长度单位解释，拒绝非有限数 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_cartesian_point.html)；[RW 参数读取](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepGeom/RWStepGeom_RWCartesianPoint.cxx:32) |
| `DIRECTION(name,direction_ratios)` | 1 I `label`；2 L `LIST[2:3]` 的 `REAL`；至少一个分量非零 | 首切片要求 3 分量。比值无长度单位，不要求源已归一化；不能把所有分量随文件长度因子放大 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_direction.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepGeom/RWStepGeom_RWDirection.cxx:33) |
| `VECTOR(name,orientation,magnitude)` | 1 I `label`；2 L `direction`；3 L `length_measure`，本体约束为非负 | 方向比值先解释为单位方向，模长消费长度单位。作为本切片 LINE 的方向时须另验有效非零向量；不要伪称 EXPRESS 字段本身声明为正数 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_vector.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepGeom/RWStepGeom_RWVector.cxx:32) |
| `AXIS2_PLACEMENT_3D(name,location,axis,ref_direction)` | 1 I `label`；2 I `placement.location:cartesian_point`；3、4 L optional `direction`；派生 `p` 为 3 个方向，无附加参数 | location 和存在的方向必须为 3D；两方向同时存在须不平行。按 STEP 轴构造函数补合法缺省；不把位置当实例变换，不把非正交原始参考方向直接作为 x 轴 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_axis2_placement_3d.html)、[placement](https://www.steptools.com/docs/stp_aim/html/t_placement.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepGeom/RWStepGeom_RWAxis2Placement3d.cxx:33) |
| `LINE(name,pnt,dir)` | 1 I `label`；2 L `cartesian_point`；3 L `vector`；点与向量维数相同 | 这是无限定义直线，不是两个顶点之间的现成线段；区间由所用 EDGE_CURVE 的端点与 sense 确定，不能强置成 `[0,1]` 后丢失映射 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_line.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepGeom/RWStepGeom_RWLine.cxx:32) |
| `PLANE(name,position)` | 1 I `label`；2 I `elementary_surface.position:axis2_placement_3d`；本类型无新字段 | 保留无限支撑面与有限 face trim 的区别；UV 随轴构造，并按参数规则映射。源面法向还需与 FACE_SURFACE 的 same_sense 合成 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_plane.html)、[elementary_surface](https://www.steptools.com/docs/stp_aim/html/t_elementary_surface.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepGeom/RWStepGeom_RWPlane.cxx:31) |

基类 `point`／`curve`／`surface`／`solid_model` 在此处只参与类型推导，不额外提供 simple 字段；不能因为一条引用的静态类型是 `curve`，就把未知曲线子类解释成 LINE。[point](https://www.steptools.com/docs/stp_aim/html/t_point.html)、[curve](https://www.steptools.com/docs/stp_aim/html/t_curve.html)、[surface](https://www.steptools.com/docs/stp_aim/html/t_surface.html)、[solid_model](https://www.steptools.com/docs/stp_aim/html/t_solid_model.html)。

## 4. 拓扑和形状记录：12 类

| 类型、simple 参数顺序 | 字段类型、继承、基数 | P0 必需解释／限制 | 来源 |
|---|---|---|---|
| `VERTEX_POINT(name,vertex_geometry)` | 1 I `label`；2 L `point` | P0 窄到三维 CARTESIAN_POINT。不同 VERTEX_POINT 引同一点或同坐标仍是不同顶点，不能严格导入时焊接 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_vertex_point.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWVertexPoint.cxx:31) |
| `EDGE_CURVE(name,edge_start,edge_end,edge_geometry,same_sense)` | 1 I `label`；2、3 I `edge` 的 `vertex`；4 L `curve`；5 L `BOOLEAN` | P0 端点窄到 VERTEX_POINT，几何窄到 direct LINE。source edge 身份独立于 use；端点不等于 LINE.pnt。same_sense 只说明底边相对定义曲线的方向，不能吞并 ORIENTED_EDGE.orientation | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_edge_curve.html)、[edge](https://www.steptools.com/docs/stp_aim/html/t_edge.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWEdgeCurve.cxx:120) |
| `ORIENTED_EDGE(name,*,*,edge_element,orientation)` | 1 I `label`；2、3 原继承端点重声明为 derived，**必须为 `*`**；4 L `edge`；5 L `BOOLEAN` | 当前合并定义的 WHERE 禁止 edge_element 再指 ORIENTED_EDGE；P0 再窄到 EDGE_CURVE。按 orientation 推导起终点，不能读取两个 `*` 为缺失顶点 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_oriented_edge.html)；[RW CheckDerived／writer](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWOrientedEdge.cxx:31) |
| `EDGE_LOOP(name,edge_list)` | 1 I `label`；2 I `path.edge_list:LIST[1:?] OF UNIQUE oriented_edge`；derived `ne` 不占槽 | 列表逐 use 首尾同一顶点身份衔接，最后闭回起点；不靠坐标接近补接。不同 OE 可以共享底边；一面不能按 `(face,edge)` 去重 use | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_edge_loop.html)、[path](https://www.steptools.com/docs/stp_aim/html/t_path.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWEdgeLoop.cxx:35) |
| `FACE_BOUND(name,bound,orientation)` | 1 I `label`；2、3 L `loop`、`BOOLEAN` | P0 bound 窄到 EDGE_LOOP；orientation 控制该 loop 对此 face 的使用方向，不能当孔标志 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_face_bound.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWFaceBound.cxx:34) |
| `FACE_OUTER_BOUND(name,bound,orientation)` | 3 槽全部 I 自 FACE_BOUND，本类型无新字段 | 由类型声明外环角色；不是“列表第一个 bound”。同一个 face 不能有两个此类型 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_face_outer_bound.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWFaceOuterBound.cxx:31) |
| `FACE_SURFACE(name,bounds,face_geometry,same_sense)` | 1 I `label`；2 I `face.bounds:SET[1:?] OF face_bound`；3、4 L `surface`、`BOOLEAN` | P0 几何只取 PLANE；用于 surface-model 分支。保留面相对支撑面方向，检查区域／孔。当前定义禁止 oriented_surface 作其几何 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_face_surface.html)、[face](https://www.steptools.com/docs/stp_aim/html/t_face.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWFaceSurface.cxx:35) |
| `ADVANCED_FACE(name,bounds,face_geometry,same_sense)` | 1、2、4 I；merged 中 3 R 为 `advanced_face_geometry_select`，仍是显式值，**不是 `*`**；AP214e3 文件以 WHERE 限制，无该重声明；都没有第 5 个槽 | P0 几何仍窄到 PLANE；精确 solid 分支要求此类。按具体文件检查其边、顶点、bound 等 WHERE，不能强求旧 schema 存在新 SELECT | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_advanced_face.html)、[当前 SELECT](https://www.steptools.com/docs/stp_aim/html/t_advanced_face_geometry_select.html)、[AP214 差异](p0-ap214-provenance.md)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWAdvancedFace.cxx:35) |
| `OPEN_SHELL(name,cfs_faces)` | 1 I `label`；2 I `connected_face_set.cfs_faces:SET[1:?] OF face` | 保留开放类型；检查连接性、共享边和实际边界。不能为满足 solid 资格擅自缝合／封闭 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_open_shell.html)、[基类](https://www.steptools.com/docs/stp_aim/html/t_connected_face_set.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWOpenShell.cxx:31) |
| `CLOSED_SHELL(name,cfs_faces)` | 同上，全部 I；本类型无 closed 布尔槽 | closed 声明必须与边使用、定向、区域等几何拓扑证据一致。仅外形封闭或带此类型不自动授予 material solid 角色 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_closed_shell.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWClosedShell.cxx:34) |
| `MANIFOLD_SOLID_BREP(name,outer)` | 1 I `label`；2 L `closed_shell` | P0 无空腔、一个外壳。OCCT RW 的 C++ 读取参数用较宽 ConnectedFaceSet，不能据此把源 outer 放宽到 OPEN_SHELL | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_manifold_solid_brep.html)；[RW 的较宽读取](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWManifoldSolidBrep.cxx:47) |
| `SHELL_BASED_SURFACE_MODEL(name,sbsm_boundary)` | 1 I `label`；2 L `SET[1:?] OF shell` SELECT；有几何约束函数 | P0 支持 OPEN_SHELL，另以 CLOSED_SHELL 表达表面模型的控制用例，保持 surface-model 类型。不能把 closed boundary 改成 MANIFOLD_SOLID_BREP；其他 shell SELECT 分支不支持 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_shell_based_surface_model.html)、[shell SELECT](https://www.steptools.com/docs/stp_aim/html/t_shell.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWShellBasedSurfaceModel.cxx:33) |

`FACE` 的当前合并 WHERE 只规定显式外环数量至多为一，不规定必有一个。为首切片明确要求恰好一个 FACE_OUTER_BOUND，可以减少首轮区域判定歧义；**没有显式外环但按相应 schema 合法的模型属于本切片不支持，不能标作 schema 非法**。孔用其他 FACE_BOUND 表达，其物理角色由包含关系和方向共同验证，不能用源 SET 下标代替。

本切片仍须验证非自交、无未声明重叠／非流形、面区域非零、端点在线且与同一 edge 的所有使用一致、共享边两侧一致等几何条件；只检查 EXPRESS 布局和内核 validator 不足以授予资格。它们是产品验收条件，不是声称已完整实现 STEP 全部 formal proposition。

## 5. 表示记录：4 类

| 类型、simple 参数顺序 | 显式字段与角色 | P0 约束 | 来源 |
|---|---|---|---|
| `REPRESENTATION(name,items,context_of_items)` | 1 L `label`；2 L `SET[1:?] OF representation_item`；3 L `representation_context` | 这是基类结构知识，不能仅凭 items 里出现 solid 就忽略具体表示类型；派生 id／description 不新增槽 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_representation.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepRepr/RWStepRepr_RWRepresentation.cxx:33) |
| `SHAPE_REPRESENTATION(name,items,context_of_items)` | 3 槽全部 I；本类型无新字段 | 识别其类型与关联，不把它普遍视为任意几何容器。首轮 canonical 根用下面两个具体 subtype；若文件依赖 generic 外壳＋未支持 relationship，明确不支持该路径 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_shape_representation.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWShapeRepresentation.cxx:34) |
| `ADVANCED_BREP_SHAPE_REPRESENTATION(name,items,context_of_items)` | 1、3 I；2 R 为当前 `SET[1:?]` 的专用 SELECT | 必须有 MANIFOLD_SOLID_BREP 或 MAPPED_ITEM；P0 只转换前者。允许合法 AXIS2_PLACEMENT_3D 辅助 item 并保留；它不自动变成零件变换。所有 solid 壳面须为 ADVANCED_FACE；oriented outer 及 void 受额外 WHERE 限制 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_advanced_brep_shape_representation.html)、[当前 SELECT](https://www.steptools.com/docs/stp_aim/html/t_advanced_brep_shape_representation_items_select.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWAdvancedBrepShapeRepresentation.cxx:35) |
| `MANIFOLD_SURFACE_SHAPE_REPRESENTATION(name,items,context_of_items)` | 1、3 I；2 R 为当前 `SET[1:?]` 的专用 SELECT | 必须有 SBSM 或 MAPPED_ITEM；P0 只转换 SBSM，允许合法 AXIS2_PLACEMENT_3D 辅助 item。SBSM 可用 OPEN_SHELL 或 CLOSED_SHELL；面至少为 FACE_SURFACE 子类；其余 WHERE 也按版次处理 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_manifold_surface_shape_representation.html)、[当前 SELECT](https://www.steptools.com/docs/stp_aim/html/t_manifold_surface_shape_representation_select.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWManifoldSurfaceShapeRepresentation.cxx:35) |

`ADVANCED_FACE`／两种具体 representation 的专用 SELECT 是当前合并定义的重声明，其中有 AP 标记；较旧 AP 可能把等效限制写在 WHERE 中。实现前应固定对应版次的“显式类型＋WHERE”，不能盲目要求旧 AP 字典也定义同名 SELECT。

## 6. 产品和 shape 关联记录：10 类

这些字段不是装饰性可忽略内容：它们决定所选零件、revision、shape 和 application context 如何关联。首切片不实现装配，但应有完整单零件根链，避免之后再改变选根契约。

| 类型、simple 参数顺序 | 字段类型、继承、optional | P0 角色与检查 | 来源 |
|---|---|---|---|
| `APPLICATION_CONTEXT(application)` | 1 L `label`；派生 description／id 不占槽；inverse context_elements 不序列化 | 保留应用说明；与产品上下文、协议记录关联，不把这个字符串当 FILE_SCHEMA 的替代品 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_application_context.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWApplicationContext.cxx:30) |
| `APPLICATION_PROTOCOL_DEFINITION(status,application_interpreted_model_schema_name,application_protocol_year,application)` | 1、2 L `label`；3 L `year_number`；4 L `application_context`。merged 年份有 >1581 WHERE；AP214e3/AP203e2 所取得文件只是 INTEGER，不能套新限制 | 四字段均必需。索引反向记录，按具体 RULE 的常量与量词检查；APD 字符串不总等于 HEADER 名，不能强制每 AC 恰一个作为通用 AP 规则。年份不是 edition 真值 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_application_protocol_definition.html)、[merged year_number](https://www.steptools.com/docs/stp_aim/html/t_year_number.html)、[profile 差异](p0-schema-profiles.md)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWApplicationProtocolDefinition.cxx:33) |
| `PRODUCT_CONTEXT(name,frame_of_reference,discipline_type)` | 1、2 I 自 application_context_element：`label`、`application_context`；3 L `label` | 产品业务上下文；名称和 discipline 原样保留，不根据文本猜几何单位或坐标系 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product_context.html)、[基类](https://www.steptools.com/docs/stp_aim/html/t_application_context_element.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWProductContext.cxx:31) |
| `PRODUCT_DEFINITION_CONTEXT(name,frame_of_reference,life_cycle_stage)` | 1、2 I 同上；3 L `label` | definition 所属生命周期上下文；不可与 representation context 混用 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product_definition_context.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWProductDefinitionContext.cxx:32) |
| `PRODUCT(id,name,description,frame_of_reference)` | 1、2 L `identifier`、`label`；3 L optional `text`；4 L `SET[1:?] OF product_context` | 保留产品身份标签和所有上下文；标签重名不等于同一产品，空 context SET 非法 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWProduct.cxx:34) |
| `PRODUCT_DEFINITION_FORMATION(id,description,of_product)` | 1 L `identifier`；2 L optional `text`；3 L `product`；当前 UNIQUE 是 id 与 of_product 的组合 | 保留 formation／revision 层，不能跳过后把不同版本的 shape 合并 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product_definition_formation.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWProductDefinitionFormation.cxx:34) |
| `PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE(id,description,of_product,make_or_buy)` | 前 3 槽 I，4 L `source` 枚举（`.MADE.`／`.BOUGHT.`／`.NOT_KNOWN.`）；description 继承 optional | 常见旧 AP203 表达；采购／自制声明与 shape 无等价关系，保留其语义。不能照搬某 reader 对 inherited description 的过强必需检查 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product_definition_formation_with_specified_source.html)、[枚举](https://www.steptools.com/docs/stp_aim/html/t_source.html)；[RW 参数序列](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWProductDefinitionFormationWithSpecifiedSource.cxx:37) |
| `PRODUCT_DEFINITION(id,description,formation,frame_of_reference)` | 1 L `identifier`；2 L optional `text`；3 L `product_definition_formation`；4 L `product_definition_context`；derived name 无附加槽 | definition 才是本切片关联 shape 的零件定义节点；第 2 字段不是 product name | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product_definition.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWProductDefinition.cxx:35) |
| `PRODUCT_DEFINITION_SHAPE(name,description,definition)` | 3 槽 I 自 property_definition：`label`、optional `text`、`characterized_definition` SELECT；有自己的 WHERE／UNIQUE | P0 definition 只选 PRODUCT_DEFINITION 分支，保留引用型别；不能接受任意 #record。当前 UNIQUE 约束其 definition；不能用 name 判 shape 身份 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product_definition_shape.html)、[property_definition](https://www.steptools.com/docs/stp_aim/html/t_property_definition.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepRepr/RWStepRepr_RWProductDefinitionShape.cxx:37) |
| `SHAPE_DEFINITION_REPRESENTATION(definition,used_representation)` | 两槽 R 自 property_definition_representation；当前分别窄到 `property_definition` 与 `shape_representation`；没有 name 槽 | P0 definition 取 PRODUCT_DEFINITION_SHAPE，used_representation 取两种已列具体表示，完整建立两方向索引。不能把裸 representation 误接到 shape-definition 链 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_shape_definition_representation.html)、[基类](https://www.steptools.com/docs/stp_aim/html/t_property_definition_representation.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepShape/RWStepShape_RWShapeDefinitionRepresentation.cxx:40) |

完整关联方向为：`PRODUCT_DEFINITION → formation → PRODUCT → product_context → application_context`；definition 另指向 definition_context；`PRODUCT_DEFINITION_SHAPE.definition → PRODUCT_DEFINITION`；`SHAPE_DEFINITION_REPRESENTATION.definition → PRODUCT_DEFINITION_SHAPE`，且 used_representation 指向 shape。协议定义、PDS、SDR 经常需要反向查询才能从所选 definition 得到，不得仅沿几何正向引用拼闭包。

## 7. 上下文、历史别名和分类元数据：6 类

| 类型、simple 参数顺序 | 核对结果／P0 规则 | 来源 |
|---|---|---|
| `REPRESENTATION_CONTEXT(context_identifier,context_type)` | 两槽均 L，分别 identifier、text；inverse representations_in_context 不占槽。它本身没有单位、维数或 application_context 指针 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_representation_context.html) |
| `GEOMETRIC_REPRESENTATION_CONTEXT(context_identifier,context_type,coordinate_space_dimension)` | 前 2 槽 I；3 L `dimension_count`。当前基类型只要求正 INTEGER，P0 明确要求 3；不能宣称 EXPRESS 将其限制为 1—3。常见文件把它与单位及 uncertainty context 组合成 complex instance，不能照 simple 表把父字段重复消费 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_geometric_representation_context.html)、[dimension_count](https://www.steptools.com/docs/stp_aim/html/t_dimension_count.html) |
| `MECHANICAL_CONTEXT(name,frame_of_reference,discipline_type)` | AP203 历史 subtype。供应商库将其语义映射到 PRODUCT_CONTEXT，但原始文档必须保留原类型；固定 OCCT 三参数 reader 及类型继承与该对应一致。具体 edition 约束仍需原 schema | [AP203 历史类型说明](https://www.steptools.com/docs/stp_aim/notes_ap203.html)；[RW](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepBasic/RWStepBasic_RWMechanicalContext.cxx:35) |
| `DESIGN_CONTEXT(name,frame_of_reference,life_cycle_stage)` | AP203 历史 subtype。对应 PRODUCT_DEFINITION_CONTEXT，三参数；固定 OCCT 没有单独 RWDesignContext 文件，dispatch 复用该基类 reader。不得因为 modern merged 字典找不到独立页就当无关未知字段跳过 | [AP203 说明](https://www.steptools.com/docs/stp_aim/notes_ap203.html)；[OCCT dispatch](E:/Coding/OCCT/src/DataExchange/TKDESTEP/RWStepAP214/RWStepAP214_ReadWriteModule.cxx:8784)、[类型继承](E:/Coding/OCCT/src/DataExchange/TKDESTEP/StepBasic/StepBasic_DesignContext.hxx:26) |
| `PRODUCT_CATEGORY(name,description)` | 两槽 L：label、optional text；derived id 不占槽。按 edition 识别并保留分类信息，不用分类字符串改变几何类型／单位 | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product_category.html) |
| `PRODUCT_RELATED_PRODUCT_CATEGORY(name,description,products)` | 前 2 槽 I；3 L `SET[1:?] OF product`。记录产品分类的反向关联；不是面几何依赖，仍可能与所用 AP 的全局规则有关。不能把所有 product 关系一概叫无关 metadata | [EXPRESS](https://www.steptools.com/docs/stp_aim/html/t_product_related_product_category.html) |

两种历史 context 的字段适用性是“供应商文档＋固定 reader 的交叉证据”，不是已取得 AP203 e1 原文。更早的 `ADVANCED_BREP_REPRESENTATION` 与现代名字只差几个字，但参数布局还缺 name，不属于可静默重命名的别名；首切片明确不支持。[AP203 说明](https://www.steptools.com/docs/stp_aim/notes_ap203.html)。

### 7.1 取得 longform 后的覆盖规则

AP203e1 修改副本中的 PRODUCT、formation、PRODUCT_DEFINITION、PROPERTY_DEFINITION（含PDS继承）及 uncertainty 的 description 是 required，不能按第6节 merged 表统一接受 `$`；同副本 PRODUCT.id 有 UNIQUE，仍不能把名字/标签当实体身份。其 PRODUCT_CONTEXT、设计 property 对应 context、formation 的 subtype 另有全局限制。AP203e2 文件仍保留两个历史 context，不能见 deprecated 标记就丢弃。详见 [AP203 来源和差异](p0-ap203-provenance.md)。

AP214e3 的 product category 和 id-owner 等 RULE、AP242 各版 APD/shape 规则进一步表明：**38类是首片主图清单，不是完整输入支持闭包**。要新增的管理记录必须有属性/SELECT/规则清单及影响分类；在闭包完成前，不把它们全部声明为可忽略 metadata，也不以本表声称完整 AP 符合。

## 8. complex、派生字段与全局约束的门槛

1. **38 类不是完整文件 schema。** common context 会引出 SI／conversion unit、measure、dimensional exponents、uncertainty 等实体。HEADER 与所有 DATA 的结构也应完整处理；其合法 Part 21 版次与 STEP AP edition 是两个维度。
2. 合法 complex instance 按 constituent 的局部属性解释，不能把 simple 展平字段表直接套给每个 constituent；额外且有影响的 constituent 不可丢弃。单位的 `NAMED_UNIT` dimensions 等显式转派生槽是另一组必须验证的 `*`；详见单位专题材料。
3. 几何 item 的 `dim` 来自 geometric_representation_item，拓扑永久 ID 来自 topological_representation_item；两者是 derived。representation_item 的 name 是显式。新派生字段不“补写 `$`”；继承端点转 derived 的 OE 仍须两个 `*`。[几何基类](https://www.steptools.com/docs/stp_aim/html/t_geometric_representation_item.html)、[拓扑基类](https://www.steptools.com/docs/stp_aim/html/t_topological_representation_item.html)、[表示 item](https://www.steptools.com/docs/stp_aim/html/t_representation_item.html)。
4. 数字维数、聚集基数、SELECT 分支、UNIQUE、局部 WHERE 与必要全局 RULE 都可能决定完整性。首切片不能仅用字段数量或某 RW 的“读成功”决定接受。STEP Tools 自身也把[非 optional、局部与全局规则检查](https://steptools.com/docs/devtools/ap2xxcheck.html)区分出来。
5. 合并字典的 AP242 [协议定义要求](https://www.steptools.com/docs/stp_aim/html/t_ap242_application_protocol_definition_required.html)与 [shape subtype 要求](https://www.steptools.com/docs/stp_aim/html/t_ap242_subtype_mandatory_shape_representation.html)明确展示了全局检查的存在；其中 schema 名已替换为合并库名称，不能把它的字符串常量直接当原 AP242 文件正确值。generic SHAPE_REPRESENTATION 并非普遍非法，也不普遍能承载随意 payload；需要按原版规则和角色检查。
6. 单位全局要求不因 P0 没有角度几何就消失。canonical fixtures 宜明确给出长度、平面角、立体角及 uncertainty；是否某成员严格必需，须从对应 edition 的 RULE 核对，不凭“本切片没有消费该量”推导可缺。

## 9. 建议首切片的能力包络与反例

**接受候选**：一条无装配的完整产品链，直接关联具体 ABSR／MSSR；合法的三维几何＋单位 context；PLANE 面，direct LINE 定义边、显式 VERTEX_POINT、ORIENTED_EDGE、EDGE_LOOP 与明确外环／孔；开放片和无空腔闭合盒体。源 `same_sense`、use orientation、bound orientation 的非缺省组合必须进入正确性用例，不能只验证全 `.T.` 样件。合法辅助 placement item 保留，不能当 assembly placement 应用一次。

建议首轮固定三种形状角色：单平面矩形／带矩形孔面经 SBSM 保持开放；六面盒体经 MANIFOLD_SOLID_BREP 具有材料实体角色；同一六面 CLOSED_SHELL 经 SBSM 仍是表面模型，作为“封闭但不是 solid”控制。三者使用不同的类型与资格断言；不能只用 volume 一项代替角色。

| 情况 | 首切片行为 | 不能做的降级 |
|---|---|---|
| 合法 `SURFACE_CURVE`／`PCURVE`／`SEAM_CURVE`，即使其中 curve_3d 恰好是 LINE | 若是所选内容必需，`Unsupported`，保留全部原始表示及来源 | 只取三维 LINE 后丢 associated/master |
| `TRIMMED_CURVE`，CIRCLE／NURBS／其他非 direct LINE；非 PLANE 支撑面 | 必需依赖 `Unsupported` | 用端点折线／面拟合顶替定义几何 |
| `ORIENTED_FACE`、oriented shell、`BREP_WITH_VOIDS`、VERTEX_LOOP／退化边、POLY_LOOP／FACETED_BREP | 必需依赖 `Unsupported` | 忽略反向层、空腔或零长度边，改外壳闭合性 |
| MAPPED_ITEM／representation map、definition relationship／NAUO、representation relationship／变换、外部引用 | 首切片不授予装配／该包装路径能力；明确具体缺失路径 | 挑一个底层 solid、丢 placement 或静默摊平 |
| valid face 没显式外环且首切片无判定能力 | `Unsupported` 或范围无法判定的拒绝，不能伪报 schema 字段错误 | 按 bounds SET 第一个强定外环 |
| outer 明确指 OPEN_SHELL、必需字段缺失、重复 SET 成员、OE derived 槽错用 `$`／引用 | 源结构／类型检查拒绝，并指向字段 | 借 reader 较宽 C++ 类型接受，或默认补值 |
| source IDs 不同、坐标相同但未共享的顶点／边导致 shell 缺接 | 诊断原拓扑并严格拒绝相应资格；修复阶段另行处理 | 按坐标 hash 自动焊接后声称严格成功 |
| 存在一个合法 solid 与一个不支持的必需 item／根 | 所选请求全成全败，返回完整原因 | 发布其中一个并把另一项变 warning |
| 只包含 axis placement、没有所需 solid／SBSM 的具体 representation | 依据其必需 item 规则拒绝 | 当空模型成功 |
| 非必需的已知分类元数据 | 在来源与诊断中保留，并执行适用全局规则；几何转换资格单独评价 | 原始记录删掉后称无损；或按分类名字推导 shape |
| 未知附加类型、未知 complex constituent 或关系的影响不能证明 | 保留原文，影响不明则拒绝 | 仅因不被 face 正向引用就当无关 |

## 10. 仍待核对的具体事项

- 已取得文件与实际 FILE_SCHEMA／OID registry 的映射、发布状态和修订差异仍须冻结；本报告不能把 merged 差异注释或带 DIS/TS 自述的实现文件扩大成全部 ISO 版次原件。
- 明确支持哪些 legal complex 组合，不把任意组合实例都当允许；验证 constituent 排列、继承、derived 槽以及额外 subtype 不丢失。首轮样件可集中为 standard unit/context complex，不等于其他合法组合已支持。
- 核对所有必需全局规则，包括产品分类／application protocol／shape subtype／单位聚集等；在宣称“源符合某 AP”前解决。导入几何资格与完整 AP 符合性声明要分别记录。
- 原始点／方向／轴向量、LINE 参数、PLANE UV 与 SMLib 目标映射必须由另一份参数契约和独立真值封闭验证；本字段表不证明任何映射已正确实现。
- 处理一 definition 多形状／多 SDR、多 protocol declaration、共享几何跨 context 及无法唯一选根：首切片如未提供完整判定，须明确范围拒绝，不能猜“首个”。
- ID_ATTRIBUTE／NAME_ATTRIBUTE 等身份附加材料，以及其他 common metadata，仅在对应版次与影响分类证明后扩展白名单；本轮没有把任意 metadata 宣布无影响，也没有枚举完整 AP 管理信息。
- 本轮不决定生产默认数值、不运行私有模型、不生成 parser／fixture／测试代码。后续用例应分别证明字段结构、方向链、区域、单位、表示角色与完整来源，而非以某个内核成功返回替代全部验收。

## 11. 首次旧地址获取记录（历史）

2026-10-15，根据官方 [MBX-IF supplementary geometry 推荐实践](https://www.mbx-if.org/home/wp-content/uploads/2024/05/rec_prac_suppl_geo_v12a.pdf)提供的旧下载线索，尝试了以下 **3 个官方 URL，每次超时上限 15 秒**。三个请求均在超时内返回 HTTP 404；因此没有取得可验证的 ZIP／EXPRESS 原文，也没有进行 ZIP 提取、原 schema 字段比对或全局 RULE 认证。

| 请求 | 结果 | 本轮能支持的结论 |
|---|---|---|
| [AP214 e3 ZIP，HTTPS](https://www.cax-if.de/documents/AP214E3_2010.zip) | HTTP 404 | 该地址本次不可取；不能据此判断原发布内容 |
| [AP214 e3 ZIP，原 HTTP 地址](http://www.cax-if.de/documents/AP214E3_2010.zip) | HTTP 404 | 改用原协议也未取得 source |
| [AP242 e2 MIM LF v1.101](https://www.cax-if.de/documents/ap242ed2_mim_lf_v1.101.exp) | HTTP 404 | 未取得该版 source；不能用 merged 字典补称原文 |

获取记录保存于本机私有工作目录 [retrieval-attempts.json](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/p0-schemas/retrieval-attempts.json)。该 JSON 是失败尝试的审计记录，**不是 schema 内容证据**；仅在首次尝试结束时取得数为0。随后已找到迁移后的当前入口并取得文件、hash和逐版差异，见[profile 附件](p0-schema-profiles.md)及三份来源报告。旧404记录保留作历史，不能继续代表当前来源状态。未从商业安装包提取或使用不明镜像。

