---
title: 模板和静态尺寸的哪些前提必须由编译器约束
parent: ../map.md
labels: ["wayfinder:grilling"]
state: open
assignee: null
---

## Question

SmTArray 等模板及内部指针/数量接口中，哪些实际前提能用 concepts、traits、requires、固定 extent 或 static_assert 准确表达？
约束应放在存储本体、特定算法还是调用 seam，才能拒绝错误用法而不误拒合法的历史实例？

先核实类型的构造、复制、清零、析构和比较要求。语法表达式可调用不等于语义安全；
不得用“无虚函数”或泛泛的 trivially-copyable 限制替代真实对象生命周期审计。
判断哪些问题需要先修类型模型，不能假称加一个 concept 就已经解决。

## Blocked by

- [编译期深化应采用什么收益与兼容性门槛](01-priorities-and-compatibility.md)
- [建立已有编译期保障与候选点的代码基线](02-code-evidence-baseline.md)

## Context

[初步代码线索](../initial-scan.md)。本票先定约束位置和可证明的范围，不授权全面替换容器或改变 BRep 所有权合同。
