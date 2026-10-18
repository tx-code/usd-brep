# C++20 深化初步代码线索

2026-10-18；扫描固定点 `602b082`。这里只保留足以提出问题的局部证据，
没有完成全仓类型实例审计、性能测量、编译期能力探针或候选验收。

后续任务已建立[代码基线与本机编译观察](evidence/code-baseline.md)。本文保留为建图时的线索快照，
候选事实与已知限制以后续证据为准。

## 已有实践

[`StepRuleCatalog.h`](../../../source/STEP_PLANAR/src/StepRuleCatalog.h) 已有
`LocalRegistryIsValid()`、`GlobalRegistryIsValid()` 的 `consteval` 校验与 `static_assert`，
约束局部/全局规则身份、执行登记和覆盖关系。应复用其思路，并先检查覆盖缺口，避免重复建设。

[`StepPlanarTests.cpp`](../../../source/STEP_PLANAR/tests/StepPlanarTests.cpp) 使用 concept 和
`static_assert` 检查 Rational 私有成员不可被直接写入，说明编译期负例已是仓库的有效验证形式。

## 待决策的落点

| 方向 | 当前源码事实 | 可调查的编译期收益 | 不能预先假定的事 |
|---|---|---|---|
| 固定 schema 目录 | [`StepCatalog.cpp`](../../../source/STEP_SEMANTICS/src/StepCatalog.cpp) 的 `Catalog::Catalog()` 建立名称索引、祖先关系和展开字段；`ValidateProfile()` 在运行期检查引用、派生槽、重复与闭包 | 固定声明描述的完整性校验，以及祖先/展开字段等固定派生数据的生成 | 当前 `std::vector`、`std::string`、`std::bitset` 和指针绑定布局能否原样用于 C++20 常量求值，必须核实；不能靠把异常改成断言完成迁移 |
| 容器的元素与算法契约 | [`SmTArray.h`](../../../source/SMLib/inc/SmTArray.h) 用注释限制元素种类，实际存储路径使用 calloc、memcpy、memset；其成员算法还有比较等额外要求 | 用 traits、concepts、`requires` 在实际使用的操作上表达前提，并提供明确诊断 | “没有虚函数”不等于可按字节安全管理；也不能未经实例审计就在整个类上加 `is_trivially_copyable`，误拒历史类型或给不安全类型背书 |
| 固定数值与质量配置 | [`StepImport.cpp`](../../../source/STEP_IMPORT/src/StepImport.cpp) 在固定 Quality 工厂中构造分数；[`StepPlanar.h`](../../../source/STEP_PLANAR/inc/StepPlanar.h) 的 Rational getter 已为 constexpr，构造和运算仍独立实现 | 固定字面量的规范化、范围/关系约束和配置身份检查可以优先研究编译期入口 | 自定义运行期质量、十进制源值、容量耗尽和浮点环境仍须保留真实诊断；不能把编译器的浮点求值当作运行期几何证明 |
| 协议身份与枚举映射 | [`StepSchemaSelection.cpp`](../../../source/STEP_SEMANTICS/src/StepSchemaSelection.cpp) 已有 constexpr 版本数组；[`SmPyCommon.h`](../../../source/SmPyLib/src/SmPyCommon.h) 有绑定侧枚举 | 登记键唯一性、闭合集合覆盖、固定映射一致性与静态查找结构 | 外部 HEADER 仍是运行期数据；绑定里重复同一数字并不能证明它符合原生合同，须先找到可编译的事实来源 |
| 静态尺寸与非拥有视图 | 内核有大量固定维度几何值和指针/数量接口；STEP 已使用 `std::span` 表达固定登记表的读取视图 | 调查哪些内部 seam 能用静态 extent、受约束重载或固定数组降低参数组合错误 | 动态 span 不证明长度正确或对象存活；不得把视图包装当作所有权验证 |

## 候选评估应记录的证据

- 能被编译器拒绝的具体错误，或能移除的运行期固定工作；是否已有等效保障。
- 固定描述的唯一来源与派生关系，外部输入仍需承担的检查。
- 对合法旧类型、接口、布局、诊断、初始化顺序及数值语义的影响。
- 常量求值与模板实例化的位置：是否在公共头中重复计算，能否集中到少量翻译单元。
- 编译时间、编译器内存、目标大小及必要的运行期观测；未测项目标为未测，不编造收益比例。

目前最清晰的首批调查方向是固定目录与模板契约。它们是否进入首批实施，仍由专题票决定。
