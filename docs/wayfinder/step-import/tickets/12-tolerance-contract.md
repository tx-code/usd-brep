---
title: 现有容差系统与 OCCT 的差距怎样影响修复预算
parent: ../map.md
labels: ["wayfinder:research"]
state: closed
assignee: codex-tolerance-research
---

## Question

SMLib是否已有容差系统，当前实际使用哪条路径？与OCCT相比，实体容差、算法精度、STEP源精度映射、传播、修复限制与验收各有什么已有证据及缺口？这些事实如何限制本轮数值预算决策？

## Blocked by

无；这是修复预算讨论中由用户提出的事实前置问题。

## Resolution comments

2026-10-15 · 已完成源码核对和一个合成SMLib setter探针。SMLib确有逐face/edge/vertex容差、zone/intersection/approx分类、context/模型尺度和传播，当前源码使用old scheme，新方案标为未ready。合成盒体中增大一face tol传播到4edge/4vertex，相关approx/xsect getter随之改变，点坐标不变。

OCCT有相应实体/算法阈值，并有成熟STEP单位/uncertainty、工作精度及ShapeFix配置链路，但也会增长容差吸收偏差，且最大tol存在preferred越界与forced截断的折中。不能据此证明全部算法质量优劣或任何配置等价于严格预算。

关键差距是当前自研loader尚未把源精度、SMLib有效容差、操作传播及独立验收接成可追踪契约。原1 μm/5 μm候选暂停定案，用户未批准。真实导入容差/残差分布、预算数值与生产默认仍需后续设计/验证。

证据：[SMLib与OCCT容差契约初步对照](../tolerance-system-assessment.md)；本机`occt-tolerance-contract.md`及`tolerance-probe/result.json`。不执行真实库导入/修复，不改生产内核。
