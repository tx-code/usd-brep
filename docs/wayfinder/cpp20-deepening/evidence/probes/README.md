# 编译期能力与类型事实探针

这些是专题基线材料，不接入产品构建。只编译为静态库，不链接 SMLib、不运行几何操作，
生成文件、原始诊断和观察 JSON 留在指定 build 目录。

```powershell
cmake -S docs/wayfinder/cpp20-deepening/evidence/probes -B <fresh-build> `
  -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
```

固定点 `5249915` 对应的本机观察：

| 源码 | 预期 | 能说明什么 |
|---|---|---|
| type_traits.cpp | 编译通过 | 当前 SmVector3d、SmTArray、指针和反例的类型特征；不证明存储语义安全 |
| rule_registry.cpp | 编译通过 | 真实的现有局部/全局规则 consteval 校验可触发 |
| rational_default.cpp | 编译通过 | 当前 Rational 默认值/getter 可用于常量表达式 |
| rational_nonzero.cpp | 编译失败 | 当前参数构造不支持该常量表达式 |
| transient_vector.cpp | 编译通过 | 本机可用临时动态容器生成最终固定数组 |
| persistent_vector.cpp | 编译失败 | 不能由上一项推出动态分配可保留在持久 constexpr 对象中 |
| bitset_type.cpp | 编译通过 | bitset 类型和头文件可用 |
| bitset_mutation.cpp | 编译失败 | 本机 C++20 模式下 set 操作不能用于该常量求值 |
| message_only_assertion.cpp | 编译通过 | 字符串作为 static_assert 的唯一参数不拒绝编译 |

CMake 要求结果符合这组固定观察，意外结果应先读对应日志，不当作产品缺陷。
未来源码、编译器或标准库改变时，预期可随新证据更新；本机结果不是跨平台支持声明。
正例陪同相关负例，避免把缺失头文件误读成预期的编译期拒绝。

`observations.json` 记录实际结果；每项 `<name>.log` 保留原始构建诊断。
仓库中的 `../probe-observations.json` 是本轮结果摘要，原始日志位于本机
`D:/Temp/cpp20-evidence-baseline/`。没有将配置探针的耗时当作产品构建性能数据。
