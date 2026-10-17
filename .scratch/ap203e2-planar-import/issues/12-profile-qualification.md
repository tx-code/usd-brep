# 12: 汇总49项端到端证据并限定首profile资格

**What to build:** 一次可重复验收生成49项样件的真实结果与能力范围，调用者能判断哪些请求可接纳、怎样失败，以及尚未覆盖什么。

**Blocked by:** [06：带孔面与负UV原点保持源区域](06-holes-and-uv-origin.md)；[08：同形闭合表面保留surface-model角色](08-closed-surface-role.md)；[09：单位重述与conversion链保持物理几何](09-unit-conversion-fidelity.md)；[10：可选来源声明与旧AP203类型完整保留](10-optional-source-declarations.md)；[11：生命周期、取消与限额不留下部分结果](11-lifecycle-cancellation-limits.md)。

**Status:** closed

**Assignee:** codex

**Execution:** completed。2026-10-17按用户继续指令完成组合与49项端到端资格验收；冻结01材料和Q，不扩展profile、不改测试包协议。当前支持、失败与P0—P7边界见[STEP_IMPORT](../../../source/STEP_IMPORT/README.md)，可重复命令见[原生验证](../../../tools/step_import_native_validation/README.md)，运行记录保留本地。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [x] P13组合conversion盒体与uncertainty锚；N34只有预期单位环、没有额外孤立unit，根因诊断与拒绝准确。
- [x] 13正例、36反例按固定输入/配置/检查版本执行；输入hash、源图/阶段、独立oracle、真实模型/诊断和owner/质量证据一致，不复用混代结果。
- [x] 36反例均无本次模型；已有明确反证的case不能以笼统未判定代替应有诊断；未测能力不被计为通过。
- [x] 输出首profile支持清单、失败原因和缺口；零已知错接纳、静默丢必需内容、超预算通过或未声明部分发布。
- [x] 将49项首profile证据与总体P0—P7门槛逐项映射；未覆盖的全部Part21载体、其他AP、开放共享片/全sense组合、截面/建模及工程故障仍保持待证，不提升为整个P1/P2或首版完成。
