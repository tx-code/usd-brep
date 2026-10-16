# 03: 单零件、类型与SI单位的源语义可检查

**What to build:** inspect能从源文档解析唯一零件定义、具体表示和SI单位context，给出可追踪的中立解释；必需字段、类型或范围不成立时不猜默认值。

**Blocked by:** [02：最小STEP可inspect，文件错误准确定位](02-step-inspect.md)。

**Status:** closed

**Assignee:** codex

**Execution:** completed。2026-10-16按用户授权实现并验收单零件、类型与SI源语义；不依赖OCCT。04及后续规则/几何/native票未开始。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [x] 完整名称/OID明确绑定N2635；APD声明与HEADER分开保留，不因年份或家族名覆盖矛盾。
- [x] 产品/PDS/SDR/表示/context、祖先及constituent身份完整；源图没有native指针，也不另复制一份全文件类型对象图。
- [x] 基础SI长度/rad、复合局部字段和derived槽按确切profile解释；方向比值、物理长度、LINE参数和PLANE参数分开。
- [x] N01、N04—N09、N14/N15及本票涉及的范围反例给出正确逻辑责任；未知/未支持必需内容不被跳过。
- [x] 反向索引覆盖完整已捕获范围，后续规则检查不会把未遍历内容伪造为空集合；未完成语义在inspect中明确标记。

**Resolution:** [03实际验收记录](../../../docs/step-import/ticket03-acceptance.md)。独立C++17 STEP_SEMANTICS与step_semantic_inspect已交付。49项当前阶段合同匹配：26 Checked、17准确阻断、6 conversion Deferred；72声明独立对账，两种配置各10组新语义＋10组源层检查、两版Python各35测试通过。完整导入仍NotRun，01/02代码及冻结材料未改。
