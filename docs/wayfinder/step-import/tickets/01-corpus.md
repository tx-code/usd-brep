---
title: 真实模型库要求哪些 STEP 表示与几何实体
parent: ../map.md
labels: ["wayfinder:research"]
state: closed
assignee: codex-corpus-research
---

## Question

现有模型库的协议、表示根、几何/拓扑实体、装配和单位上下文呈现哪些需求？在有限文本读取预算内，哪些覆盖结论可信，哪些仍未知？据此首版实体白名单需要哪些候选，哪些必须等待后续证据？

已有清单覆盖 4,049 个 STEP 文件；HEADER 样本 179 个，覆盖 12 个源目录。HEADER 不构成实体或几何有效性证明。研究仅做文本画像与严格计数，不调用 CAD 导入、网格化或自动修复。

## Blocked by

无。

## Context

已有本机证据：`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/library-inventory.json`。新实体画像应落在同目录独立文件中，公开内容只使用匿名聚合与充分披露的样本限制。

## Resolution comments

2026-10-15 · codex-corpus-research：完成有限预算文本研究。40 个文件覆盖 12 个源目录，34 个完整读取、6 个仅 8 MiB 前缀，实际读取 59,068,215 字节。观察到 134 种显式实体类型，包括开放壳、有理复合 B 样条、裁剪曲线、旋转面、pcurve，以及多个单位/不确定度上下文。大文件前缀还出现椭圆、线性拉伸面和 quasi-uniform 类型。

这些是候选清单的需求证据，不是本项目已支持的实体。样本未能验证真实装配、周期缝/极点/退化语义或转换制长度单位；MAPPED_ITEM 不能单凭出现就当装配覆盖。各实体计数、完整文件与前缀区分、选样偏差和未知项以证据报告为准。

证据：`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/wayfinder-corpus/step-entity-profile-findings.zh.md`；同目录 `step-entity-profile.private.json` 和 `evidence-verification.json`。未执行 CAD 导入、修复或网格化。
