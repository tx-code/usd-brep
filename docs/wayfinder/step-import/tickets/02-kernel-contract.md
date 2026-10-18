---
title: STEP 参数与拓扑如何无歧义地进入 SMLib
parent: ../map.md
labels: ["wayfinder:research"]
state: closed
assignee: codex-kernel-research
---

## Question

对于首批解析面、有理 B 样条、共享边与周期裁剪，STEP 长度/角参数、各层方向、pcurve/3D 边与 SMLib 数据契约的对应关系是什么？哪些 SMLib 构建入口会隐式转换、修复或丢失错误状态？哪些问题属于转换，哪些才属于修复？

源码证据优先，规范与实现选择分别记录。OCCT 固定版本文档/转换源码可参考，但产品不链接其读取器。既有 Truck 调查可以复用，不重新运行全套测试。

## Blocked by

无。

## Context

[既有 STEP 计划](../../../step-import-repair-plan.md)；[BRep 模型参考](../../../../.agents/skills/smlib-brep-model/references/brep-format.md)。将研究结果保存为本机独立 Markdown，记录固定提交和未验证项。

## Resolution comments

2026-10-15 · codex-kernel-research：源码调查完成。源 STEP 文件参数、SMLib STEP-style 参数和 ordinary/NURBS 参数必须分别映射；闭合边在 SMLib 可合法保留，极点 type 3 不能作为一般退化拓扑的可靠传送通道。方向、面内 occurrence、源 pcurve 和优先表示不能压缩为一个布尔方向或仅按 `(face, edge)` 去重。

`MakeTopologyFromData` 在关闭 healer/analytics 后仍会处理 face 域及改变输入几何状态，不具备已证明的无副作用/原子事务保证；UV 检查开关未实施，修复状态传播有缺口。以上是构建适配器的设计约束，不是新 importer 已经失败或已被修复的动态结论。反向 use 的 UV 存放约定存在源码冲突，须后续专门探针验证。

证据：`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/wayfinder-kernel-contract.md`。固定 SMLib `f0d3897`、OCCT `3d097a0`；只读源码调查，没有新增几何运行验证。语义规范化、pcurve 策略、容差政策、具体构建入口和失败清理保证留给后续设计/验证票。
