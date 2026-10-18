# STEP 固定目录的编译期迁移方案

2026-10-18；[哪些固定目录应在编译期验证并生成派生数据](tickets/03-static-catalogs.md)的技术展开。
本文件确定实施方案和验收目标；本轮尚未迁移产品代码。

## 范围与收益

选中 `STEP_SEMANTICS` 的四个固定目录：AP203 planar、AP203 general source、AP214e3 source、AP214 MBx source。
覆盖 [StepCatalog.cpp](../../../source/STEP_SEMANTICS/src/StepCatalog.cpp) 及
`StepAp203GeneralCatalog`、`StepDateTimeCatalog`、`StepAp214PresentationCatalog`、`StepAp214MbxCatalog`、
`StepRationalSplineCatalog` 五个扩展登记文件。只迁主表会留下运行期组装和未检查的 profile 差异，不满足本方案。
包含调查期间合入 `713bf00` 的 AP214 曲线样式描述及 `FieldKind::SizeSelect`；其源 width/font/unit 检查仍在运行期执行。

| 收益 | 改前 | 必须交付的改后结果 |
|---|---|---|
| 错误发现提前 | 固定描述的重复、引用、继承和映射错误在相应目录首次构造时发现 | 四个目录全部强制常量求值，固定描述错误使构建失败，不依赖运行期是否选到该 profile |
| 消除固定初始化 | 每个 profile 首次使用建立字符串/容器、名称表、祖先闭包和展开字段 | 固定数据直接存于静态存储；取得目录不分配、不执行 DFS、不构造 hash 表或重跑元数据验证 |
| 消除查询临时字符串 | `Find()`、部分固定类型查询将 string_view 转成 string 查 hash 表 | 通过 string_view 查静态索引；固定 SELECT、排斥组和派生槽提前解析为整数引用，无临时 string |
| 消除字段维度副本 | `AttributeAt()` 复制 nestedAggregates vector，绑定保存时又复制 AttributeView | 内层维度借用不可变静态 span，查询和绑定不再复制这份元数据容器 |

首次目录构造目前每个 profile 只发生一次，不能把它当作每次导入的节省。
临时字符串是否分配依赖长度和工具链；完整导入是否加速、二进制是否更小，都须前后测量。
原始输入解析、逐 record 绑定、source Value、population、诊断和数值验证仍需运行期工作及存储。

已实测的[当前分配基线](evidence/catalog-baseline/README.md)如下，均为 MSVC x64 Release、每项新进程：

| Profile | 类型数 | 首次 Schema 分配请求 | 累计请求字节 |
|---|---:|---:|---:|
| AP203 planar | 72 | 1,185 | 99,821 |
| AP203 general source | 132 | 2,222 | 204,813 |
| AP214e3 source | 114 | 1,963 | 185,140 |
| AP214 MBx source | 120 | 2,069 | 193,434 |

两轮结果相同；四个 profile 的 1000 次重复 Schema 均已零分配，1000 次短名 LINE 查询也已零分配，
1000 次长有效名称查询均有 1000 次分配、累计请求 64,000 字节。
这些是全局 new/new[] 请求，包含释放掉的临时对象，不是存活/峰值内存，也没有证明完整导入提速。

不合并重写已有 `StepRuleCatalog.h` 规则登记；它已有独立 consteval/static_assert 保障。
不把 Python 状态映射或所有协议表拉进本批，也不把本次改动描述为完成 EXPRESS 编译器、WHERE/RULE 或几何验证。

## 唯一事实来源与生成过程

保留公共字段及各 profile 的差异来源，改成模块私有的 constexpr 描述和组装函数，统一供构建器读取。
原文件中的固定源 artifact 引用、项目子集限制和传输顺序说明随描述保留。
不增加需手工同步的生成表、字段总数或外部代码生成步骤。

生成按以下顺序进行：

1. 按现有顺序组合共同描述和 profile 差异。新增要求名称不存在，删除/覆盖要求目标存在且唯一；
   向现有 SELECT 扩展分支时，目标必须存在且唯一，新分支不可重复。
   AP203 general、AP214e3 与 MBx 的差异显式列出，不能把“相同传输布局”当成相同 schema 语义。
2. 校验名称及引用，确认所有父类存在且无环，再计算祖先、展开字段及各池所需大小。
   先检查再索引，保证错误描述由目录 validator 拒绝，而非意外越界、缺头文件或求值预算耗尽。
3. 计数阶段得到 constexpr 的整数 Counts 和 ValidationReport；在常量上下文断言报告有效，
   用 Counts 实例化恰好所需大小的 `std::array`，由 consteval 构建最终数据。
4. 四个最终存储和只读视图集中定义在一个 owning `.cpp`；`Schema(profile)` 只分派返回相应视图。
   所有真实 profile 都在正常库构建中实例化，不能仅在测试中触发。

最终 storage 和长期视图均要求 constexpr 定义，并检查 owning TU 没有目录动态初始化函数或局部静态 guard。
不能把分配/DFS 挪到 `main` 前，再以 Schema 窗口零分配冒充消除初始化。

允许组装/计数期间使用临时 vector；分配必须在该次常量求值内释放，元素名称只借静态字面量。
最终结果不含 vector/string/unordered_map，也不含指向构建器临时对象的指针或 span。
现有[编译观察](evidence/probe-observations.json)只证明本机的临时 vector 小例子可用，
不等于完整目录生成已通过；完整配方的编译和资源测量属于实施验收。

Counts 只包含整数，可用作模板参数；不将包含 string_view 的描述对象当作模板非类型参数。
诊断断言读取已成为 constexpr 对象的 ValidationReport，不把 consteval 普通函数形参直接用于 static_assert。
禁止用仅有 `static_assert("message")` 的形式表达拒绝。

## 最终表示与查询接口

| 数据 | 存储与访问决定 |
|---|---|
| 类型、字段、父类、内层 aggregate、分支和派生槽 | 固定数组池；名称为指向静态字面量的 string_view，变长范围用 offset/count 描述 |
| 类型 ID 与声明顺序 | profile 内的整数 ID，保持现有 declarations 顺序；不得因查找排序而改变 ID、报告或 TypesOf 顺序 |
| 名称查找 | 独立按名称排序的 `(name, id)` 数组，用 string_view 二分查找；不引入 perfect hash 或运行期兼容 hash 缓存 |
| 继承和源 record membership | 同一个私有 `TypeMembership`，以四个 uint64_t 保存 256 位，提供 constexpr set/test/合并；不经运行期 bitset 转换 |
| 继承字段绑定 | `(declaringTypeId, localFieldIndex)`，最终视图解析为 const Field 引用；构建期间不保存指向自身字段池的指针 |
| SELECT/排斥组/派生槽 | 在生成时解析名称为 TypeId、字段引用或 membership mask；运行期仅判断已有整数关系 |
| 只读范围 | 由最终静态对象建立 span；构建器返回前不把指向自己成员的 span 烘入结果，防止返回/移动后悬空 |

256 是现有表示容量，改为一个共享常量并编译期检查。257 个类型必须明确拒绝，不静默截断或扩大容量。
当前 C++20 工具链不能在已测例子中常量修改 std::bitset；四字存储是针对固定祖先计算的具体选择，
不据此推广为全仓替换 bitset。位移使用 `id % 64`，访问前检查 `id < 256`。

Catalog 的内部查询保持小接口：查类型、取本地/展开字段、判断继承/SELECT/枚举/complex/derived。
`StepSemantics.cpp` 的直接 ids/容器访问改为这些只读操作，Geometry/UnitDependencies 继续经 Find 查询。
标识符只在对应 profile 内有效，不能成为可持久化实体 ID 或跨 profile 互换。
公共数字索引错误仍需检查；span 的 `operator[]` 不能替代现有对越界参数的拒绝。

四个长期独立的静态目录保留身份，不设可变的“当前 profile”。私有清单集中登记这四份实例及分派，
新增 profile 必须同时增加实例和测试；不声称 C++20 自动反射未来新增的枚举值。
未知 profile 仍作为内部调用错误拒绝，外部 FILE_SCHEMA 的识别和源位置诊断继续由原选择器处理。

## 编译期校验的准确范围

既有运行期目录检查全部迁移，不删掉难以常量求值的部分：

- 类型数不超过容量，声明名唯一，父类存在，继承无环。
- complex facets 已注册、唯一、有序、包含各自全部祖先；整个 signature 不重复。
- 排斥组和引用 SELECT 的分支已注册；引用字段能解析为 entity 或已登记 SELECT。
- 枚举字段有对应值表；SI_PREFIX 的空值解释及固定前缀幂来自现有表，不能推导新增前缀。
- derived 的 owner/origin 存在，origin 是 owner 的祖先，字段是 origin 的本地字段。

另外增加目录自身的质量约束：同一声明的本地字段、SELECT 名、枚举名及其分支/值不可重复；
引用分支/值表不可意外为空，aggregate 有上限时 minimum 不大于 maximum，内层维度描述合法；
profile 覆盖/替换必须命中唯一目标。SI_PREFIX 的空字符串 sentinel 是显式允许的既有例外。
这些是程序描述质量检查，不新增对外 STEP 文件的接受/拒绝规则。
不同祖先声明的同名字段仍以原声明资格区分，不能因为名字相同就错误合并或在编译期拒绝。

展开算法按已声明的父序递归，继承字段按 `(origin, field)` 保留第一次，最后追加本地字段。
complex 映射只读取每个 facet 的本地字段，simple 映射才读取展开字段。
derived 槽仍按原登记优先顺序查询，只描述 `*` 槽，不执行 DERIVE 或改写源值。

ValidationReport 记录稳定错误码、profile、声明/字段或登记项下标。
负例必须在生成入口出现稳定目录诊断标记，例如 `STEP_CATALOG_INVALID` 和错误类别；
不绑定整段编译器文字。可用只含枚举和整数的模板诊断携带定位，不为错误报告构造常量期动态字符串。

## 调用者迁移与所有权

明确采用两处公开 C++ 表示变化，和静态目录切换放在同一可审查批次，提供 breaking 发布说明：

- `Snapshot::RegisteredTypes()` 从 `const vector<string>&` 改为 `span<const string_view>`。
  现有 JSON、STEP_IMPORT 的遍历/查找及测试一起更新；需要独立可编辑列表的调用者显式复制。
  不为旧返回类型保留运行期 vector 缓存。
- `AttributeView::nestedAggregates` 从 vector 改为 `span<const AggregateDimension>`，借用静态维度池。
  调用者不能再原地改写该元数据；需要修改时复制。`TypesOf()` 仍返回 owning vector<string>。

`FieldView` 继续持有 snapshot owner。AttributeView 的 value 仍借 SourceSnapshot，不能因为元数据静态化就脱离源 owner 使用。
simple/complex 的 declaredIn 文字必须一致，可统一指向对应静态声明名；源 record/facet/slot/span 仍取自原输入。
源引用必须是 owning snapshot 中实际 Value 的地址，复制值或另一 snapshot 的相同 ID/span 仍拒绝。

未知类型查询、注册但 population 为空、歧义字段、越界和错误资格的区分保持不变。
预算仍按原源图访问收取；不得因固定目录优化降低依赖访问计数、改变首错次序或将失败结果发布为 snapshot。

## 验证与通过条件

### 固定描述的正反编译

正例至少包括四个真实 profile、菱形继承及父序、合法 derived override、嵌套 aggregate、
membership 的 63/64、127/128、255 及容量 256、独立 consumer TU 的链接。
每类负例只注入一个错误：

| 负例组 | 必须拒绝的固定描述 |
|---|---|
| 名称/继承/容量 | 重复声明、未知父类、继承环、257 类型 |
| 字段和分支 | 未注册 entity/SELECT 分支、无枚举、重复本地字段/SELECT/枚举/分支、非法空表、非法 aggregate 范围 |
| complex | facet 无序、重复、缺祖先、重复完整 signature |
| derived/profile | 未知 owner/origin、非祖先 origin、无本地字段、覆盖目标不存在或重复 |

每个负例同时要求编译非零退出和对应 validator 标记；同环境的未注入正例必须编译通过。
测试使用生产构建器，不能抄一份校验逻辑当 oracle。编译失败只是测试预期，不能靠缺 include 或无关错误“通过”。

### 目录全量等价与运行期合同

迁移前用当前构造器输出仅供测试的四份规范目录快照，绑定源码和工具链身份。
逐项保留 profile/artifact、声明和父序、simple 标记、本地与展开字段、完整祖先、字段全部类型/维度属性、
SELECT、枚举、signature、derived、measure、前缀表、排斥组成员/顺序和 name→ID；
生成后的排斥 mask 逐项对应原组。只对索引展示排序，不对语义顺序排序。
迁移后逐项比较；跨二进制不比较地址，Value 身份在各进程内由合同测试断言。

现有 CLI 只输出输入实际用到字段，不能代替全目录比较。
目录快照证明重构等价，固定 N2635 的独立 witness 继续证明其覆盖范围内的源事实；二者不可互相替代。
保留以下现有验证面：

- [StepSemanticsTests.cpp](../../../source/STEP_SEMANTICS/tests/StepSemanticsTests.cpp)：profile 同时存活与隔离、
  继承资格、derived 差异、nested grid、源 Value 身份、FieldView 保活及错误查询。
- [语义验证工具](../../../tools/step_semantic_validation/README.md)及其 CLI tests：冻结输入、独立声明 witness、
  AP203 general/AP214/MBx（含曲线样式及 SIZE_SELECT）、精确诊断、临界访问/报告预算、无部分 snapshot。
- STEP_PLANAR 合同与 STEP_IMPORT general/声明合同：公共返回类型迁移、源 profile 传播、先拒绝后 native、报告字节和无部分发布。

先在独立构建目录执行（实施后的命令，不代表本轮已通过）：

```powershell
cmake -S source/STEP_PLANAR -B <fresh-build>
cmake --build <fresh-build> --config Release
ctest --test-dir <fresh-build> -C Release -R 'step_(source|semantics|planar)_contracts' --output-on-failure
$env:STEP_SEMANTIC_BINARY = '<actual-step_semantic_inspect.exe>'
python -m unittest discover -s tools/step_semantic_validation/tests -v
```

原生层另用与源码/配置/CRT/宏匹配的 SDK 构建 STEP_IMPORT 和对应合同，不把独立语义测试当作原生导入验收。
用相同输入、请求和预算，对冻结 49 cases 及其他三个 profile 的合成案例运行旧/新 CLI，
比较退出码、status/phase、源字节/ID、字段和诊断次序、span/path、依赖计数及报告预算边界。
只允许逐项列明的执行身份或计时差异；不得整体删除统计字段或忽略预算/报告字节变化。
静态生成先在当前 MSVC C++20 Debug/Release 通过；Linux/macOS 工具链执行同一正反编译及合同后才记录该平台通过。
未测平台不据语法推断兼容。

### 收益与构建成本

同一 harness、工具链和配置比较改前/改后，窗口外准备参数、输出结果，窗口内只执行被测查询：

实施收据使用的计数器要有已知分配的正对照，覆盖使用中的普通、数组、aligned/nothrow 路径；
结合 constexpr 定义和目标文件检查，排除计数失效或初始化前移。

- 新进程首次 `Schema(profile)`：四个 profile 分别测，改后目录取得路径的 new/new[] 请求数目标为零。
- 1000 次重复 Schema 作为对照；Find 分别测短名/长名和未知名，正确结果是前提，改后查询分配目标为零。
- nested 维度元数据：绑定和公开属性查询的结果等价，非空维度查询不再分配该 vector；不能要求整次源绑定零分配。
- 首用耗时、重复查询和完整导入分别报告；完整导入固定输入与请求，重复测分布，未测不报提速比例。
- 同机器/并行度记录目录 TU 与整个 STEP_SEMANTICS 的干净/增量构建时间、编译器峰值内存、object/library/程序体积。
  分配累计请求字节与峰值/保留内存分开，不把其中一个当成另一个。

固定优化/LTO、完整编译命令、依赖版本和源码身份，旧/新样本的其他并行改动须一致。
增量分别测私有描述、公开头和普通 consumer 改动，记录缓存条件、重复次数及分布；
不能把本次并行的 CMake/SDK 调整归为目录迁移收益。

编译成本增加不自动否决已选保障，先集中计算、缩小重算范围和消除公共头重复实例化。
若触及编译器求值限制，记录实际命令和失败，再优化生成算法或拆分私有数据；不能悄悄恢复运行期 DFS。
无法达到固定错误编译拒绝、静态无构造或行为等价中的任一项，或尚无分配与构建成本实测收据，
就不能将迁移标为完成。完整导入未提速可以如实记为无提速，不能以此掩盖缺失的测量。

## 本候选的实施批次

1. 固定基线与验收工具：四 profile 全量快照、分配测量、正反编译驱动的基础设施。
   这是重构的比较依据，不改变产品接受范围。
2. 完整迁移：私有描述/生成器、四份静态存储、membership、所有消费者及两处公开表示一起切换。
   删除运行期组装/验证和旧兼容缓存；更新接口说明与 breaking 发布说明，跑上述合同和正反编译。
3. 收益与成本收据：用同一基线测分配、耗时、构建成本和体积，补必要生成优化并复核等价。
   只报告实测结果；没有必要的优化时不制造额外产品提交。

每批独立提交，不包含并行改动；本候选的这一顺序是后续专题总体验收/批次决策的输入，
不提前解决容器契约、固定数值或全仓平台策略的其他票。
