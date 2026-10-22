# STEP_IMPORT（内部 C++20）

通用互操作导入可显式选择 `--preserve-opaque-file-name-text`，保留 FILE_NAME 的未知编码
文本及逐值诊断，原始字节、字段和作者/组织列表顺序不变。旧的仅文件名选项保持原义。
此选项要求 general BRep 与非几何互操作，不能用于严格/P0 导入；不推断代码页，
不放宽协议标识、实现级别、DATA 几何或单位检查。原始层边界见 [STEP_SOURCE](../STEP_SOURCE/README.md)。

通用工作容差检查失败时，报告仍保留触发失败的有限容差观测及其拓扑类型、来源身份。
该数值是原生工作容差，不代表允许的误差或修复预算；非有限值不写成非法 JSON 数字。
候选仍拒绝发布并清理。当前内核在 UV 创建／安装中保护“不调整容差”请求；旧 SDK
曾在嵌套投影重试中增大边容差。最终工作容差检查继续独立验证实际结果，不可省略。

通用路径对圆柱面上的部分 CIRCLE 边先尝试一次受控 surface 投影，以派生 UV 预算的
1/8 作为数值求解精度，不调整原生边容差。只采用单分支、参数配对采样通过的候选；
多分支或未建立候选时沿原路径检查。来源圆弧、参数区间、拓扑和单位不改写，记录
`ControlledCircleUvProjection`、来源面/边/边界及求解精度。随后仍执行面闭合、缝、
最终配对 UV、原生校验与工作容差预算检查。采样不是连续误差界，也不是源 pcurve 修复。

圆锥面上的非闭合 CIRCLE 和 B_SPLINE_CURVE_WITH_KNOTS 边同样可尝试单分支受控投影，
记录 `ControlledConeUvProjection`。投影的数值逼近精度与源曲线到曲面的法向距离分别
检查：后者可大于求解精度，但返回距离和最终参数配对误差都不得超过原派生 UV 预算。
通过这一步不代表整个模型可用；后续 UV 曲线有效性、面闭合和容差检查仍可拒绝候选。

通用构建支持已检查的 `CONVERSION_BASED_UNIT + LENGTH_UNIT` 链，终点必须是已登记的
SI `METRE` 长度单位；SI prefix 与嵌套因子按实际实体引用求积，不使用单位名称猜测。
因子必须逐项为正。源因子和每步比例使用固定 4096 位幅值、检查溢出的精确整数计算，
乘法先交叉约分；只有最终毫米比例收窄到既有 `Fraction` 的有符号 63 位幅值范围。
因子或中间比例超出 `Fraction` 不再单独阻断可表示的最终结果；4096 位运算容量不足，
或最终比例仍不能进入 `Fraction`，继续返回 `Indeterminate`。未支持的基单位或名义量
返回 `Unsupported`，不回退到近似比例。此容量不是几何预算或生产精度设置。
源 SELECT、名义类型、量纲和必要条款继续由 `STEP_SEMANTICS` 检查；本层不另造维数实体，
也不把其“转换解释待下层”的源状态改写为已通过全部检查。

长度比例只作用于坐标、半径与 VECTOR 长度等长度量，应用一次；方向比、LINE 参数、
B 样条节点与权重不乘长度比例。角度继续按原独立角单位合同解释，本次不扩展转换角单位。
`unit_normalization` 仅在实际通用长度转换时增加，记录源单位／因子／基单位身份、
精确源因子与逐步毫米比例；原始字符串、数值和声明仍由 owning 源快照保留。
已有 SI 路径与平面证明路径的报告字节和计费顺序保持。

`ModelSnapshot::LengthUnits()` 返回独立的值副本，通用与平面模型均保留实际毫米归一化，
丢弃可变 outcome/report 后仍有效。转换定义中的记录索引相对于该模型的 `Source()`；
读取原实体时须保留模型或其 owning 源。原生存档本身不编码 STEP 来源资格，仍须保留报告／源。
`UnitNormalization` 的内部 C++ 布局已扩展，库与消费者需一起重建；内核布局和存档格式不变。
`generalDefinitions` 中的 `factorValue`／`scaleMm` 为 `ExactUnitRatio`，以只读十进制整数
字符串的 `Numerator()`／`Denominator()` 保存规范约分结果；调用者不得将其强制收窄。
这些视图借用该比值对象，使用时须保留返回的 `UnitNormalization` 值副本。
原报告的 rational 对象和字符串 num/den 格式保持，原先可接纳比例的值保持。
`lengthToMm` 仍为 `Fraction`。Boost.Multiprecision／Config 1.89.0 的未修改独立头文件
固定在 `third_party/boost-multiprecision`，仅内部单位实现编译使用，不进入公共头文件。
单位合同有独立 CMake 目标 `step_import_general_unit_tests`／CTest
`step_import_general_unit_contracts`，可单独构建与验证。

通用路径可显式设置 `Options.numericalIntegration=NumericalIntegrationSettings{1e-6}`，
CLI 使用 `--integration-relative-accuracy 1e-6`。范围为 `[1e-8,1e-4]`，必须为有限数；
严格平面证明路径拒绝此配置。它只控制原生融合面积、体积及一阶矩积分的数值停止条件，
无量纲，不改变几何一致性预算、工作容差或修复许可，也不是全局相对体积误差保证。
未指定时保持原来的 `1e-4` 计算与报告字节，不自动依据 uncertainty 或独立读取结果调参。

显式请求的报告增加 `numerical_integration`，记录请求值、实际采用值和积分完成状态，
并标记未建立连续误差界。源检查失败时实际值为空；原生面查询开始后才记录实际采用值，
不会静默钳制。积分完成后仍可能因其他检查、取消或报告预算而失败，不能把该完成标记
当作模型交付成功。原有逐面计费、合作取消、安全边界和完整发布顺序保持。

`ModelSnapshot::NumericalIntegration()` 将实际数值观察保存在 owning 快照中，丢弃 outcome／
report 后仍有效；默认通用模型也返回所用 `1e-4`，平面证明模型返回空值。
独立建模副本与原生存档不继承这份源请求资格；后续查询需自行选择数值控制。
特别是 `SmApiBrepComputeMassProperties` 仍按其既有合同将更严请求钳制为 `1e-4`，
不能把传入较小数值当作已经执行更高精度。公开 Bernstein 屋面盒体回归的体积真值
为 `6000+200/121 mm³`，由 Bernstein 基函数积分给出，不来自 CAD 输出。

本次扩展改变内部 C++ `Options`／`Report` 布局；原生库与消费者须一起重建。
源格式、原生存档及既有默认质量配置保持。性能比较必须同时记录实际积分配置，
不能合并不同停止条件的量测或将其误作几何精度。

`ModelSnapshot::ClassifyMaterial` 先执行点与面／边／顶点的原生相交检查，
再确定材料区域。区域射线若击中边或顶点，可能属于擦边退化情况，不能直接采用该次
区域答案；实现按固定方向最多尝试 7 条射线，采用面命中或成功的无限区域无交点结果。
证据用尽时返回 `Indeterminate`，不从包围盒推断内外，也不增加查询／工作容差。
实际位于边界的点仍通过原最近点误差预算验证，保持面／边／顶点分类。
该查询沿用原生求解器能力，不构成连续距离界或任意几何的正确性证书；
源数据、模型、导入检查、报告和发布流程不受此重试影响。

声明 1 的 DATA 十六进制转义可另选
CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex／CLI
--interpret-level1-with-lowercase-hex；仍须显式通用质量与非几何互操作。
仅解释 X/X2/X4 payload 中的小写数字，原 token、严格失败位置和逐值完成状态随 owning
载体见证保存。旧标志、严格模式、HEADER 和其他声明不放宽；坏 Unicode、非 hex、
不完整 payload、限额和下游错误仍拒绝。成功仍为 AcceptedUnderInterpretation，CLI 11。
这不猜文字编码、改写原件或认证源声明；失败／取消仍不交付部分模型。

裸 `AUTOMOTIVE_DESIGN` 可另选 `SchemaInterpretation::Ap214NameOnlyAs2010`／CLI
`--interpret-ap214-name-only-as-2010`，并显式选择通用质量和非几何互操作策略。
它只选择固定 2010 兼容子集，不从裸名、APD 或年份认证源版次；原裸名、空 OID 和原拒绝
诊断保持。完整结果为 `AcceptedUnderInterpretation`，仍与普通结果分开。该选项与旧的
八段标识解释互不替代，也不能覆盖其他协议、多 schema 或任意未知 OID；CLI 不允许冲突选择。
已登记标识不因请求该选项而获得假设；类型、单位、几何、限额、取消和发布检查不放宽。

通用开发请求可独立选择 `Options.carrierInterpretation=Level1AsCorrectedEncoding`／CLI
`--interpret-level1-as-corrected`，并须同时显式选择通用质量与非几何互操作策略。
它只让声明 `1` 在未验证的修正版编码假设下进入现有读取链路，保留原始声明、字节、
跨度及原严格拒绝诊断；不实现原 1994 SELECT 形式，不把 `1` 改写或认证为 `2;1`。
其他载体、字符串、引用、schema、类型、单位、几何、预算与完整发布检查保持原约束。

实际采用载体假设且通过全部原生检查时，结果为 `AcceptedUnderInterpretation`，CLI 返回 11；
它与 schema 假设分别记录，成功绑定 schema 不能验证载体。报告的
`source_carrier_verified=false` 与 `source_carrier_interpretation` 标明实际假设；
`source_binding_verified` 继续只描述 schema 假设。请求在普通 `2;1` 上未应用时仍是
普通 `Accepted`，不会为模型捏造假设。两种假设可以显式组合，不能进入普通验收或性能组。

`ModelSnapshot::Source()->Source().AppliedCarrierAssumption()` 提供 owning 源内的不可变见证；
面、独立线框与辅助几何 owning view 的同名方法借用该见证，随 view 保持有效。
`Report.carrierAssumption` 还可在后续解析／引用失败且源图未交付时保留原见证。
清除可变 outcome/report 不会清除模型或 view 的资格。取消、报告不足及失败仍禁止部分发布；
建模副本、原生存档不继承源验收资格，保存存档时须另存原报告。默认和严格／P0 路径不变。

显式通用选项 `Options.normalizePeriodicFaces`／CLI `--normalize-periodic-faces`
可对圆柱／圆锥的 U 周期近缝面执行目标表示规范化。默认关闭，P0 拒绝；
它不选择质量配置、不放宽容差、不修复源几何，也不承诺任意周期面的支持。
首版仅处理一个近缝候选面，并要求单个显式 `FACE_OUTER_BOUND`；以精确等参数线切分目标拓扑，再限制
未用支撑域。正权重 UV 控制包围盒必须被实际保留域完全覆盖，整个保留的
原生 NURBS 矩形通过与 `definitionMm` 对比的连续差界检查；每个目标面还须与
切分前捕获的原始原生支撑面比较，而非仅比较切分后的裁域输入。证明不成立即拒绝。
该差界针对原生二进制 NURBS，不是原始十进制源或解析重分类的误差证书。
保留原生解析子类／参数对应，并继续执行完整原生、最终 UV 和工作容差检查。

旋转曲面的通用 `TrimWithDomain` 会重建中间参数，不能用于直接复用原 UV。
周期规范化先在私有通用副本上确定实际裁域，在原域仍有效时更新解析辅助数据，
再提取原 NURBS 基并重建参数转换缓存；不重新生成或重参数化定义曲面。
构建时的访问适配不交付给调用者，输出仍为普通原生圆柱／圆锥类型。
每个额外复制、提取及辅助数据更新都计入可取消的执行边界，失败不交付部分候选。

每条原源边使用的完整原生基曲线及解析子类原样保留，目标子边仅选择其参数子区间；
子区间按原参数精确、无重叠、无缺口覆盖原使用区间。原源顶点的位置与身份不变，
派生顶点逐个验证其原基曲线端点对应。不能用拟合、重参数化或调整源点来通过检查。
临时来源属性在完成前删除，失败／取消时整个未发布候选在 context 前释放。
额外原生／证明工作沿用可取消的 `nativeCalls` 边界；保留曲线控制点总量和
各次 UV／曲面证明的临时控制网受 `sourceLimits.values` 限制。

`ModelSnapshot::TopologyNormalization()` 返回 owning 对应清单的独立副本：
目标面映射到源面／源曲面，目标边映射到源边或明确的派生切线，目标顶点映射到
源顶点或关联源边的原生基参数。`basisParameterInterval` 与派生顶点参数采用
原始转换后原生基参数，源 STEP 参数关系仍由 `parameter_mappings` 解释。
目标面按源面分组，子面可以共享 `FaceView::SourceId()`；不能将该 ID 当目标唯一键。
没有实际规范化时返回空；显式报告以 `periodic_normalization_requested` 与
`topology_normalization` 区分请求和实际执行。`completed` 仅表示这一步完成，
后续失败／取消仍不得发布模型。建模副本和原生存档不自动继承这份源资格，
须另存报告。绘图成功和原生 validator 通过也不代表完整源符合性或连续几何验收。

显式通用选项 `Options.restrictUnusedSupportDomains`／CLI
`--restrict-unused-support-domains` 可限制未使用的 B 样条支撑面域。默认关闭，P0 拒绝。
首版要求源声明唯一外边界、非闭合／非周期参数图、正权重 UV 控制网，以及完整边参数
包含于对应 UV 自然区间。边界控制凸包包围有界参数区域，孔边界也完整保留；
图分支或区域条件不明时不执行该规范化。

提取使用保参数的结点分割，不拟合、不平滑、不移动源点。实际提取域必须覆盖控制包围盒；
原生前后曲面在整个保留矩形上的差由公共结点细化、张量 Bernstein 系数及正权重分母
的向外舍入区间界验证，误差必须满足显式 `definitionMm`。这不是原始十进制 STEP 或整个
模型的连续误差证书。候选通过证明后才转移 owner；UV 独立副本恢复，安装不增长拓扑容差，
最终原生检查和完整提交仍必须通过。报告用 `UnusedSupportDomainRestriction`、原／请求／
实际参数域与数值界记录来源对应；原始完整支撑面仍在 owning 源快照中。
该显式路径的证明工作块也计入受限、可取消的 `nativeCalls` 执行边界；它不是底层内核
函数的逐次调用计数。临时控制网和 Bernstein 乘积项分别受当前 `sourceLimits.values`
上限限制，不能通过拆成更多面来绕过整体执行／内存／取消检查。

另一个独立研究配置 `Q-AP214-work-tolerance-study-mm-01` 保持源一致性和派生 UV 预算
为 0.01 mm，初始原生工作容差 0.01 mm、上限 0.02 mm、允许增长 0.01 mm。
它是应用明确选择的原生容差校准，不由源 uncertainty 推导，不自动开启限制域选项，
不修改现有冻结配置，也不是生产默认精度或源几何修复。

通用构建的最终派生 UV 若超过同参数三维残差预算，可重新投影并保留已有闭合端点，
保持新投影的原参数和权重，不使用裁剪后拉回原区间的重参数化。
原投影与端点匹配后的残差都须通过原预算；定义性三维几何和源数据保持，
安装时不增长拓扑容差。报告记录 `DerivedUvEndpointMatching` 及实际运行的量测，
失败和取消释放未安装候选；完整模型、缝／极点／连续性及最终 UV 检查仍须通过。
这些 UV 量测是开发路径的结点分段采样，不是全段误差证书或源 pcurve 替换策略。

内部 `CurvePointDefinitions` module 按源成员逐项向线框／辅助几何角色追加曲线和点，
集中维护源身份复用、曲线 owner／索引、参数对应及可选量测证据。相同位置的不同源点
仍是不同定义；定义内部区分未运行量测与实测零值。线框保留既有零值聚合报告，辅助
角色继续省略不适用的量测条目。线框包围盒仍按每次使用收费，
包括定义复用；原遍历、报告、取消和提交顺序保持。
`Finish` 只移交独立定义存储给快照，查询和存档借用只读内容；原生对象仍在同一锁下
先于 context 释放。内部强类型区分角色证据、源记录及曲线／点索引，禁止复制 owning
存储；源身份归属、索引范围和数值质量仍须运行期检查。平面／坐标框架保持原流程。

显式通用路径可构建 AP214 所选 CGR/CGRR 的独立辅助几何：直接三维坐标框架、点、
无界解析 PLANE，以及已支持的独立有限曲线子集（单层裁剪 LINE/CIRCLE/ELLIPSE、
完整圆/椭圆、带显式节点的多项式/有理 B 样条）。其他必需成员在原生调用前拒绝。
`SupportingGeometry()` 返回 owning 只读 view，保留源表示、关系、成员和原生存档索引。
坐标与长度归一化为 mm；方向无量纲；曲线查询使用实际原生参数。
`EvaluatePlane` 的 u/v 为 mm，`ClosestPlane` 显式选择无限平面，不参与材料分类。
原生有限参数哨兵不是源裁剪边界；超出可表示范围即查询失败，不静默钳制。
坐标框架只定义坐标，不被当成额外点、曲面轨迹或实例变换。

辅助几何和材料 BRep 分开持有，材料数量、范围、面积、体积、分类及边界查询仍只针对
所选 BRep。完整 `CopyForModeling` 拒绝混合模型；应用可明确选择独立
`CopyBrepForModeling`。全体 native owner 在 context 之前、同一原生互斥锁下释放，
沿用整个请求的取消、资源边界和原子报告发布。

完整原生存档保存独立曲线、解析平面和点适配 BRep。坐标框架通过明确标注的 canonical
plane adapter 保存原点与轴；它不是源 PLANE 或 BRep 面。回读核对全部成员库存、
曲线定义、解析平面/框架的原点、轴、UV 尺度和参数域，以及点和材料边界。
同一源成员跨线框/辅助表示复用原生定义，坐标相同而源身份不同的点保留独立身份。
STEP 身份与角色对应保留在 owning 快照/报告中，原生 part 格式本身不编码这些语义。
辅助表示按源曲线共享已测残差；没有运行相应量测时不发布该辅助量测行，也不写成零。
`supporting_geometry` 报告只在有该内容时增加；开发路径仍未建立全局连续误差界或完整
AP 符合性。公开独立真值见 [合成辅助几何样件](tests/fixtures/supporting_geometry.md)。

源层可在显式通用范围中保留 BRep 与独立关联线框，具体图、角色与必要规则见
[源检查合同](../STEP_SEMANTICS/README.md)。原生通用路径分别持有材料 BRep、独立曲线和点，
不从关联关系推断合并、变换或材料边界。线框支持 CARTESIAN_POINT、单层 LINE／CIRCLE／ELLIPSE
裁剪、完整 CIRCLE／ELLIPSE 和显式节点 B 样条（含已登记有理复合类型）；嵌套裁剪等其余构建
明确 Unsupported。LINE 裁剪参数无量纲，VECTOR 长度只缩放一次；圆锥曲线沿已有角单位、
主表示、双表示一致性和方向合同解释。源点、集合成员、共享定义及每条关联身份保持，
同坐标的不同源点不合并。全部必需几何、检查与报告完成后才经原有提交点整体发布。

`Wireframes()` 返回按源曲线集分组的 owning `WireframeView`。`Info()` 含原表示／关系／集合／
成员身份、独立点坐标、实际原生曲线参数域和方向对应；`EvaluateCurve()` 先拒绝域外参数。
视图保活模型、context 及源解释资格。`Closest()` 只查询本集合；必要成员求解失败就拒绝答案，
已知精确重合的零距离可直接裁决。求解与原生包围盒仍是开发量测，不授予连续全段定义误差、
已认证最近距离界或任意线程安全。原生定义端点误差和源裁剪残差分别按其独立预算检查与报告。

`ModelSnapshot::Info()`、`Closest()` 和材料分类／体积针对所选 BRep 边界；独立线框不参与
材料内外判定。含线框时 `CopyForModeling()` 返回 Unsupported，避免隐式丢掉关联几何；
调用者可明确选择 `CopyBrepForModeling()` 取得独立的 BRep 建模副本。
`WriteNative()` 保存完整 part：独立曲线数组、材料 BRep，以及每个独立点对应的无边无面
vertex-only 适配 BRep。该适配不表示源文件声明了 VERTEX_POINT；其索引对应保存在报告中。
原生容器不自动保存 STEP 身份或关联语义，须同时保留原报告／源快照。
写出使用私有副本并读回核对完整数量、曲线类／节点／权重／控制网、点和 BRep；
不能只用顶层 writer 的成功状态证明完整保存。读回保留原生存档记录的工作容差，
不套用外部数据转换的坐标尺度推断。失败和取消在 context 前释放所有未发布曲线与 BRep。

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
内核 `SmFace::CreateUVTrimCurves(FALSE, ...)` 在整个投影／安装过程中禁止边与顶点工作容差增长，
并保留外层只验证状态；重试仍报告实际得到的投影间隙，不以重置容差掩盖写入。
这只建立不调整合同，不能证明派生 UV 足够精确。受控投影、最终 UV／原生检查、
工作容差预算和失败／取消时不发布模型的规则继续保留。
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

存档复核使用 `SmNativeArchive::ReadPartFromFile`（`SmNativeArchive.h`）。它复用原生
part 解码器，关闭自动解析类型识别、UV 重新生成及版本触发的 healing，保留存储的
定义类型、NURBS/UV 参数和正工作容差；原生格式不变。完整读取后才移交输出，
失败不交付已读取的部分对象。调用者须销毁返回对象后再销毁其 context，并自行
执行有效性／定义／UV 验收；这不是 STEP 来源资格的持久化。
通用回归要求回读曲面、三维曲线及 UV 曲线类型一致，继续比较完整系数、节点和边参数，
不再借用历史解析识别作为回读类型例外。旧 `SmBrepData`／`SmApiReadPartFromFile`
入口保留原来的识别与 healing 行为，不能用于声称上述存储定义保真。
使用此新入口的消费者需要与包含该符号的新内核 SDK 一起链接；C++ 布局与文件格式不变。

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

圆柱面上的非闭合 `B_SPLINE_CURVE_WITH_KNOTS` 边（包括有理样条）现在也复用受控
单分支投影，记录 `ControlledCylinderSplineUvProjection`。精确圆弧即使以样条表示，
旧 UV 重试也可能增大边容差；该路径保留源样条、权重、节点与参数区间，以原派生 UV
预算的 1/8 求解，并要求安装前后的边工作容差完全相同。源法向间隙、参数配对采样、
闭合／缝、最终原生校验和工作容差预算仍须通过。未建立单分支候选时仍由原路径验收；
不把有误差的源曲线投影改写到曲面上，不提供连续误差证明或新增修复许可。

通用构建可直接保留 `SPHERICAL_SURFACE` 的球心、轴系及正半径，构造解析 `SmSphere`；
不拟合定义曲面。源正长度仍须满足内核的有限非退化范围，超出时明确返回
`GeneralSphericalKernelRadiusRange`。对重复圆弧且两端经度一致的情形，可仅移动
目标球面的整周期经度切口，记录 `SphericalLongitudeChart` 和以度为单位的目标解析域。
端点一致只是切口候选，不能证明整条边是子午线；最终缝、UV 和原生校验仍决定是否交付。
普通原生 UV 为有理 NURBS 参数，未声明它与源 pcurve 或源角参数相同。

公开球带验收包含解析面积／体积、不同切口位置、AP214 与修改版 N916、源数据保留、
复制和存档回读。错误半径和内核范围外半径拒绝发布。单闭合圆边界的同轴球冠可按下述
路径补建目标极点／缝；任意球面裁剪、退化边和其他极点表示仍须各自建立支持资格，
不得把源类型识别或网格可见性替代这些检查。

`B_SPLINE_SURFACE_WITH_KNOTS`（含有理曲面）上的非闭合 CIRCLE／B 样条边也复用
单分支受控投影，记录 `ControlledSplineSurfaceUvProjection`。这只生成派生 UV；
源曲面控制网、权重、节点和三维边定义不拟合、不改写。安装前后边工作容差必须相同，
投影法向距离、参数配对采样、最终缝／环和原生检查仍使用原预算。未取得单分支候选
则继续原路径并接受原验收门槛，不把已有的错误源几何投影替换为新定义。
公开精确有理圆柱曲面控制涵盖样条边、圆弧边、解析面积／体积、真间隙反例及复制回读。

已明确携带极点顶点和重复圆弧缝的球面，在仅一个端点具有唯一经度时，可用非极点
端点选择整周期目标切口候选；不会把 `atan2(0,0)` 当作极点的源经度，也不移动顶点。
公开显式极点半球覆盖两种边方向及反向圆弧参数化，面积、体积、包围盒和复制回读通过。
两个端点都无法确定经度时仍不猜测；这个显式缝选择器本身不新增拓扑，隐式球冠走下述独立路径。

隐式球冠仅在 SPHERICAL_SURFACE 的一个显式 FACE_OUTER_BOUND 包含单个闭合 CIRCLE
使用时建立候选。轴、圆心、半径与端点须通过向内舍入的定义预算检查；曲面轴和圆轴
还须满足独立的无量纲平行条件。已合成的边／边界／面方向决定保留南侧或北侧，不按
较小面积猜测。目标上添加一条正反使用的子午线缝及一个极点，原球面、源边区间和源
顶点不改写，记录 `ImplicitSphericalPoleCut` 和 `ImplicitSphericalPoleChart`。

派生极点通过 `TargetVertexOrigin.surfacePole`／JSON `surface_pole` 记录源面、源球面
及相对源轴的正负方向，不填写虚构的 source_vertex_id 或源边参数。原始源顶点数组与
目标数组分开；发布前核对完整目标清单、源区间、源顶点和派生点，随后沿用原生校验、
预算和提交门槛。自动补建也输出 topology_normalization，但 periodic_normalization_requested
仍为 false。显式周期请求没有候选时保留已有来源记录；发生单面周期拆分时，
保留派生极点的句柄、位置及曲面来源，并重建目标索引与源边区间覆盖证据。
已完成／已重排的来源清单不按原始面顺序再次解释。支撑域裁剪的组合尚未验证，返回
`SphericalPoleNormalizationCompositionUnestablished`。

验收覆盖隐式半球、南北大小球冠、两片球冠构成整球、刚体变换、米／毫米单位、
复制回读、取消及逐原生检查点资源中断。积分设置仅控制量测，不放宽几何预算。
这不是倾斜圆截口、多环、任意退化极点或带源 pcurve 情形的通用修复能力。
C++ TargetVertexOrigin 布局已扩展，库及消费者须一起重建；内核 ABI／存档格式不变。

极点补建与周期拆分的组合验收包含球冠＋圆柱的公开控制：圆柱实际拆分后，原始顶点、
源边区间、派生极点和新切割顶点分别保留其来源；复制、存档回读和取消检查继续执行。
该控制使用显式工作容差研究配置保留原有微小缝偏移，不将研究预算升级为生产默认。


显式 `Options.dataStringPolicy=PreserveAsciiInvariantBytes`／CLI
`--assume-ascii-invariant-data` 为原始 DATA 高位字节添加“不表示 ASCII 字符”的应用解释，
不猜字符编码、不生成名称文本。它要求 general + nongeometric interoperability；
默认路径与原 `PreserveOpaqueBytes` 行为保持不变。该解释用于已证明归属与反向用途的
AP214 表示名称，排除纯 ASCII 特殊用途；转义、控制字节、未知用途不能获得此证明。
原始文本与诊断保留，几何、单位、预算、取消与原子提交规则保持不变。
实际捕获到该解释的模型返回 `AcceptedUnderInterpretation`（CLI exit 11），报告含
`data_string_interpretation`、`data_encoding_verified=false`；只请求但没有适用文本时
不会把结果标为已应用。源 snapshot 持有不可变 token 证明。此选项通过 C++、inspect/native CLI 和独立查看器
`--assume-ascii-invariant-data` 提供；查看器同时记录请求值与实际应用状态。

面、线框和支撑几何 owning view 的 `HasAsciiInvariantDataAssumption()` 同样读取源内证明，
释放导入结果后不会丢失该解释资格。


同一零件的多个 MSB 现在可以经过 general 源检查，并在源报告中完整列出
`part.source_shapes`，聚合角色为 `MaterialSolidCollection`。原有单体 API 对集合请求在原生分配前返回
`GeneralMaterialCollectionNotImplemented`；显式集合 API 的范围见下节。源检查成功本身
不计入原生导入成功。


显式 `Options.reportEncoding=ReportEncoding::CompactV2`／CLI
`--compact-source-report` 选择无损紧凑源报告，仅用于 general 路径。原格式仍为默认。
原生报告以 `source_report_encoding=CompactV2` 标明选择，嵌入的语义/原始层分别采用
`step-semantics.attributes.v2` 和 `step-source.spans.v2`：共用重复属性说明，原始输入
保留一次，重复原文片段由原字节区间还原。字段重声明、单位、原始数值、来源身份、
诊断和验收字段均保留；展开规则见相应组件 README。调用者必须识别新的嵌套格式。

该选项不改变报告容量、几何预算、验收、取消、两次报告生成或最终原子提交；不在
失败后自动切换。紧凑报告仍超限时不发布模型或部分 JSON。它改善报告表达，不授予
任何新几何能力；多实体构建未实现时仍返回明确的 Unsupported。


## Material collections

`ImportMaterialCollectionFile/Bytes` returns `CollectionOutcome` for one part's
complete registered MSB inventory. It requires the general path and preserves
one immutable source snapshot with independent native BReps. A successful outcome
owns `collection`; the existing `ImportFile/Bytes` still owns a single `model` and
retains its multiplicity refusal. CLI users select `--material-collection` and may
also select `--compact-source-report` for the mandatory report.

`MaterialCollectionSnapshot::BodyCount`, `Body(index)`, `SourceShapeId(index)` and
`Source()` expose the complete ordered inventory and owning body snapshots. A body
or its face view can outlive the collection outcome; modeling uses that body's
existing independent copy API. Body indices follow serialized representation item
order, not size or geometric priority. Shared source shells/geometry never merge
source solid identities or native context ownership. Overlapping bodies remain
separate. No aggregate union volume or material classification is inferred.

Every body uses the existing construction, installed-UV, native validation,
tolerance, source correspondence and measurement checks. One request execution
ledger charges all body work; it never resets the budget between members. The
collection and required report publish only after every body and the final
cancellation decision succeed. Failures discard all unpublished bodies while
retaining per-body observations. Late cancellation/report failure likewise returns
no collection. Separate native contexts do not imply kernel thread safety;
construction, queries, destruction and archive copying retain native serialization.

The report format is `step-material-collection-development.v1`. One root source
inspection serves all bodies. `material_collection.bodies` records source
shape/shell IDs, `checks_passed`, and `step-material-body-checks.v1` observations.
Nested source/execution evidence explicitly references `CollectionRoot`; nested
status follows the whole request and is not independent publication. On failure,
no nested `model` is published. Original source interpretations qualify the whole
collection as `AcceptedUnderInterpretation` when applicable.

`WriteNative` writes an ordered native BRep array with an empty CSG tree, using
private deep copies to protect snapshot UV data from kernel writer normalization.
Native archive order matches body order; source identities and import qualification
remain in the owning snapshot/report and are not conferred on an arbitrary native
archive load. Archive readers must validate all bodies and their defining geometry.
The CLI supports collection `--write-native`; queries require explicit body selection
through the C++ API.

Current scope is body-only material collections in one part/common representation
context. Assembly transforms and collection-level associated wireframe/construction
ownership remain unsupported. Mixed requests return
`MaterialCollectionAssociatedGeometryUnimplemented` before constructing bodies;
source surface models are never upgraded to material solids. The standalone viewer uses canonical part dispatch for complete collection scenes.


The reusable collection controls use public synthetic fixtures:
`tests/fixtures/two_material_boxes.step` contains two 10 x 20 x 30 mm boxes
with a 100 mm X translation. Each has volume 6000 mm3 and area 2200 mm2;
their centroids are (7, 7, 20) and (107, 7, 20) mm. The source carrier derives
from the public P0 analytic box; the independent generator is
`tools/step_import_native_validation/tests/test_material_collection.py`.
`tests/fixtures/box_and_cylinder.step` adds a radius-3, height-7 mm cylinder
(volume 63*pi, area 60*pi) overlapping the box. Its purpose is to verify separate
body identity and an empty native CSG tree, not a union-volume assertion.
Collection contracts include exact defining coefficients and installed UV after
native archive readback, independent copies, shared source shell identities,
late/second-body cancellation, cumulative budgets, second-body failure and
mandatory-report cleanup.


`ImportPartFile/Bytes` returns `PartOutcome` with a typed `PartModel` variant:
`monostate` on failure, an owning single `ModelSnapshot`, or an owning
`MaterialCollectionSnapshot` on success. It requires the general path. Source
capture/check runs once; the checked inventory selects the construction path.
Single-body associated geometry keeps the existing owner/export behavior; multiple
material bodies use the collection contract above. No retry/reparse or source
interpretation fallback is introduced. Both publication alternatives are prepared
before the one commit point; final variant ownership transfer is nonthrowing.


显式 `CarrierInterpretation::BlankLevelAsCorrectedEncoding`／CLI
`--interpret-blank-level-as-corrected` 可为实现级别声明为空字符串或纯 ASCII 空格的输入
选择现有纠正编码语法。它要求 general 非几何互操作配置；原始声明、错误诊断和未验证
身份保留，成功仍为 `AcceptedUnderInterpretation`。默认及旧 level1 选项不接纳空白声明。
类型、物理单位、几何和原生验收继续执行，不自动改写声明或重试另一编码。


显式 `Options.unitInterpretation=VerifiedExplicitUnitDimensions`／CLI
`--interpret-explicit-unit-dimensions` 支持经精确验证的 AP214 SI/换算长度单位派生槽位引用。
它要求 general 非几何互操作配置，默认关闭。单位指数、换算依赖、原始引用和诊断保留；
维度矛盾、循环、不支持的单位和非正换算因子继续拒绝。精确物理缩放仍由原生单位解析器
验证，容量不足仍为 Indeterminate，不能因维度一致而跳过缩放检查。

源快照记录独立的单位解释证据；面、线框和支撑几何 owning view 的
`AppliedUnitInterpretation()` 读取同一证据。最终模型成功时为
`AcceptedUnderInterpretation`，并报告 `source_unit_declaration_conforming=false`。
修复仍为 None，几何定义、预算、检查、取消与原子发布保持原合同。查看器通过
`--unit-interpretation VerifiedExplicitUnitDimensions` 显式选择，并保留 HUD/扫描资格。
