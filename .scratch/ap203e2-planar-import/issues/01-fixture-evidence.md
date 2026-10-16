# 01: 从样件设计得到可重复的独立验收证据

**What to build:** 执行者能按样件ID取得固定输入和独立真值，看到正例及单缺陷反例的版本、谱系和预计诊断；尚未实现的导入行为明确NotRun。

**Blocked by:** None。

**Status:** closed

**Assignee:** codex

**Execution:** completed。2026-10-16首次验收时仅实施01，四条原验收条件已完成；当时02—12未开始。后续01—04均完成，当前状态见票组索引及准备记录。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Implementation plan:** [样件、独立真值与证据组织方案](../plans/01-fixture-evidence-plan.md)。原方案保留设计时快照；实际完成状态以本票和验收记录为准。

**Data contracts and controls:** [原设计](../plans/01-data-contracts-and-controls.md)；[实际工具和格式](../../../tools/step_import_validation/README.md)。

**Control runbook:** [12组、14子场景的设置、触达证据与裁决清单](../plans/01-control-execution-checklist.md)。实际14项均通过预定检查，详见验收记录。

**Acceptance criteria:**

- [x] 13正例、36反例各有唯一ID、固定输入字节/hash、父例与精确mutation；记录数/引用与设计对账，反例不混入额外编辑。
- [x] 精确坐标、方向、面积/体积/距离和身份真值由独立数学定义取得；不从被测loader、native CreateBox或OCCT输出回填expected。
- [x] 样件、Q、诊断合同及oracle的版本、误差界和来源可追溯；未运行不写零误差、Passed或零泄漏。
- [x] 一次case选择可产出可读证据；报告能区分设计期望、输入对账和真正的被测执行。后续票复用该调用路径。

**Resolution:** [01实际验收记录与V01—V12证据](../../../docs/step-import/ticket01-acceptance.md)。49项材料核验与独立核算完成；14工具控制通过；Python 3.10/3.14各28测试通过。303个材料文件双目录字节一致。产品导入仍NotConfigured/NotRun=49，未声称native构建或完整AP符合性通过。
