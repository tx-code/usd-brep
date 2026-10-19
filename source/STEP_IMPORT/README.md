# STEP_IMPORT（内部 C++20）

源层可在显式通用范围中保留 BRep 与独立关联线框，具体图、角色与必要规则见
[源检查合同](../STEP_SEMANTICS/README.md)。原生构建尚未实现该线框的完整对象、查询和存档路径；
遇到 `wireframe_representation` 时，在任何原生调用前返回
`GeneralLinkedWireframeUnsupported`，不发布只含 BRep 的部分模型。
类型识别或源检查成功不授予这种组合的原生导入资格。

通用开发请求可显式设置 `Options.schemaInterpretation=UnregisteredAp214As2010`，
并同时选择通用质量与非几何互操作策略。它只为未登记的八段 AP214 标识选择固定源解释，
具体范围见[源检查合同](../STEP_SEMANTICS/README.md)。默认路径继续拒绝该标识；
不删除 OID 段、不改 HEADER、不放宽必需内容、单位、几何、容差或发布检查。

完成全部检查的结果为 `AcceptedUnderInterpretation`，与普通 `Accepted` 分开；
报告明确 `source_binding_verified=false` 并保留原绑定失败诊断。调用者可从
`model->Source()->AppliedInterpretation()` 和 owning 面视图的
`AppliedSourceInterpretation()` 查询这一假设，丢弃原 outcome/report 不会丢失它。
独立建模副本与原生存档不继承源验收资格；保存原报告才能保留此次导入的来源说明。
失败、预算和取消继续释放未发布候选，完整模型与报告仍经同一个提交点交付。

原生 CLI 使用 `--interpret-unregistered-ap214-2010`，并需显式通用质量与
`--interop-nongeometric`。成功交付解释模型时返回 **11**；普通成功仍为 **0**。
这类模型需由调用者单列，不自动进入普通验收数量或性能成功组。

显式通用路径还支持单个裸 `CONFIG_CONTROL_DESIGN` HEADER 的
`203-N916-Source-0.1` 源子集。固定依据是修改版 N916/AP203 amendment 1，
原始 HEADER 仍为 `NameOnly`，报告标记未证明具体版本；不改写为 N2635。
源类型／单位规则及外来图层的受限互操作证明见
[源检查合同](../STEP_SEMANTICS/README.md)。严格源策略仍拒绝外来图层和缺失 APD；
显式互操作可在完成归属与非物理依赖证明后保留原诊断，不能据此补造协议声明或几何。
原生构建继续执行相同质量配置、完整候选检查和原子发布；不授予完整 AP203 符合性。

另一个独立选项 `Options.dataStringPolicy=PreserveOpaqueBytes`／CLI
`--preserve-opaque-data-strings` 可保留未知编码的 DATA 文本；同样必须显式选择通用质量与
`SourcePolicy::PreserveNonGeometric`，不会随旧文件头选项或互操作模式自动启用。
当前仅允许已证明属于所选零件的 `PRODUCT.id/name`，
以及已验证全部目标归属的 `PRESENTATION_LAYER_ASSIGNMENT.name`。
这些值始终是 `OpaqueString`，文本不可得，原始 token、身份、跨度与诊断留在只读源快照和报告中；
不猜编码、不转码、不改名、不把未知文本当成空名称。其他字段或未证明的用途明确不支持。
`REPRESENTATION.name` 还参与现有用途判断，文字不可得时继续不支持，不以空字符串绕过。
它不改变 schema 绑定、几何、单位、容差、完整报告／模型的发布和取消清理。
旧 AP203 等未绑定协议不会因此自动获得导入资格。

显式通用互操作请求可另选 `Options.headerNamePolicy=PreserveOpaqueBytes`，仅保留
文件头 `FILE_NAME.name` 的非标准原始字节。该字段是 `OpaqueString`，不生成解码后的
名称，报告包含原 token、跨度及 `OpaqueHeaderFileName` 提示；不转换原始文件。
DATA、其他文件头字段、协议绑定、类型、单位和几何检查均不放宽。单选质量配置或
互操作策略不会启用此入口；严格／P0 请求不接受这一载体选项。原生 CLI 对应
`--preserve-opaque-header-name --interop-nongeometric`，并须选择通用质量。

新增显式的通用开发路径 `Options.generalBrep=true`，用于精确绑定的 AP214e3／AP203e2 单个普通
`MANIFOLD_SOLID_BREP` 的 LINE、CIRCLE、ELLIPSE、显式节点 B 样条边，
PLANE、CYLINDRICAL_SURFACE、CONICAL_SURFACE、标准环形 TOROIDAL_SURFACE 和显式节点 B 样条曲面。
样条包括已登记的有理复合实体映射；控制点采用欧氏坐标，权重为独立的无量纲参数。
单层 `TRIMMED_CURVE` 可使用 CIRCLE／ELLIPSE 基础曲线，保留 wrapper 与 basis 的不同身份，
并按 ISO 10303-42:2003 §4.4.40 组合裁剪 sense、EDGE_CURVE.same_sense 和有向使用。
参数裁剪使用已建立的 SI radian／前缀比例；坐标裁剪按源解析位置架反求角度。
两种表示同时存在时都检查物理一致性，master 选择主表示；UNSPECIFIED 且两者存在时选参数。
原数值、单位和源裁剪始终保留，不以拓扑端点替代源裁剪范围。
边可使用源裁剪的一段；封闭边须证明源裁剪为完整一周，完整周期可在源顶点处等价换缝。
`TrimmedConicSourceDomain` 记录 `source_geometry`、`source_basis_geometry`、经方向选支的角域、方向、
canonical 裁剪参数域、边角度分支及原生角域的整周平移；原参数到 radian 的已建立比例单独记录，
原生 NURBS 参数不冒充角度。端点反求在舍入量内贴近裁剪界时取原裁剪界，之后仍须满足毫米端点误差预算。
当前开发范围只解释绝对值不超过 4π rad 的参数及至多一周的非零有向范围；
仅以 binary64 转换舍入量识别整周，不以一致性容差补成整圆。
嵌套／其他基础曲线、未建立的参数单位、歧义或多周范围明确不支持。
已有 UV／缝检查仍适用，不因裁剪曲线可构建就授予任意周期曲面裁剪资格。
源权重原词形始终保留，转入内核前检查数值可表示性，构建后读回齐次控制网验证。
仅当源权重词形全部相同、可证明共同因子精确消去时，允许内核改存单位权重，
并记录 `ExactUniformWeightCancellation`；非均匀权重被内核近似简化时拒绝。
这不保证所有有理参数化都满足目标内核的 UV／容差检查；失败仍丢弃整个候选。
仍保留下面原有平面路径的行为和资格；默认选项不进入通用路径。

AP203e2 的通用请求使用独立的 `203e2-G0-Source-0.1` 源子集，保留 N2635 旧上下文、
显式转换单位量纲、属性与管理声明；管理挂接须证明目标已属于所选零件。
旧审批 SELECT 冲突只能在显式互操作模式下，经所选 PD／formation 和非物理用途证明后，
连同原始诊断保留。完整管理 WHERE／global RULE 尚未执行。开发质量配置的历史名称
`Q-AP214-*` 不决定源协议，实际源身份和源检查范围由报告中的 schema/profile 单独记录。

通用路径先检查类型化源图、所选实体角色、物理单位和源共享拓扑，然后直接构建私有
SMLib 候选。以源 EC 身份共享边，保留所有源顶点和原定义；不调用缝合或修复器。
闭合圆／椭圆边及整周圆柱／圆锥支撑的原生角域先转换起角，再加 360°；
避免分别转换两端时，一周跨度因舍入略大于内核允许范围。该计算角域不改写源参数、
几何或拓扑，部分角域保持原范围；后续几何、UV、容差及完整发布检查仍须通过。
两圆环完整圆柱可添加一条使用两次的精确参数缝。未标外环的平面、圆柱和圆锥必须在
UV 安装与误差检查后，经原生曲线相交/包含分类证明唯一外区、闭合、方向和嵌套关系；
圆柱/圆锥还须没有未解决的缝、极点或未配对零面积环问题。其他支撑继续 Deferred。
周期切开所得外环不把任何源圆环改称 FACE_OUTER_BOUND；分类的规范化记录保留全部
相关源 bound。螺旋带可以使用严格位于同一圆柱支撑上的有理参数图，源曲线/顶点不改写。
这些动作记录源身份，不能把 surface-model 升级为材料实体。报告的 `parameter_mappings`
保存曲线参数区间、圆弧的角度分支和有理求值关系、样条反求的 trim 参数，以及螺旋圆柱图
的起角、螺距、下界、各段宽度、坐标架和原生面关联；源参数不被覆盖。

`Quality::GeneralDevelopment()` 为明确选择的 1 μm 一致性/UV/工作容差开发配置；
`Quality::GeneralToleranceStudy()` 是独立的 10 μm 研究配置。两者都不允许容差增长，
不由源 uncertainty 自动选值，也不是生产默认。源错误必须按显式
`SourcePolicy::PreserveNonGeometric` 的已证明非几何白名单保留；严格源模式不变。

通用报告 `GeneralBrepDevelopment` 只授予声明的源检查、原生构建和量测后验检查。
它不是完整 AP214 conformance，也不继承平面路径的全段/全域区间证明。
曲面投影误差是内核量测；UV 安装完成后另按边/UV 的联合节点分段，每段八等分，
比较同参数的定义曲线点和最终 UV 抬升点并执行 UV 预算。此采样检查包含端点，不能代替连续误差界。
面积/体积以原生相对精度 1e-4 计算；尚未建立全局定义误差、
全局曲线—曲面误差或面积/体积绝对误差界。报告明确标记这些未建立的资格。
质量积分以包围盒中点为参考原点，改善矩量计算的数值条件；报告记录
`mass_integration_origin=AxisAlignedBoundsMidpoint`。这不改变定义几何、工作容差或请求精度，
也不把请求相对精度当作已证明的绝对误差界。
通用材料实体将模型面积/体积/体积形心与各面面积/面积形心合并为一次逐面积分，
只请求这些量所需的矩量组。体积矩保持模型包围盒中点参考系，面积矩直接在各面局部
包围盒中点参考系计算，避免事后平移矩量造成消减。积分面积尺度取原模型与原面尺度
中较小者，保留相对精度 1e-4、误差控制和重试策略；有限精度结果可有末位数值变化。
每个融合面查询前仍检查取消与资源限额，模型量和全部逐面结果完成后才生成原有完整报告；
失败或取消不发布部分结果。此复用不改变源检查、LEVEL2 校验、Repair=None 或报告资格。

圆环面首版构建要求有限可表示的 `major_radius > minor_radius > 0`，保留源位置架和已有边／缝。
半径还须大于内核 `SM_EFF_ZERO`；超出此原生数值范围时报告
`GeneralToroidalKernelRadiusRange`，不进入不安全的直接构造路径。
源 `TOROIDAL_SURFACE` 仅要求两个正长度；角形／自交的正半径源表示仍可通过源检查，
其原生构建明确报 `GeneralToroidalRingRequired`，不会移植 CSG `TORUS` 的规则把它判成非法源。
目标解析角域以度表示，原生 NURBS UV 与之不是线性关系；不声称等同于源参数。
未标外环的圆环面继续 Deferred，跨周期缝等情况仍须通过既有 UV／有效性检查才能发布。

返回模型是 owning 只读快照，`FaceView::ParameterDomain()` 返回实际原生求值域，
不承诺它是源 UV 或以毫米计。`WriteNative(path)` 保存不拟合解析定义的二进制原生
`.smb`，`CopyForModeling()` 生成独立 context/BRep；报告对应原快照。

以下说明专指既有已资格的平面路径。


STEP→SMLib 内部导入：单零件、canonical 毫米；源长度可SI前缀或已验证conversion链，
几何为开放 LINE/PLANE 平面及孔、两个可带孔平面区域组成的无重叠共享支撑边面片，或经过独立证明的矩形盒体边界，
基于 STEP_SOURCE、STEP_SEMANTICS 和 STEP_PLANAR 的自研读取与源检查。没有 OCCT 读取依赖。

`ImportFile` / `ImportBytes` 显式接收 `Options`。先保存不可变原始文档，完成类型、单位、规则与区域检查，
再建立不含 SMLib 指针的中立计划。候选 context、BRep 与尚未移交的几何分别有所有者。
完成 UV、指针/LEVEL2 验证、独立后验检查和必需报告后，才发布 `Accepted` 与模型。

平面与通用路径共用内部 `Publication`，集中持有构建后尚未发布的候选模型，负责快照准备、
报告生成、失败释放与最终移交。`Execution` 仍负责贯穿导入的检查及唯一取消／成功裁决。
发布保留两次报告序列化：首次确认报告可生成，后一次记录最终检查点观测；提交成功后只移交
模型所有者，不再分配、回调或调用内核。失败先释放候选再生成失败报告，报告预算不足不覆盖取消终态。

```cpp
#include "StepImport.h"

auto result = step_import::ImportFile(path, step_import::Options::PlanarFixture());
if (result.model) {
    auto face = result.model->Face();
    result.model.reset();
    auto closest = face.Closest({4.2, 9.6, 8.0}); // owning view 保留模型及 context
}
// result.report 独立拥有源材料、诊断、实际容差及规范化记录。
```

`PlanarFixture()` 是明确选择的合成样件配置 Q-P0-mm-01，不是生产默认值。
该名称的数值不可改写；其他配置必须另取 ID。无效 options 抛出 `invalid_argument`。
定义曲线和支撑面不拟合；源顶点、递增 LINE 参数、四层方向各自保留。
目标曲线重参数化到 [0,1]，源根式参数映射留在报告，UV 在拓扑构建后装到 SAME member。
保留各源 bound/loop 与每次 ORIENTED_EDGE 使用的有向顶点、实际 UV、SAME owner 和所在侧。
内核首环必须为外环；仅将源 SET 中的外环提前，源身份与声明方向不改，规范化政策显式记录。
孔沿源语义构建，不翻向、填充或放宽容差。负 UV 与平移后的源原点保持。
有限 window 为 trim 包围域加有记录的浮点保护；域外求值先拒绝，不使用内核夹取掩盖错误。
实际解析系数及 NURBS 控制点、节点向量共同用于全段/全域误差界，不能由点采样替代。
最终 native 顶点—曲线端点、顶点—平面和全段曲线—平面的一致性另行证明，不能由源残差或工作 tol 代替。
保守上界不足以证明 Q 时返回 `Indeterminate`，不会把它称为已证明的源缺陷。

模型没有编辑或 native 指针接口，面视图强引用模型，报告不保活 native BRep。
`MaterialVolumeMm3()` 对所有表面模型返回空值。面查询在同一模型内串行；尚未授予一般多线程内核资格。
材料盒体按八角、三组正交边、六个完整外向矩形面证明，支持有理刚体变换和参数/顺序变化，
没有使用盒体 primitive 替代源几何。唯一源边只构建一次，其他面复用同一 native edge；
每面的 mate pair 有各自的 SAME UV，逐项核验实际 radial 邻接和区域/壳的 owner。
`Faces()` / `Face(index)` 返回带源身份的 owning 面视图，`FaceView::Info()` 为每面属性。
整体 `Info()` 的 normal/window 对多面形体为空，`Closest()` 查询整个边界。
`ClassifyMaterial()` 对表面返回空值；材料实体返回内/外/面/边/顶点，
native 工作容差给出的边界分类若不能满足查询误差则返回 Indeterminate。

闭合表面保留 MSSR→SBSM→CLOSED_SHELL 的 `ClosedSurfaceModel`，与同边界材料盒体分开资格。
闭合表面的内外空间区域均标记void；有界void是空间单元，不表示材料包围的空腔。
原生 `IsManifoldSolid()` 只检查拓扑条件，在此可为真；它不能授予材料资格。
`source_shape` 保存原表示/形状/壳身份与角色，`native_role` 保存实际区域/壳数量、材料/有界void数量及该原生谓词。
表面面积、面积形心和边界查询由各面量测汇总；材料volume/census/积分证据均不适用，不能用0冒充成功。
P04材料路径继续计算材料属性；P05全部材料分类返回空值/NotApplicable，即使边界封闭且具有几何内部。

开放共享面片保留 MSSR→SBSM→OPEN_SHELL 的 `OpenSurfaceModel`。
当前范围限定为两个简单直线外环界定的平面区域，可各带零个或多个直接孔，
恰好共享一条完整的源外环边及其两个顶点，
支持折角或共面相邻。源层先证明边界简单；中立计划要求源顶点精确位于定义LINE与PLANE，
再证明每张面除共享边以外严格位于共享直线的一侧，从而证明两个有限面仅沿该边相交。
矩形和严格凸面的既有算术证明保留；其他面逐个检查非共享顶点的精确有理数侧向符号，
允许远离共享线的凹角和前向共线顶点，全部源顶点、分段边及有向使用保持独立。
源层先证明各孔简单、严格位于外环内且互不相交、接触或嵌套；中立计划要求所有孔顶点
也精确位于定义平面。扣除孔不会增加两面交集，共享边须同时属于两个外环。
全部内外环、源bound/loop及每次使用的方向和UV均保持；净面积、面积形心与查询包含孔的扣除。
有孔矩形只以外环证明支撑边，不能把净面积与完整矩形面积比较或用外环替代实际面。
共面时先证明支撑平面一致，再判断两多边形位于共享直线的严格相反半平面；
不依赖某个世界坐标投影或native validator代替区域证明。
同侧重叠、完全重合、内嵌、跨越共享直线或额外触线的外环、共享孔边、更多面及缝组合仍不在此目标范围内。
有效源超出上述目标证明范围时返回Unsupported，数值证明容量不足返回Indeterminate，不自动拆分或删点。
构建复用同一个native edge，各逻辑使用保留自己的方向与SAME-owner UV；每边分别核验2/4个native EU、
mate/radial关系、实际相邻面和自由边身份，不能只核对全局数量。
整体面积、面积形心和bounds包含两个面，整体normal/window为空；`Faces()`保留各面的法向与参数域，
`Closest()`查询完整面片，材料体积与材料分类均不适用。
`Info().boundaryMm`是唯一边长度之和，包含共享边一次；`freeBoundaryEdges`只计自由边数量。
独立S1的6V/7E/2F、6条自由边、面积18 mm²、面积形心(8/3,3/2,1/3)及唯一边总长21 mm分别验收；
其外部自由边总长为18 mm，不能与唯一边总长或两个面周长之和24 mm混淆。
共面相邻控制分别为[0,4]×[0,3]与[4,6]×[0,3]，z=0；仍保留2F/7E/6V、各面的支撑面及参数框架，
面积18 mm²、形心(3,3/2,0)、唯一边总长21 mm。即使支撑平面相同，整体normal/window仍为空，
不自动合并成一个面；每面的查询及UV域保持独立。
三角形、梯形、凸/凹五边形及含共线顶点的边界沿同一构建路径保留全部顶点和有向使用；
实体数量、自由边和容差观测按源拓扑变化，不补成四边形或取凸包。
附加支撑边、关联证明及native邻接输入表按边界大小线性建立，原源区域检查的计费保持。
同坐标但不同身份的边不自动焊接；源连接/方向/闭壳反证在进入native前拒绝，合法的超范围形状明确Unsupported。

材料体积预算取完整样件 Q 的 1e-5 mm³；解析源盒体、实际目标边界的区间通量积分与 native mass properties
分别对照。积分读取内核实际生成的有向有限边界副本，包围解析/NURBS 两种表示，
采用与 native 量测相同的 z 通量及 Green 原函数。每段为至多四次多项式，以区间三点 Gauss 精确积分。
逐段累加有限端点补缝和可能被 native 微小 du 分支跳过的通量绝对上界，避免靠不同接点的抵消放行。
补缝物理残差、有限域、曲面不翻折及总积分误差分别受限；不改写 UV、源边界或工作容差。
临时曲线副本逐次释放，报告的逐环映射不充当积分输入。
产品 JSON 为 `step-native-import.v2` / `PlanarNativeImport`，增加逐面、共享邻接和材料证据；
旧 v1 验收保留原资格，不引入测试包迁移。实际 source/semantic/planar 报告协议保持。
普通失败与调用/报告限额不会交付部分模型，结果中的诊断区分源拒绝、目标不支持、数值证据不足和 native 失败。
`nativeCalls` 只计构造、UV 安装、validator、主要测量的调用检查点；系数/拓扑只读访问不逐项计费，
它不是计时、总内存或硬取消限制。任意 OOM、崩溃恢复、worker、公开绑定均未在本票授予资格。

源单位与参数：VP/LINE/PLANE位置统一到毫米，方向比和源LINE t保持无量纲，
源PLANE UV通过单独长度比例映射到目标UV。unit_normalization记录原单位、精确尺度、typed factor和共享定义链。
STEP_SEMANTICS的conversion快照明确Deferred；STEP_PLANAR补齐源量纲及正尺度/无环解释，完整Checked后才准入native。
名字不决定factor；零负尺度、环、量纲反证各归正确D3/D4，容差与误差预算继续是canonical单位。
可选来源声明：`Report::declarations` 拥有完整typed源快照，和按源顺序排列的声明记录及引用依赖闭包；
JSON `source_declarations` 保留原始记录、位置、实际/继承类型和名义单位分配。
uncertainty按自己的unit_component解释，记录原值、单位和可得的精确毫米值；
与显式一致性预算比较后仅产生独立告警，不覆盖Q、工作tol或Repair=None。
附加声明数值超出有理数容量时明确比较Unestablished，源记录完整保留，不伪造数值。
part分类、旧MECHANICAL_CONTEXT/DESIGN_CONTEXT、指定SOURCE枚举及rad/sr身份不会转成基类或按维数合并。
源层先检查约束；未知关系明确拒绝，不能丢弃后继续发布。
声明与报告不持有native对象，模型或面视图释放后仍能读取声明。
组合P13沿通用路径执行：盒体长度比例127/5，uncertainty仍按自身毫米单位解释为0.0001 mm，
其告警不改变Q或native工作容差。N34仅使factor单位形成环，毫米unit仍由uncertainty引用，
以D4 CyclicUnitDefinition拒绝且不进入native构建。
一般多面体、任意开放共享片、截面/切割及一般修复没有因本切片通过而自动获得资格。
原始来源仍保留；不支持时返回明确诊断，不静默少件。`Repair=None`。

## 构建与验证

### 执行、取消和独立副本

`Options`及文件路径在同步调用入口冻结；原始字节在捕获后不再读取路径。
`execution.control`可指定一次性的`ImportControl`。其他线程可请求取消，返回
Requested/AlreadyRequested/TooLate/AlreadyCompleted；取消请求自身不访问native。
`execution.progress`在捕获、源检查、逐native调用间、报告及提交前同步通知。
不可中断调用先安全返回，候选撤销完成后返回Cancelled；旧快照和捕获源不变。
observer异常是ExecutionFailure；意外`bad_alloc`向调用者传播，不能伪称已配置限额耗尽。

模型owner和必需报告在发布前完整准备，取消/成功由一次原子提交裁决。
成功之后迟到取消返回TooLate，模型寿命由快照/view持有者决定。
本模块的native构建、查询、副本、编辑回调和释放共用进程内串行锁；
其他直接native调用者仍须协调，独立context不代表整个SMLib可任意并发。

`nativeLimits.processPrivateBytes`检查进程PrivateUsage的软上限，`execution.deadline`使用steady clock。
检查发生于安全边界并在发布前复核，单次操作可以超出边界间预算；不提供OS硬保证。
`hardMemoryBytes`明确返回UnsupportedHardMemoryContract。
执行报告区分未测null、检查点最大private commit和进程生命周期峰值；后者不归因于单请求。
观测截止为最终报告序列化之前，发布前预算拒绝会记录额外失败观测。
工作内存、截止和取消与硬取消/崩溃隔离分开，生产档位仍待实测定标。

`ModelSnapshot::CopyForModeling()`返回独立结果和`EditableBrep` owner：
深复制保留几何类型/材料角色，在独立context中通过指针及LEVEL2验证后交付。
`Edit(callback)`仅在串行回调作用域借出BRep，允许非消费式原地编辑；
不能删除/消费根或保留native借用。副本释放与原快照无关，编辑结果不继承原导入质量资格。
报告和声明仍不持有native对象。进一步建模、worker和跨进程物化各需自己的资格。

### 命令行

根 CMake 在启用 `SMLIB_BUILD_VALIDATION` 时注册库和 CLI。独立 CMake 可接收已有 `SMLib` / `SM_API` targets；本机 Windows 路径使用
匹配源码、配置、CRT、宏及工具链的 SDK，目录中分别为 `release/`、`debug/` 的 DLL 与 import lib：

```powershell
cmake -S source/STEP_IMPORT -B <build> -DSTEP_IMPORT_NATIVE_DIR=<sdk>
cmake --build <build> --config Release
ctest --test-dir <build> -C Release --output-on-failure
<build>/Release/step_native_import --fixture-limits --quality Q-P0-mm-01 input.step --query 4.2 9.6 8
$env:STEP_NATIVE_BINARY = '<build>/Release/step_native_import.exe'
python -m unittest tools.step_import_native_validation.tests.test_native -v
```

CLI 返回 0 Accepted、1 Rejected、2 Unsupported/参数错误、3 ResourceLimit、4 SourceFailure、
5 必需输出未发布、6 BuildFailure/调用异常、7 Deferred、8 Indeterminate、9 PostcheckFailure、10 Cancelled。
CLI可指定源字节/记录/引用/值/深度/字符串/单记录/ID长度、语义访问和planar工作限额，
以及`--max-process-private-bytes`、`--deadline-ms`和`--hard-memory-bytes`。
CLI截止可设在24小时内；所有执行限制按上述合作性质处理。
JSON 输出为 binary stdout，字节预算不受 Windows 换行转换影响。
测试直接沿内部公开接口和真实 CLI；没有另建测试包发布机制。
Windows 资源检查包含成功、各调用预算边界、后验预算不足、报告超限及保留模型的阳性对照。
当前匹配的共享调试 CRT、非 TBB SDK 下，Debug 以 CRT 用户分配判断残留，Release 以进程堆判断；
两者都保留总堆/PrivateUsage 观察及保留模型的阳性对照；
该分配域的覆盖不能推广到其他 SDK 分配器。具体执行收据保留在本地。

早期验收使用 C++20 适配器配套 C++17 原生内核；该历史记录不代表一般 ABI/跨平台资格。
当前全仓构建基线为 C++20，旧验收记录不替代新基线验证。
`SmIterationValue::operator==` 仅补 `const` 以使头文件能在 C++20 equality rewriting 下编译，
函数原有恒 FALSE 主体与求解算法不变。对应头文件见
[SmLocalSolve1d.h](../SMLib/inc/SmLocalSolve1d.h)。

## 首profile的资格边界

范围固定为`203e2-P0-Planar-0.1`及N2635声明见证，不以替换HEADER推导其他AP版次。
冻结49项合同的端到端核对包括13个正例模型和36个无模型反例；其中7项为明确Unsupported，
不能把范围外内容称为源文件非法。逐项输入、源图、诊断、独立真值、Q、实际模型和查询由
[原生验证工具](../../tools/step_import_native_validation/README.md)重复核对；实际运行收据保留本地。
通过表示这组合同符合，并不表示任意同类STEP文件均可导入。

| 能力 | 当前可验收范围 | 仍未授予的范围 |
|---|---|---|
| 文件与schema | 自包含ASCII Part21，2;1，单个无参数DATA，简单/复合实体、引用与原始位置；AP203e2限定闭包 | 全部DATA/编码/载体、AP203旧版完整读取、AP214/AP242 |
| 几何与拓扑 | LINE/PLANE单面及直接孔、负UV原点、两平面区域折角/共面相邻共享支撑外环边（可凹/保留共线顶点/各带多个直接孔）、矩形材料盒体及同边界闭合表面 | 一般多面体/开放片、跨越或额外触及共享直线、共享孔边/缝、重叠与一般自交、全sense组合、曲线曲面扩展、周期/奇异/空腔 |
| 单位与声明 | SI长度/名义角单位、已检查的正conversion链、原始声明及自身单位的uncertainty | 任意量纲/因子组合、装配混合单位与实例变换 |
| 结果与质量 | 显式合成Q、严格原请求、owning只读快照/view、独立报告、受限查询、独立可编辑副本 | 生产默认精度、公开稳定绑定、任意后续建模资格 |
| 失败与执行 | 因果诊断、反例无模型、普通失败清理、安全边界取消、提交竞争、软资源检查 | 任意OOM、硬取消、崩溃恢复、worker/跨进程、应用版本令牌接入 |

失败保留类别：源反证为Rejected；源/目标范围为Unsupported；数值证明容量不足为Indeterminate；
配置限额、取消、native构建失败和后验失败分别报告。后三类失败不能替代已有明确源反证，
任何失败均不交付本次部分模型。`Repair=None`，没有隐式stitch/heal或预算放宽。

与总体规格阶段的对应关系如下；每一行只授予已验证的子范围，**没有整体P0—P7完成声明**。

| 总体阶段 | 本切片提供的依据 | 剩余门槛 |
|---|---|---|
| P0 契约基础 | 49项冻结输入/独立真值、合成Q、源到目标和owner合同、真实DLL/资源观测 | 其他族真值与测量器、生产资源/质量定标 |
| P1 源读取 | 本profile的词法/引用/类型/单位/规则闭包、原始源保留 | 多DATA/编码/扩展载体、其他AP/edition完整清单 |
| P2 纵向切片 | 带孔平面、两带孔平面区域折角/共面相邻共享支撑外环边（可凹/保留共线顶点）、材料盒体/闭合表面、参数/身份/质量/生命周期贯通 | 一般开放片、完整方向族、截面/切割及代表性建模资格 |
| P3 精确BRep扩展 | 无新增族资格 | 解析曲线/曲面、B样条、pcurve/master、周期/奇异、空腔 |
| P4 装配实例 | 本地独立副本是基础能力 | 产品关系、实例层级、重复/镜像/缩放/混合单位及全请求失败 |
| P5 修复 | 严格导入，Repair=None | 每项白名单修复、局部与累计预算、特征保护 |
| P6 接口隔离 | 内部C++ owner/view/报告、合作取消与单提交点 | SM_API/Python稳定化、worker/崩溃隔离、跨进程物化、应用接入 |
| P7 晋升与真实模型 | 合成合同回归入口 | 60回归/20盲测/8大型及模型库分批验收、独立分歧裁决与生产定标 |
