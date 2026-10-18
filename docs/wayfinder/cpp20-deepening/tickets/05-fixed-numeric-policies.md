---
title: 固定数值和策略配置应如何选择编译期入口
parent: ../map.md
labels: ["wayfinder:grilling"]
state: open
assignee: null
---

## Question

固定分数、单位映射、质量预设及有限策略组合中，哪些应采用强制编译期构造，哪些宜支持同一实现的编译期/运行期使用，
哪些只需要静态初始化保证？分别说明 consteval、constexpr、constinit 在所选位置到底保证什么。

明确常量规范化、范围和组合约束、失败诊断与配置身份的唯一来源；
保留用户运行期配置、源十进制值及容量耗尽的真实错误处理。
不得改变数值精度/容差政策，也不能用编译期浮点结果代替运行期环境下的几何证据。

## Blocked by

- [编译期深化应采用什么收益与兼容性门槛](01-priorities-and-compatibility.md)
- [建立已有编译期保障与候选点的代码基线](02-code-evidence-baseline.md)

## Context

固定 Quality 工厂、受限 Rational 及单位解释的初步入口见[初步代码线索](../initial-scan.md)。
