# 06: 带孔面与负UV原点保持源区域

**What to build:** 同一导入调用能交付带孔面，并在源平面原点平移后保持世界几何及孔语义。

**Blocked by:** [05：从STEP交付owning只读开放平面](05-owning-planar-import.md)。

**Status:** ready-for-agent

**Assignee:** None

**Execution:** on-hold。用户于2026-10-15明确要求暂不编码；本票仅已建档，获得后续明确开工指令前不得开始实现、生成样件或运行实施验收。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [ ] P02/P03分别得到面积88、边界52、正确孔内/孔边/面内分类及独立距离；世界结果一致，源参数映射可追踪。
- [ ] 负UV与window原点不导致clamp掩盖缺边，不把window当额外trim。
- [ ] N25等孔方向/区域反例在正确阶段拒绝；不翻孔、填孔或增tol。
- [ ] 每次use的UV/owner及源loop身份保持，查询证据和原有F1回归一起交付。
