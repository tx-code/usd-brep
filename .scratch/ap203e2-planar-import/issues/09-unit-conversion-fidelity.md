# 09: 单位重述与conversion链保持物理几何

**What to build:** 导入调用按源物理单位正确解释厘米重述与英寸盒体，并在零负/不完整换算时明确拒绝。

**Blocked by:** [07：从盒体STEP保留共享边和材料实体](07-shared-edge-solid-box.md)。

**Status:** in-progress

**Assignee:** codex

**Execution:** in-progress。2026-10-17用户在08完成后要求继续，开始09单位重述与conversion链；10—12保持on-hold。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [ ] P06只改单位声明得到10倍几何；P07协调重述保持同一世界几何，LINE t不重复缩放，PLANE UV保持专属映射。
- [ ] P11的127/5换算、显式dimensions、typed factor、共享单位链与U1面积/体积/形心贯通；不按inch标签猜factor。
- [ ] N31/N32/N33区分源量纲反证与项目正尺度要求，预算继续用canonical mm，不静默取绝对值/修单位。
- [ ] 本票不依赖可选uncertainty支持；P13与N34的组合单位环控制在12统一验收，不人为让两条独立分支串行。
