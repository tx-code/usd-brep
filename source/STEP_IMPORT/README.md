# STEP_IMPORT（内部 C++20）

## 显式装配导入 C++ 接口

`StepAssemblyImport.h` 提供 `ImportAssemblyBytes`／`ImportAssemblyFile`。调用者显式设置
general 模式、质量配置和 `AssemblyLimits`：occurrences、instances 必须为正，深度从根的 0
开始计数；实例上限包含根。当前 occurrence 上限不得超过源记录预算，instances 和 depth
实现上限各为 1,000,000。无默认放宽；修复、独立定义选择／源错误隔离暂不组合。

接口对全部声明定义及 usage 根执行源归属、单位／坐标系、原生定义构建、实例展开和
完整报告检查，沿用两次报告序列化及 `Execution::Commit` 的唯一成功／取消裁决。
失败先释放未发布模型；报告预算不足不能返回模型。源解释假设保留为
`AcceptedUnderInterpretation`，不因成功构建升级为已验证协议身份。

成功的 `AssemblyOutcome::assembly` 是 owning 只读快照：`Body(definition, body)` 返回
定义本地毫米坐标下的已有 `ModelSnapshot`，重复实例共享该定义。`Instance(i)` 返回独立
数值副本，含定义索引、父实例、源 usage、所属根及到该根坐标系的旋转／平移。
不同根不存在自动推断的公共世界坐标系。清空 outcome/report 不会丢失来源或变换证据。
通过 Body 获取的建模副本仍在**定义本地坐标**；没有自动实例化建模副本或装配存档接口。

当前范围为已登记源 profile 的 NAUO/CDSR、直接表示项中的 IDT 三维轴系，以及已有
单体／材料集合构建能力；surface-model 角色保持原义。源归属不完整、未支持关联几何等
仍明确拒绝。该 development 接口不授予完整 EXPRESS 或连续 UV／变换误差证明。
现有自动文件导入、CLI 和 GUI 尚未切换到此入口，须显式调用该 C++ API。

## 装配变换的内部数值候选

`StepImportAssemblyPlacement.h` 的 `EvaluateAssemblyPlacements` 消费 owning 的
`AssemblyInputs`，按其已检查的上下文身份复用既有精确长度换算器。每个换算链的原始因子、
精确比例和单位身份保留；不接受调用者随意提供的尺度，也不借用另一个上下文的单位。
当前仅求值已绑定的 `ITEM_DEFINED_TRANSFORMATION` 两端 `AXIS2_PLACEMENT_3D`。
与已有几何构建共用源数值解析、范围检查及 `build_axes/FIRST_PROJ_AXIS` 实现。

两端原点各自换算为毫米，方向比保持无量纲。输出是 **child-mm → parent-mm** 的行矩阵
`R_parent * transpose(R_child)` 和平移 `o_parent_mm - R * o_child_mm`；不是世界变换，
不能再乘一次单位比例。源关系反向由绑定身份决定，重复 usage 保留为不同记录。
坐标系缓存同时使用 item 与 context 身份，避免同一 item 在不同单位上下文间串用。

内部候选保留源输入所有权、精确单位证据及每个 usage 的数值矩阵。有限性和旋转正交残差
只提供浮点算术检查，不是毫米验收预算或连续几何误差证书；不会把矩阵拟合回旋转。
不支持的 frame 类型、非正换算、范围溢出、实例数限制或任一合作取消都丢弃整个候选。
该步骤不执行原生调用、不展开嵌套实例、不批准 pending 元数据、不建立完整定义归属，
也不授予装配导入成功或新增 CLI 接纳路径。完整源闭包、定义几何、嵌套实例、原生验证
及最终发布仍须分别完成。

`ExpandAssemblyInstances` 在显式选择的已声明 usage 根下展开数值实例候选。每个根使用自身
毫米坐标系，不推断不同根之间的位置。重复定义沿不同父路径保留不同实例；以父实例索引
和源 usage 记录路径，不复制完整路径数组。迭代展开限制实例总数（含根）和深度，防止
无环定义图的重复使用导致指数级展开。未完成的分支不会被截断后当成成功。
矩阵按父实例变换乘局部变换组合；无效根、重复根、数值范围失败及合作取消全部拒绝
完整候选。此步骤仍不构建 BRep、不批准源闭包、不提供选根后的源错误隔离，也不改变
现有 CLI 的装配接纳范围。

`BuildAssembly` 是内部、未发布的原生候选构建器，只接受通过完整源闭包的
`AssemblyDocument`。每个几何定义复用 `BuildGeneral`／`BuildMaterialCollection` 构建一次，
重复实例引用定义并保留实例树变换；不把位置写回原始定义，不做 Boolean union。
显式放置容器保留定义／实例节点而不创建空实体，也不声称完成了容器的数值积分。
定义及实体报告复用现有观察、共享调用预算和失败回收机制；后续成员失败时整个候选丢弃。
当前候选要求 general 模式、无修复／选择隔离，关联线框和构造几何仍明确拒绝。
调用者持有原生串行锁并负责最终报告和提交点。本内部返回值不等于已发布导入结果；
公共装配入口在其上完成报告和提交；CLI 接纳与实例存档格式仍待接入。

`CaptureAssemblyEvidence` 把完整候选的定义／形状身份、上下文精确单位证据、局部变换及
实例父路径提取为只读元数据；不保留任何原生 BRep，所以失败报告不会阻止候选清理。
`SerializeAssembly` 复用同一个有界报告写入器，输出独立的装配 development 格式和
逐定义检查范围。源哈希不匹配、成功状态缺少定义检查或字节预算不足时整体失败并清空
输出；既有非装配报告格式不变。公共入口在最终提交前完成该报告；仅调用内部序列化
函数本身仍不代表已通过请求提交点。

## 角环面／自交环面支撑的原基提取

通用开发导入可显式选择已有 `restrictUnusedSupportDomains`（CLI／查看器
`--restrict-unused-support-domains`），处理 `major_radius <= minor_radius` 的有限、
已证明不穿过旋转轴的裁剪区域。默认路径仍保留原来的环面范围保护。
此能力没有授予完整奇异环面、缺失区域证据、源错误修复或闭合表面升级为材料实体的资格。

候选保留完整源解析定义、单位、方向及共享三维边。先保留原生解析环面和其角度转换器，
再从同一完整 NURBS 基构建私有候选，避免解析投影产生“曲线图像正确、边参数配对错误”
的派生 UV。普通环面在该显式配置下也可在经逐值核对的原基副本上投影，测量仍针对真实
目标曲面；不改其解析目标类型。没有拟合或改写定义曲线／曲面。

图选择只改变一个完整周期的计算坐标窗口。端点评分和切口选择不构成接纳证据。
提取前必须有唯一显式外环，派生 UV 在展开图中按内核参数分辨率闭合且不借用周期跳转；
正权重控制凸包覆盖完整边界。原生成线在实际保留区间内须有严格正或严格负的半径界，
并与轴保持大于定义误差上限的余量。正／负两个正常参数分支都保留源面朝向。

随后仅对普通 NURBS 原基执行子域提取，用向外区间公共节点细化与 Bernstein 界，
对整个实际保留矩形证明相同 native UV 的曲面差异不超过显式 `definitionMm`。
不调用会重新生成并改变同参映射的解析面裁剪。安装时恢复原派生 UV 副本，不增长工作容差；
最终配对误差、区域／拓扑、有效性、量测及全请求发布门槛仍须通过。
连续证书只覆盖曲面原基提取；现有开发路径的 UV 配对／积分观察不因此升级为连续证明。

`ModelSnapshot::SurfaceNormalizations()` own 来源面／曲面、目标面索引、原／保留 native 域、
原生计算图的解析角度域、生成线半径界及连续差异证据；角度域不是改写后的 STEP 参数。
原解析原生定义也由模型所属上下文保留。
清除 outcome/report 不会丢失这些内容。报告按需输出 `surface_normalizations`，
失败报告中的私有候选观察不代表已发布模型。未建立的图、控制凸包、规则性或连续证据
分别报告数值不确定或未支持；资源限制及取消仍释放整个未发布请求。

当前不组合晚期周期分面、极点拓扑改写或显式修复。源三维参数、身份、单位和角色不变，
本操作属于目标表示规范化，仍为 `Repair=None`。建模副本及原生存档只含规范化后的目标
BRep，不继承源解析定义、STEP 资格或归一化报告；需要源语义时须保留 owning 快照。
内部 C++ STEP 接口布局扩展，库及消费者须一并重建；内核 ABI 不变。

## 完整圆环的目标分段与圆柱／圆锥图切口

通用构建可把圆柱及有限圆锥上的两条完整名义 `CIRCLE` 边界打开为一个目标图环。
圆锥两端半径必须有限且为正；半角通过已有单位绑定换算为物理弧度，切口按轴向毫米计算，
不把 STEP 的母线距离参数等同于目标轴向 V。球极点、穿过圆锥顶点及任意边界没有因此获得支持。
源闭合表面的角色不变，通用材料构建的既有范围也不扩充。

两条圆环起点相位不同时，先在任何面构建前形成全局分段计划：保留原顶点，在另一圆的
**原生原基参数**处加入派生顶点，并让全部相邻盖面／侧面共享相同子边。每个子边保留完整
原解析类、齐次控制网、节点、自然／STEP 参数域及解析辅助数据，只限定自己的 edge interval；
不裁短、重建、重设圆相位或重参数化定义曲线。各子区间必须逐端点精确相接并覆盖完整原使用。
同一源圆上的多个切点也在全局排序后共同展开，派生点不伪造源实体身份。

切口沿固定经线／母线。新点反求的数值残差受原 `definitionMm` 约束；无法区分的近端参数
使用独立的内核参数分辨率保护并拒绝，不能用毫米误差预算替代。原源点逐值保持，子边及
全部目标库存仍通过原有 UV、邻接、容差、原生有效性、量测和最终提交检查；没有放宽质量预算。
派生 UV 仍是既有开发数值观察，不因区间覆盖正确而获得连续提升误差证书。

`ModelSnapshot::TopologyNormalization()` own 完整目标面／边／顶点来源。实际一对多使用还
保留 `sourceUseSegments`：原面、边界、环、有向边、边身份及方向，对应目标子边／顶点和
原基子区间。只清除 outcome/report 不会丢失；报告在 `topology_normalization` 中条件输出
`source_use_segments`。相位对齐的新圆锥图也保留派生切口来源，无分段时不制造使用分段记录。

这是已有等价目标表示规范化的扩展，无新自动修复选项。合法源保持 `Repair=None`；源中的
多外环错误仍须已有显式、SHA256 绑定的圆柱带角色修复，原错误和原文继续保留。新图规划
与晚期周期分面、支撑域限制、面朝向修复或球极点的组合尚未建立时明确拒绝。所有所选目标
仍共同提交，取消、原生／报告预算或任一后验失败均释放未发布候选。

内部 C++ `TopologyNormalizationInfo` 布局扩展，STEP 库和消费者需一起重建；内核 ABI、
原生存档格式及原生存档／建模副本不继承 STEP 资格的规则不变。

## 显式选择与未选定义的局部源错误隔离

`Options.sourceErrorIsolation=SourceErrorIsolationLimits{maxFailures,maxInfluenceVisits}`
必须与原件 SHA256 绑定的 `definitionSelection` 一起明确请求。CLI／查看器使用
`--source-error-isolation FaceBoundsClause --max-isolated-source-failures N
--max-source-influence-visits N`，并提供既有 `--selection-source-sha256 HASH`
及重复的 `--select-definition ID`。两个预算必须为正，不能与修复请求组合；无生产默认值，
固定选择／隔离计划继续仅限一个明确 STEP 输入，不能用于目录扫描。

首版 `SourceFaceClauseSelection-0.1` 只覆盖固定 AP214 源配置中，**未选独立定义**实际失败的
`FACE.WR2`。完整解析、类型、单位／几何必要依赖、全部原始定义的其余产品检查、单位上下文、
全源归属／反向挂接与元数据最终审批仍须完成。装配、未知记录、解析错误、所选坏面和其他
未获隔离资格的源错误保持阻塞；共享坏面在任一所选使用中仍失败。源字节、属性、方向、单位、
几何和容差不改写，隔离不是修复，也不认证完整 AP。

随后分别证明所选内容的依赖和错误的完整反向边界。所选属性及触及所选目标的显示、覆盖样式、
图层、管理记录作为完整原件纳入影响，不过滤它们的混合目标。声明的产品／定义／形状归属桥
覆盖原始前向引用未表达的反向所有权。错误若触及所选内容、存在共享使用或反向使用无法归属于
已证明的未选岛，明确拒绝。仅凭前向不可达不足以通过；未选面上的孤立显示也可能因缺少归属
证据而被拒绝。所有证明访问同时计入独立影响预算和既有语义访问预算。

证明后才发布只含所选合格定义的源 `ProductDocument`；其 `PartCount()` 不再代表全部声明数。
`SourceSelection()` 保存完整声明清单、所选／未选身份、原始错误、原件身份、请求预算和证明
访问计数；各体的源只读快照也 own 同一证据。`Source()` 仍保留完整原始记录。
普通源检查／原生选择的既有合同不改。源阶段声明 `CheckedSelectedDefinitions` 或原有数值
`Deferred`，报告明确 `SelectedDefinitionsOnly`，不能将所选原生成功计为整个文件通过。

所选原生体仍执行全部既有构建、查询资格及后验，并共同提交。有实际隔离时返回
`AcceptedWithSourceIsolation`（CLI 13），独立的源解释仍保留；无实际源错误时不制造该状态。
该配置不扩充通用原生构建的形状范围；合格源中的闭合表面仍保留 surface-model 角色，
既有仅材料实体的通用构建路径继续明确拒绝，不能因闭合或隔离成功自动升级为实体。
任一所选体失败、取消、预算或必需报告不足均释放未发布候选。查看器 HUD 区分选入定义数和
隔离错误数，继续保留面／边／顶点测量与选择。原生存档及建模副本不继承 STEP 隔离资格。
内部 C++ 源／原生接口、结果枚举与消费者需一起重建；内核 ABI 不变。

## 精确有向圆柱带的边界角色修复

`Options.cylindricalBandRepair`／CLI 单文件显式计划可选择 `ExactOrientedCylindricalBand-0.1`。
请求必须绑定原始 SHA256，逐项给出定义／形状／面及原有两个 `FACE_OUTER_BOUND` 的身份，
并显式提供最大面数、边界改动数与证明运算预算。默认仍为 `Repair=None`，不按错误自动选计划。
该配置暂不与面朝向修复、定义选择、限制支撑域或额外周期分面请求组合。

源层真正执行原 `FACE.WR2` 谓词并保留失败。只有计划与全部所选源使用匹配，才允许继续其他
类型、单位、依赖及归属检查；输出为 `DeferredExplicitRepair`／`ExplicitRepairPending`，
不是合格源声明，也不是非几何属性例外。缺失／重复／错误／不必要计划仍拒绝，任何其他源错误
保持原行为。原记录、类型、顺序、数值、单位、方向、边／顶点身份均不改写。

原生分配前，用固定 4096 位有界精确有理数证明：两个环各由一个完整闭合 CIRCLE 边构成，
圆心精确共轴，圆法向平行圆柱轴，半径精确相等且为正，高度严格分离；组合源边、使用、边界与
面朝向后，下环沿正角向、上环沿负角向。此有向边界在 `S1 × R` 上唯一确定有限圆柱带，
而非它的补集。不通过采样、浮点 epsilon 或容差增长获得该结论；不满足精确关系、资源不足或
其他曲面／裁剪边界均不授予该修复资格。

目标只中和两个互相矛盾的显式外环提示，保留全部原有环与使用方向，再由现有区域分类验证目标。
既有隐式经线图切口继续保留来源映射；需要尚未支持的圆环分割或 UV 超预算仍拒绝。
在 UV 创建前私有复制原生三维定义，最后逐对象比较全部顶点、曲线／曲面定义、参数域、
控制网、权重、节点、解析参数和工作容差，要求完全一致。未声明移动／拟合／缝合。
这是有向有限带的独立源区域证明及原生后验，不声称所有源 AP 规则或连续 UV 误差都已证明。

完整模型返回 `AcceptedWithRepairs`（CLI 12）。`ModelSnapshot::CylindricalBandRepairs()`
own 原始错误、原件身份、逐面证明身份与三维／容差比较结果；清除 outcome/report 不丢失。
`Source()` 仍能读取原始两个外环类型及未消除的源错误。建模副本和原生存档不继承 STEP 修复资格。
所有体／定义仍共同提交；取消、截止、证明／原生／报告预算不足或任一后验失败均释放未发布候选。
源原始错误计入源诊断总预算；精确证明运算与原生对象比较在整个请求累计，逐控制点只读访问仍
受源输入／对象容量约束，不冒充原生求解器调用次数。

参数为 `--band-repair-source-sha256 HASH --max-band-repair-faces N --max-band-bound-edits N`
`--max-band-proof-operations N`，重复 `--repair-cylindrical-band DEFINITION:SHAPE:FACE:BOUND0:BOUND1`。
查看器以“周期边界角色”区分朝向修复；固定计划不能用于目录扫描。
内部 C++ STEP 源／原生接口与消费者需一起重建，内核 ABI 不变；本开发配置没有生产默认预算。

## 按已登记组成项名称解释复合实体顺序

通用互操作请求可显式选择
`Options.complexMappingInterpretation=step_semantics::ComplexMappingInterpretation::RegisteredFacetNames`，
CLI／查看器使用 `--complex-mapping-interpretation RegisteredFacetNames`。默认 `DeclaredOnly` 继续拒绝
非升序组成项；此配置不随单位、协议、互操作或修复选项自动启用。

源层必须证明完整、唯一、已登记且无子型冲突的组成项集合；各属性仍按原始组成项名称、位置
逐项检查，后续单位量纲、依赖、归属、拓扑及原生验收均保留。只排序临时签名索引，不修改源
顺序、参数、单位或字节，也不新增实体签名。细节见
[源层合同](../STEP_SEMANTICS/README.md#explicit-registered-complex-facet-interpretation)。

实际采用解释的完整模型返回 `AcceptedUnderInterpretation`，原始排序错误、记录身份和索引对应
保存在 owning 源快照及报告中；清除 outcome 后仍能读取。未实际采用则不产生解释资格。
显式修复时保持 `AcceptedWithRepairs` 并记录独立的源解释，不改修复许可或几何预算。
后续任一必要检查失败仍不发布模型。源诊断／声明产品图可以保留，但不得当成完整导入结果。
这是可验证的特定解释路径，不是通用语法恢复、源错误隔离或对坏单位／缺失几何的放行。
内部 C++ STEP_SEMANTICS、Options 和消费者需一并重建；内核 ABI 与原生存档格式不改。

## 显式选择独立零件（源检查完整通过）

`ImportModelFile/Bytes` 与 `ImportDocumentFile/Bytes` 可显式设置
`Options.definitionSelection = DefinitionSelectionRequest{originalSha256, definitionIds}`。
ID 为源 `PRODUCT_DEFINITION` 实体身份，不是名称、数组序号或形状 ID。请求绑定原始字节
SHA256，非空且不得重复；不存在／过期身份在原生分配前拒绝。未设置时仍选择全部定义。
原生 CLI 与查看器支持 `--selection-source-sha256 HASH` 和可重复的 `--select-definition ID`。

该配置的资格为 `SourceCompleteNativeSelection-0.1`：先对完整文件执行现有类型、单位、
依赖、反向归属及独立产品结构检查，只有发布完整 `ProductDocument` 后才选择原生构建范围。
装配、变换、未建立的独立性或任一源检查失败仍拒绝；本配置不提供局部语法恢复或源错误隔离。
共享几何与上下文保留原有定义作用域，不改源数据，也不通过前向可达性推断其他记录无害。

所有所选定义按源顺序构建并共同提交；任一所选定义失败、取消、预算或必需报告不足，均无模型。
未选定义的原生构建不会运行，记录为 `not_requested_definitions`，不声称它们原生有效。
选择一项也返回 `DocumentSnapshot`，其 `Selection()` 返回 owning 选择证据；
`Source()` 仍含完整文件和全部源定义，原生 `PartCount()`／体的定义序号只对应所选集合。
清除 outcome/report 不会丢失选择身份。建模副本和原生存档不继承 STEP 选择／来源资格。

显式朝向修复可与选择组合，但计划中的每个改动都必须属于所选定义，不能修复未请求的兄弟定义。
原生工作与证明预算仍在整个所选请求累计。独立文档的线框／辅助几何构建限制继续适用于
每个所选定义；未选定义的此类原生内容不被冒充为已导入。旧单体／集合入口拒绝选择配置。
查看器仅允许单个明确 STEP 文件采用固定选择，不用于目录扫描；HUD 和本地结果标明选择范围，
不能将完整所选请求的成功计作整个原文件的完整原生导入。内部 C++ 布局扩展，消费者须一并重建。

## 显式面朝向修复（开发切片）

`Options.faceOrientationRepair` 可选择 `FaceOrientationZeroGeometry-0.1`：调用者明确提供
原文件 SHA256、零件定义／形状／面身份、预期源 `same_sense` 和目标朝向，并分别给出
最大改动面数、证明控制点工作区及全请求累计证明运算预算。缺省仍为 `Repair=None`；
没有生产默认预算，也不根据错误消息、源 uncertainty 或文件名自动选择修复。

首版限定材料 BRep 中具有一个显式外环的面，目标为原生普通多项式 B 样条支撑面；
以整个自然参数矩形上的投影强单调性证明单值性。三维边与派生 UV 必须采用受支持的
B 样条／有限直线参数约定，且修前、修后逐边完成连续区间提升误差证明。
有理支撑面、解析曲面子类、孔、未证明的多值／折叠、任意 healer 或定义几何拟合没有因此获得支持。
该策略目前不能与周期分面或限制支撑域请求组合。其他未修复面的原能力保持。

源只捕获、解释一次。先在私有候选上执行原严格原生构建，失败集合必须与显式计划完全
匹配；方向诊断本身不足以授权修复。随后以同一源快照和受限的有效朝向覆盖重新构建，
逐源身份比较全部顶点、边／曲面定义、参数域、控制网、权重、节点及解析参数，要求完全
相同且工作容差不变；再完成原有全部后验与修后连续提升证明。前一候选在最终就绪前释放。
零位移证据仅指这些定义数据，不声称原无效材料区域与修后区域等价；材料朝向变化是明确的修复。

零件定义／形状／面三元身份隔离共享几何的各次定义使用；修复一个定义不会修改兄弟
定义的源字段。全部选定体／定义继续全成全败。原生工作、截止、取消和报告预算不重置；
证明运算预算跨所有体、所有修前／修后证明累计。报告中的运算计数对应已完成证明操作；
中断中的操作不冒充完整计量。工作区不足、证明不成立、后验失败或必需报告不足均不发布模型。

完整结果返回独立状态 `AcceptedWithRepairs`（CLI 12），原 schema／载体／文本／单位解释
仍分别保留，不归入普通 `Accepted`。`ModelSnapshot::Repairs()` 和
`FaceView::OrientationRepair()` 返回 owning 证据；清除可变 outcome/report 不会清除资格。
`Source()` 仍返回真实原始字段和字节，报告分别保存原失败、有效朝向、进度、预算与证明。
存档只编码修后原生 BRep，建模副本和回读模型不继承 STEP 修复资格，须另留原件与报告。

CLI 显式参数为 `--repair-source-sha256 HASH --max-face-orientation-edits N`
`--max-repair-proof-controls N --max-repair-proof-products N`，并重复
`--repair-face-orientation DEFINITION:SHAPE:FACE:ORIGINAL:EFFECTIVE`（朝向为 `T/F`）。
身份来自实际 inspect；过期哈希、错误作用域、重复／空计划或已有效模型不能变成一次修复接纳。
本切片扩展内部 C++ Options/Report/快照布局与结果枚举，库和消费者需一并重建；内核 ABI 不变。

## 诊断与通用入口

通用后验诊断可附带 `classification=NativeFaceLoopOrientationFailure`。
它表示完整已记录的失败集合仅含 `SmTrimmingTools::CheckFace` 的面环方向谓词；
`ValidatePointers` 同时运行几何检查，原 `GeneralPointerValidation` 入口键不能用来断言
存在断指针。匹配依据为原生报告的 reporter／list／owner／predicate 身份，不解析消息文本。
混合、未知或超过有界明细范围的失败集合保持原分类。原状态、入口键、消息、检查顺序、
未运行检查标记、预算和候选清理保持；成功报告字节不增加字段。
新字段同样计入原有必需报告字节上限；报告不足仍拒绝发布。
这只是目标检查观察，不能据此断言源错误、授予修复许可或忽略失败。严格路径继续 `Repair=None`。

通用文件入口 `ImportModelFile/Bytes` 捕获、检查源一次，按已资格化清单返回
`FileOutcome.model` 的单体、单定义实体集合或独立多定义文档 alternative。
单定义继续使用既有构建路径，保留其全部附属线框／支撑几何；多定义复用下述文档构建。
它不在单零件失败后重读或重试，不推断装配，不交付部分定义，也不扩大质量或修复权限。
旧 `ImportFile`、`ImportPartFile`、集合及显式文档入口保持选择合同。
新入口要求显式 general BRep 质量配置；源报告为 owning 的产品文档证据，单定义的
原生模型仍持有其独立语义 view。库与消费者须使用匹配的内部 C++20 头文件和静态库。

独立多零件可显式调用 `ImportDocumentFile/Bytes`，CLI 使用
`--independent-products` 并选择通用质量配置。源只捕获、检查一次；每个定义保留自身
几何归属、物理单位及源身份，再依次复用单体／实体集合构建。所有候选共用原生调用、
截止、取消与报告预算；全部定义通过后，完整报告仍生成两次并经同一提交点交付。
后续零件失败、报告不足或取消均释放此前未发布的模型，不返回部分文档。

`DocumentSnapshot` 分开保存定义与体，`Part(i)` 返回原有单体／集合 owning 类型，
`BodyPartIndex`、`SourceDefinitionId` 和 `SourceShapeId` 保留定义－体－源对应。
不同定义可引用同一源几何，不能按源形状 ID 或空间重合去重。查询和建模副本沿用
每个 `Body()` 的既有合同。原生存档按定义／体顺序写入独立 BRep 数组、空 CSG 树；
不执行并集，存档本身不继承 STEP 来源资格或定义标签，须保留 owning 快照／报告。
各体的毫米坐标仍属于各自定义的源坐标系。本接口不推断定义间装配位置，不提供跨定义
距离、合并包围盒或总材料体积资格；查看器若排列这些定义，须标明是展示布局。

该路径当前限定独立材料定义；装配使用、变换和上下文相关形状关系仍明确拒绝。
文档级独立线框／辅助几何导出尚未建立资格，遇到这些物理内容会在分配原生体前拒绝。
`ImportFile`、`ImportPartFile` 与显式单零件实体集合接口保持原选择合同，不自动转入
多定义请求。本轮改变内部 C++ Snapshot/Outcome/Report 布局，静态库和消费者需一起重建；
内核 ABI、几何预算、源字节及 Repair=None 保持。

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
包含于对应 UV 自然区间。每个目标 UV 环还须在同一二维图按内核参数分辨率闭合，
不能按周期取模，也不能用三维误差预算代替缺失的 UV 连接；未建立闭合时，在复制或
安装候选前返回 `PlanarChartClosureUnestablished`。这些条件同样适用于普通 B 样条支撑。
边界控制凸包包围有界参数区域，孔边界也完整保留；
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
observer的非分配异常返回`ExecutionCallbackFailure`（BuildFailure）；意外`bad_alloc`向调用者传播，不能伪称已配置限额耗尽。

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

## Internal grouped construction

集合／独立定义的 Import report 观察生命周期由私有
`StepImportObservation.h` 的 `ObserveBodies`／`ObserveDefinitions` 集中处理：
建立 child report、继承源上下文、结算定义计数、汇总积分以及失败收尾。
成员回调同时负责构建和将成员纳入候选；只有回调成功才记录 `checksPassed`。
最终封装回调在积分汇总后执行，仍处于同一失败保护内。同步回调只在调用期间借用，
不保留回调，也不增加原生调用、检查点或执行预算。

`Execution` 仍是唯一请求账本，定义计数采用请求根计数的区间差值。体计数保留原生
构建／修复状态机的叶观察语义；`originalAttempt` 是重叠 witness，不能把任意报告
子树相加，也不能用根差值覆盖体计数。成员错误返回和意外异常都会保留已完成检查、
标记应丢弃的观察并释放未发布候选；观察收尾不执行原生销毁或发布。

上述两个批次入口、集合／文档构建及对应发布入口使用私有
`Result<T> = tl::expected<T, Failure>`，错误类别和诊断直接向上传递。
原生构建、计划证明、单位／分段／规范化及修复中的业务 `Failure` 均通过结果返回，
入口按错误值完成诊断和失败收尾，不再捕获这一类型的异常，也不把错误值重新抛出。
这不是全库禁用异常或全链路 `noexcept`。公开 Outcome、快照及报告格式不暴露
`tl`。Execution 继续仲裁最终执行结果，Publication 保留两次报告生成和原子发布协议。

依赖从仓库内固定版本构建，无配置期下载；来源及校验和见
[tl-expected 清单](../../third_party/tl-expected/UPSTREAM.json)。
回归范围及定向变异方法见 [嵌套观察测试](tests/NestedObservation.md)。

定义选择也直接返回 `Result`，保留源顺序、owning 选择收据以及选择前／完成后的检查点。
Publication 的接纳、报告容量和最终提交检查结果直接传播至入口，由公开 Outcome 的
拥有者记录诊断及完成失败收尾。Execution 的检查点、原生调用计费、修复登记／证明预算和
提交仲裁均返回结果。原生构建、精确单位算术、UV、周期处理和连续证明的必要回调链直接
传播错误，既不丢弃错误值，也不把执行拒绝转成数值证据未建立。内部结果函数标记
`[[nodiscard]]`；拥有候选的构建边界同时处理错误返回与异常退出的清理。
[执行协议回归](tests/Execution.md) 固定检查／计费顺序、拒绝诊断、最终 guard 与 CAS，
公开生命周期和修复回归核对完整报告及原始尝试的重叠计数。

平面原生构建的源对应、所有权、拓扑后验检查和保守数值证明拒绝也直接返回业务错误，
保留原诊断、证明观测及失败候选清理。公开查询异常与底层数值算术的既有分类保持独立；
验收边界和变异要求见 [业务失败回归](tests/BusinessFailures.md)。

严格方向修复尝试的私有 `ConstructionResult` 区分普通 `Failure` 和携带原生候选的
`OrientationFailureCandidate`。修复协调者显式消费 witness；普通构建边界先释放其
候选所有权，再返回普通错误供报告交付。原始尝试的重叠计数、诊断和后验比较保持原合同。
节点多重度及权重直接返回拥有数组的 `Result<SmTArray<T>>`；自有缓冲通过移动构造
进入结果，消费者检查成功后直接借用其中的数组，不产生包装／提取复制。
容器的借用缓冲、上下文及分配失败合同见 [移动构造回归](../../tests/kernel_containers/README.md)。

最终裁剪的参数检查及量测返回 `tl::expected<T, TrimMeasurementError>`，区分参数域、
节点读取、求值及非有限距离拒绝。该错误类型不携带执行取消或请求预算错误；原有派生 UV／
支撑域回退只消费局部量测拒绝，检查点的 `Failure` 仍直接传播。量测中的节点跨度、每跨度
八个子区间、实际同参数求值及参数舍入保护保持原合同。通用构建边界保留原来的
`BuildFailure`／`NativeReturnOrConstructionFailure` 诊断、消息、空源记录和阶段。
回退中保留的 `runtime_error` 捕获仅兼容底层原生调用仍可能抛出的异常，普通量测拒绝不再抛出。
回归及变异范围见 [业务失败回归](tests/BusinessFailures.md)。

原生构建、通用材料构建和独立曲线定义的原生状态检查通过 `CheckNativeStatus` 返回
`Failure`，保留平面／通用构建各自的原消息前缀及导入入口的原错误分类。必要的线／面系数
读取和包络汇总直接传播结果，原算式、表示读取次序和执行计费不变。工厂的非空输出在状态
检查前进入 RAII 所有权，失败和后续报告分配异常均不会遗失未发布对象。公开查询／存档
仍直接检查原生状态并保留既有异常合同，不将内部错误值重新抛出。
这些原生／量测操作拒绝带私有 `ConstructionOperation` 种类：严格修复协调者保留原先立即
退出时的未完成观察，不额外填写 `originalAttempt` 状态、诊断或丢弃收据；普通业务／执行
拒绝仍按原合同记录严格尝试失败。该种类不构成朝向修复 witness，也不改变公开报告布局。

通用方向归一化与坐标框架构造返回结果，材料边／曲面、独立曲线、辅助框架、接缝及球极点
调用链直接传播其数值拒绝。有限源方向的范数溢出或近平行投影无法建立原生框架时，仍为
`Indeterminate`／`GeneralDegenerateFrame`，保留原消息、空源记录、D5 和严格尝试的未完成观察。
范数、双叉积投影、归一化算式及源参数约定均不变；不通过重缩放扩大已支持的数值范围，
也不将其归入配置预算耗尽。原生计费、已执行／未执行校验的记录和候选释放保持原合同。
标量文本与坐标转换的其他数值域拒绝仍待单独迁移。

迁移目标是消除项目主动创建的异常，以显式结果表达业务、配置及内部失败，
而非按异常类型名筛选：
项目显式抛出的 `std::invalid_argument`／数值错误同样属于待迁移范围。
当前公开配置／查询及存档异常合同、尚未迁移的几何数值域拒绝、底层数值不确定性／容量哨兵和共享 JSON writer
内部超限终止仍未迁移；数学证据未建立与工作空间不足继续保留各自的证明结论，
不能与请求取消或业务失败混同。
STL／运行时实际产生的异常及外部回调异常另有传播或边界转换合同；
不能用全局 `noexcept` 或禁用异常编译选项代替逐层迁移和验收。
