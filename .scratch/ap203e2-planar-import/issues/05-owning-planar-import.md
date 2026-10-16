# 05: 从STEP交付owning只读开放平面

**What to build:** 对最小开放平面，内部C++调用返回完整Imported model snapshot与Import report，能精确查询，源与结果均有清晰所有权。

**Blocked by:** [04：平面的必要规则与有向区域可独立验收](04-planar-source-validation.md)。

**Status:** closed

**Assignee:** codex

**Execution:** completed。2026-10-16用户在确认05—12后续计划后要求“继续”，本轮已实现并验收05最小开放平面纵向切片。01—04继续回归，06—12仍on-hold；不扩展为GUI、公开绑定或一般修复。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [x] 从STEP经原始文档、中立模型及受控构建进入SMLib；F1面积96、边界40、拓扑/法向/参数/bbox及查询满足独立Q。
- [x] target UV后装、SAME owner、有限window、来源映射、工作tol/有效阈值逐项核验；规范化与repair分开，Repair=None。
- [x] 只在完整候选及必需报告成立后提交Accepted；普通源/构建/后验失败销毁候选，没有部分模型或暴露owned裸指针。
- [x] 快照、源证据和报告的生命周期可测试，默认只读；开放表面不授予material volume资格。
- [x] 仅证明本进程内部调用，后续公开API、GUI或worker并不因本票自动获得资格。

**完成记录：** [05验收](../../../docs/step-import/ticket05-acceptance.md)。实现76a256b、审查修复9d6a060；双配置native接口/资源及各9组CLI通过，01—04核心138项及196份字节/169预算回归通过；Standards/Spec复审各0项。没有推送，06—12保持on-hold。
