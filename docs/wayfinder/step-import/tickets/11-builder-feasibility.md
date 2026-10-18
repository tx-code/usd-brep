---
title: 哪条 SMLib 构建路径能满足保真和失败清理要求
parent: ../map.md
labels: ["wayfinder:research"]
state: closed
assignee: codex
---

## Question

`MakeTopologyFromData` 与受控top-down构建路径中，哪一条能在关闭隐式修复后保留所需共享边、方向、authored pcurve、面域、开放/闭合及空腔语义？输入不变、context生命期和失败清理可以在什么范围内证明？若现有入口不能满足，最小需要的适配或隔离策略是什么？

既有源码审计只证明存在风险，尚无足够运行证据指定实现入口。本研究允许仓库外、可丢弃的合成native探针；不实现STEP parser、不改生产内核、不使用真实模型长跑、不调用网格化，也不把某条API成功当完整证明。

最小探针候选：关闭healer/analytics的开放面和共享边；反向occurrence+源UV；solid+void和奇异表示；同一输入重复新建/销毁；安全可控的普通失败/取消及资源对账。分配故障只有在存在可靠注入手段时才声称覆盖。失败前后同时核对源、staging和目标状态。

构建探针还须记录显式初始tol、构建后及内核检查后的逐实体有效tol与关键阈值，检查传播和非预期变化；不能把默认字段值或成功状态当作已遵守调用方质量配置的证据。

结果应给出：已证明性质、失败或未支持项、未验证错误路径、是否需要进程隔离或最小内核调整。若可行路径仍不清楚，不应关闭最终规格就绪票。

## Blocked by

[精确 BRep 构建和参数边界怎样验收](06-exact-brep.md)。

## Context

已有本机研究：`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/wayfinder-kernel-contract.md`；[精确BRep讨论材料](../exact-brep-proposal.md)。按本地tracker约定保留独立证据及固定源码/运行库版本。

本轮汇总：[SMLib构建路径与失败清理合成验证](../builder-feasibility-assessment.md)。本机可复现证据位于`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/builder-feasibility/`的data-path、topdown、cleanup、periodic四个独立目录。

## Resolution comments

2026-10-15 · codex：完成有限合成native验证，研究问题已有运行证据。关闭研究票表示调查完成，不表示完整STEP能力或任意失败清理已经通过，也没有替人类批准最终构建后端。

1. 两个入口都可作为受控构建适配器的候选。独立手写平面片验证了共享边和反向使用；top-down另外验证了四面体/单空腔与解析点分类、体积。原生圆柱/球通过数据回建，保留同面seam双occurrence，圆柱两条非零闭合边也保留。后者是native表示承载证据，不是独立STEP参数/退化边映射证明。
2. 两个直接UV入口均可把反向occurrence的UV存OPPOSITE成员，而pointer/LEVEL2照样通过。已验证可先建图，再逐occurrence用fresh copy经setter装到SAME成员，保留lift/方向/周期分支。仅关bMakeUVTrimCurves不阻止安装已有UV，同指针重调setter也不迁移存储侧。
3. 数据构建会改写staging的mapping/owner/借用状态、静默裁面域、将实体tol至少提高到BRep下限；forceSameTol不能保证逐实体不变。首target销毁后复用同staging返回1006。top-down可显式控域和关闭stitch，但需约束反向edge顶点参数、surface所有权、radial插入与void标志。漏设void也能通过内核验证却得到错误材料体积，因此源语义必须独立验收。
4. 七种成功/后验丢弃/普通错误/调用边界取消路径，各预热后测800轮，未发现随批次持续增长的活跃Windows堆分配；个别进程有4292B/9块或8584B/18块未归因平台增长，不能称全链路零增长。source顶点坐标/tol和context DoingBoolean对账符合已测条件。SmContext不是统一回收arena，自带heap计数是占位实现；候选与staging仍须按所有权和生命期清理。
5. 人工worker终止/FailFast控制证明探针提交门和进程边界可用，没有证明生产结果传回与原子发布。没有可靠OOM注入，未证任意分配点失败、回调异常、调用内部异步取消、损坏图析构或进程内crash恢复。不能把catch或独立context当成这些保证。

源码固定f0d3897；既有SMLib.dll固定hash8cd99a41...，实际加载路径与完整hash记录于证据，没有重建内核、改生产代码或调用网格。已测正例没有必须改内核的阻碍。一般NURBS/STEP参数转换、源master一致性、奇异规范化和生产故障协议仍须明确进入/退出条件；最终规格票不能据本研究直接授予全部BRep或清理资格。

下一张结果接口决策需要确认受控构建、候选所有权、隔离/取消和发布协议；首个纵向切片以top-down为候选的建议尚未获人类确认，MakeTopologyFromData仍可作为内部候选。
