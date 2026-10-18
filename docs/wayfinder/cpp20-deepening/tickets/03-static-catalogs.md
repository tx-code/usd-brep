---
title: 哪些固定目录应在编译期验证并生成派生数据
parent: ../map.md
labels: ["wayfinder:grilling"]
state: open
assignee: null
---

## Question

STEP schema、规则、协议身份以及其他被证据基线识别出的固定登记表中，哪些事实应成为唯一的静态描述，
哪些引用、唯一性、容量和覆盖约束应由 consteval/static_assert 保证，哪些索引与闭包适合编译期生成？

明确生成结果的存储归属、对现有查询 seam 和源位置/诊断的影响，以及如何隔离动态源数据。
对各容器和算法的 C++20 常量求值能力逐项核实，避免靠全面模板化或重复字面量制造假保障。

## Blocked by

- [编译期深化应采用什么收益与兼容性门槛](01-priorities-and-compatibility.md)
- [建立已有编译期保障与候选点的代码基线](02-code-evidence-baseline.md)

## Context

已有 `StepRuleCatalog.h` 的编译期登记校验，待调查 `StepCatalog.cpp` 的运行期固定目录构造；见[初步代码线索](../initial-scan.md)。

[代码基线](../evidence/code-baseline.md)已确认各 profile 的目录按首次访问缓存。
本机编译观察区分了临时 vector 计算、最终持久存储和 bitset 修改；不能将当前 Catalog 整体原样标为 constexpr。
固定状态/协议登记也有具体线索，但是否与 schema 目录一同推进仍待本票取舍。
