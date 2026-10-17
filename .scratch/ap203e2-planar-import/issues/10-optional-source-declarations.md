# 10: 可选来源声明与旧AP203类型完整保留

**What to build:** 在相同导入入口中，调用者可取得uncertainty、part分类、sr、旧context和指定source的完整来源声明，精确几何及质量配置不会被它们偷偷改写。

**Blocked by:** [05：从STEP交付owning只读开放平面](05-owning-planar-import.md)。

**Status:** closed

**Assignee:** codex

**Execution:** completed。2026-10-17可选来源声明与旧AP203类型已实现并通过双配置验收，见[10验收记录](../../../docs/step-import/ticket10-acceptance.md)；11—12保持on-hold。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [x] P08/P09/P10/P12逐项通过；声明缺席也按合法范围处理，旧类型不因deprecated丢失身份。
- [x] source uncertainty用自身单位并只记录/告警，不覆盖工作tol、Q或repair budget；rad与sr维数同零仍保留名义类型。
- [x] N29/N30/N35/N36及对应旧类型局部约束给出准确原因，既不移植其他AP管理规则，也不将新增关系忽略。
- [x] 可选分支与F1完整模型/报告一起交付；和conversion盒体的组合由12验证，避免多余阻塞边。
