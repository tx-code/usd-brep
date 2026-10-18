---
title: 编译期深化应采用什么收益与兼容性门槛
parent: ../map.md
labels: ["wayfinder:grilling"]
state: open
assignee: null
---

## Question

在用户“能编译期就编译期”的偏好下，如何取舍编译期错误保障、固定工作的前移、接口简化和构建成本？
是否允许为这些收益重构内部类型及数据表示？稳定 SM_API/Python 合同、公开 C++ 布局与 ABI 各自如何约束？

应明确原则与例外的决策方式；尚无构建测量时不虚设数值阈值。
明确哪些调整可作为内部实现选择，哪些必须带具体影响另行确认。

## Blocked by

无。

## Context

[初步代码线索](../initial-scan.md)。用户已确定 C++20 基线与编译期优先；尚未批准具体表示替换、ABI 改动或可接受构建开销。
