# C++20 编译期深化代码基线

2026-10-18；对应专题任务：[建立已有编译期保障与候选点的代码基线](../tickets/02-code-evidence-baseline.md)。

## 范围和证据等级

以 `5249915` 为 Git 固定点，检查内核容器/基础值、SM_API、稳定与开发 Python 绑定入口、
STEP 目录、规则、受限有理数、质量配置和相关验证入口。
扫描时工作区存在其他会话的修改；[源码指纹](source-snapshot.json)记录实际查阅文件的字节身份，
其中 `SmApiTessellationParams.h` 属于未提交工作区材料。本次没有修改这些产品实现。

这里的“基线完成”指已为后续取舍建立具体候选、现有保障、消费者、验证入口及未知项，
不是每个模板实例、条件编译组合、数值算法或所有权路径的穷尽审计。
源码事实、编译观察、迁移建议分别记录；建议不是用户已选择的方案。

## 调查结论

| 调查优先度 | 候选 | 当前尚在运行期或尚未表达的固定事实 | 能明确争取的编译期收益 | 主要未知项 |
|---|---|---|---|---|
| 首先讨论 | 固定 schema 目录 | 目录名称、祖先闭包、展开字段、SELECT/枚举引用及派生槽在 Catalog 构造时建立/检查 | 固定登记错误编译失败，固定派生关系预生成 | 最终存储、跨 profile 身份、FieldBinding 地址寿命、编译器资源 |
| 并列重点，先审计实例 | 容器的类型与存储契约 | SmTArray 的字节存储与算法要求主要是注释和使用惯例；存在按值嵌套实例 | 在存储/算法各自 seam 拒绝确实不满足前提的用法 | 哪些实例需改类型或换成有构造语义的存储，不能由一个 trait 代答 |
| 随后讨论 | 固定有理数与质量预设 | 非零 Rational 构造、固定 Quality 工厂及其组合约束仍由运行期代码表达 | 固定参数错误编译失败，常量规范化/派生关系可前移 | 编译期和运行期共享实现、失败诊断、配置身份与数值语义 |
| 较小的独立候选 | 状态、协议及绑定选择项登记 | 固定列表有 constexpr 数据，但全集和消费者间的一致性仍分散维护 | 唯一来源和覆盖/映射检查 | 列表是否真正闭合，未知运行期状态如何保留，原生合同的可编译来源 |
| 有具体调用者再选 | 静态输入尺寸和语义类型 | 创建曲线的最少点数、参数域等有部分可静态表达的前提 | 固定数组调用的尺寸/参数种类错误提前拒绝 | 实际静态调用比例，以及动态输入的验证和借用寿命 |

这是按证据清晰度提出的讨论顺序，不是性能收益排序或实施批次批准。

## 已有保障应保留

- [StepRuleCatalog.h](../../../../source/STEP_PLANAR/src/StepRuleCatalog.h) 的 `LocalRegistryIsValid()`
  和 `GlobalRegistryIsValid()` 是真实 consteval 校验，并有 static_assert 触发。
  它们检查固定身份、内部引用、执行登记、重复和覆盖关系；不等于自动证明实现符合整个 EXPRESS 源标准。
- [StepPlanarTests.cpp](../../../../source/STEP_PLANAR/tests/StepPlanarTests.cpp) 已用 concept/static_assert
  拒绝直接写 Rational 的私有数值成员；[StepSourceTests.cpp](../../../../source/STEP_SOURCE/tests/StepSourceTests.cpp)
  已检查只读快照记录的 const 暴露。
- [SmMapPtrToPtr.h](../../../../source/SMLib/inc/SmMapPtrToPtr.h) 已有 KeyTYPE/ValTYPE 的类型约束断言，
  应读懂它们针对的模板参数约定，不把整个仓库描述为“没有编译期保障”。
- [SmApiTessellationParams.h](../../../../source/SM_API/inc/SmApiTessellationParams.h) 已集中 constexpr 默认值。
  默认值集中不代表任意运行期参数组合已经验证，也不需要为增加 constexpr 数量重写现有声明。

## 固定 schema 目录：收益和边界

[StepCatalog.h](../../../../source/STEP_SEMANTICS/src/StepCatalog.h) 保存 declarations、名称索引、
`std::bitset<256>` 祖先集合、展开字段和 SELECT/枚举等描述；
[StepCatalog.cpp](../../../../source/STEP_SEMANTICS/src/StepCatalog.cpp) 的关键入口为：

- `ValidateProfile()`（扫描时约 94 行）：检查引用的声明/字段、complex 签名顺序与重复、继承 facet 闭包、
  派生槽及字段登记。
- `Catalog::Catalog()`（约 247 行，构造派生数据约 540—591 行）：合并固定 profile 描述，
  检查 256 类型容量、名称重复和继承环，再生成祖先集合和展开字段。
- `Schema(ProfileKind)`（约 704 行）：四个 profile 各有一份函数内 `static const Catalog`，按首次访问构造并保活。

因此不能声称“每次导入都重建目录”。当前可明确移走的是固定 profile 首次构造中的工作与启动时才发现的登记错误；
本次未测它在实际导入总耗时中的比例。

消费者包括 `Checker` 的类型/字段检查、Snapshot 的类型/字段查询，以及 STEP_PLANAR/STEP_IMPORT 的源事实访问。
`FieldBinding` 保存声明字段指针和 origin 视图，迁移须明确静态描述和所有引用的寿命、profile 身份及顺序。
用户允许改变表示和接口，因而不必以复刻现有 vector/string/pointer 布局为目标。

适合的负例包括：固定描述引用不存在的父类型、重复声明、继承环、缺失 facet、无对应字段的派生槽、类型容量超限。
外部 STEP 文件的未知类型、错误字段值、引用身份、源位置、动态单位/几何关系仍须在运行期检查。

验证入口：`source/STEP_SEMANTICS/tests/StepSemanticsTests.cpp` 的 profile 隔离、字段绑定和视图寿命合同，
`source/STEP_PLANAR/tests/StepPlanarTests.cpp` 的规则证据，以及语义/平面 CLI 的诊断、首错和预算回归。
本轮未执行这些产品回归；它们是后续迁移的验证面。

## SmTArray：约束不能替代存储模型

[SmTArray.h](../../../../source/SMLib/inc/SmTArray.h) 的说明限制动态类型，
`SetDataSize()`/赋值等路径实际采用 calloc、memcpy、memset/free；查找、去重、排序、删除辅助器又有各自的操作前提。

以下是实际使用，不只是显式模板实例化：

- [SmBrep.cpp](../../../../source/SMLib/src/SmBrep.cpp) 的分离路径（扫描时约 38433 行）
  使用 `SmTArray<SmTArray<SmRegion*>>` 和 `SmTArray<SmTArray<SmShell*>>`，并调用 SetSize、赋值及 SmArraysDelete。
- [SmFilletExecutive.h](../../../../source/SMLib/inc/SmFilletExecutive.h) 保存按值嵌套的 law sequence edge 列表。
- [SmCurveClass.cpp](../../../../source/SMLib/src/SmCurveClass.cpp) 的 Homogenize 接受按值嵌套的 SmTouchData 数组。
- [SmBucketSort.h](../../../../source/SMLib/inc/SmBucketSort.h) 则明确警告同类按值嵌套会破坏动态类型，选择指针数组。

本机类型探针确认：SmVector3d 是标准布局、非多态、大小为三个 double，但不是 trivially-copyable；
SmTArray<double> 是多态且非 trivially-copyable。`std::vector<int>` 也展示了“非多态但具有非平凡存储语义”的反例。
这些结果不证明某个现存调用已发生运行期故障；它们证明不能把“无虚函数”当作完整内存契约，
也不能未经迁移盘点就在整个类上统一加入 trivially-copyable 断言。

后续需分别决定：哪些是原始字节存储的真实前提，哪些应换成会构造/析构元素的存储，
哪些只需在比较、排序或删除操作上使用受约束重载。concept 检查表达式可用，不自动证明所有权或比较关系的数学性质。

验证应包含合法实例编译通过与不合法实例编译失败，以及增长、收缩、借用缓冲区、嵌套销毁、拷贝后独立寿命等运行期合同。
已有平面区域、classification-copy、核心拓扑/fillet 和 Python 行为测试是消费者回归入口，
不能只为抽出的 trait 写一份照抄实现的测试。

## 固定数值和质量配置

[StepPlanar.h](../../../../source/STEP_PLANAR/inc/StepPlanar.h) 的 Rational 默认构造和 getter 可用于常量表达式；
非零参数构造和运算位于 [StepExact.cpp](../../../../source/STEP_PLANAR/src/StepExact.cpp)，执行约分与容量/非法除法检查。
[StepImport.cpp](../../../../source/STEP_IMPORT/src/StepImport.cpp) 的三份固定 Quality 工厂与 Validate 的身份/组合检查是具体消费者。

本机探针确认默认零值可常量求值，`constexpr Rational(1, 2)` 被拒绝。
固定 Quality、固定单位/前缀描述值得研究编译期构造与关系检查；
`Quality::id` 当前是 std::string，固定描述和用户可变请求是否分型须由后续方案决定。
`StepUnitRational` 是单位量纲解释的另一份受限算术，其容量失败含义和源诊断不能因“统一 constexpr”被抹平。

本次未证明浮点几何求值适合前移，也未证明固定工厂构造是性能热点。
配置身份、源不确定度、容差、修复预算和运行期失败分类仍是需要显式保留或迁移的合同。
验证入口包括 Rational 边界、单位维度/十进制容量测试，以及 `StepImportLifecycleTests.cpp` 的配置、预算和取消合同。

## 固定登记和输入尺寸

- [StepSchemaSelection.cpp](../../../../source/STEP_SEMANTICS/src/StepSchemaSelection.cpp) 已有 constexpr 版本数组，
  但 OID/名称的固定键唯一性、profile 绑定一致性可调查编译期保障。HEADER 解析与源 token/span 保留仍是运行期工作。
- [SmMessages.h](../../../../source/SMLib/inc/SmMessages.h) 与 [SmPyError.cpp](../../../../source/SmPyLib/src/SmPyError.cpp)
  的状态名表分别维护原生状态和呈现名称。本次对当前这两个清单做文本比对：26 个原生名称、26 个表项，未见缺项或额外项。
  这只是当前清单的一致性观察；constexpr 表本身没有证明未来新增状态被登记。若要编译期覆盖，须先建立可枚举的唯一事实来源。
  未知运行期状态的 fallback 不应被误当作“登记表应拒绝编译的源错误”。
- [SmPyCommon.h](../../../../source/SmPyLib/src/SmPyCommon.h) 的 CapEnds/OffsetType 强类型枚举转回原生 int/ULONG；
  对应取值仍在 [SmApiBrep.h](../../../../source/SM_API/inc/SmApiBrep.h) 和 [SmApiPrimitives.h](../../../../source/SM_API/inc/SmApiPrimitives.h) 的参数合同中。
  用户允许破坏性改动，原生/绑定共用类型或描述值得讨论；不能把两边重复同一数字视为编译器验证了合同。
- 所看的曲面求值入口已使用 SmVector2d/SmVector3d 等类型和引用输出，并非全是裸指针/数量。
  更具体的静态尺寸候选是 [SmApiCurves.h](../../../../source/SM_API/inc/SmApiCurves.h) 的三次曲线最少点数及 canonical curve 的度数/节点/控制点关系。
  只有调用处尺寸或参数实际已知时才能前移；Python tuple/list、源文件数量和借用对象寿命仍须运行期处理。

绑定验证入口是 TestErrors、TestCapEndsAndOffsetType，以及原生非法参数和曲线创建合同。
开发绑定的导入顺序和运行期类型注册仍需检查；本轮没有发现足以取代现有稳定/开发模块分工的独立编译期方案。

## 不应计作已获得的收益

- 编译期检查配置宏的条件分支需单独覆盖。`SmTopologyIntersector.cpp` 的 `SM_DEFINED_HASH_ORDER` 分支有
  `static_assert("not implemented")`；独立探针确认这种形式可以编译，字符串是条件。
  同一文件的其他宏分支有真正的 `static_assert(false, ...)`，所以不能由局部形式推出整个宏组合能成功构建，
  更不能声称默认构建已走到该分支。此线索归入后续编译失败测试/配置矩阵的验收讨论。
- `sizeof(TYPE)` 的恒定判断本来就可被编译器优化；换成 if constexpr 应以减少不合法分支实例化为收益，
  不无据宣称运行期提速。
- 已有零初始化指针和 constexpr 默认值，不因增加 constinit/constexpr 关键字就产生新的实测收益；
  constinit 的候选须说明需要防止哪种初始化回退，动态 SmContext 的创建与线程安全仍是运行期问题。
- ranges、span 和 `<=>` 的机械替换不列入首批建议；它们必须对应具体约束、算法或比较语义的问题。

## 编译观察和未知项

[探针说明](probes/README.md)及[本机结果](probe-observations.json)提供可复跑材料：
MSVC 19.50.35728.0、Windows x64、C++20、Release 编译，9 个探针结果均符合记录预期，其中 3 个预期编译失败。

临时 vector 可用于常量求值并生成固定数组，而带分配存储的持久 constexpr vector、bitset 修改操作及非零 Rational 构造在本机被拒绝。
这说明当前 Catalog 表示不能靠整体增加 constexpr 原样前移；它没有预先选定替代容器，也不代表其他平台或未来工具链的结果。

尚未测量候选迁移的编译时间、编译器峰值内存、二进制大小或导入性能；没有做产品重构或重跑完整模型回归。
后续固定目录、模板契约、固定数值三张票已有足够事实进入方案讨论；标准解释或平台差异需要时另开具名研究票。
