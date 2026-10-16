# 02: 最小STEP可inspect，文件错误准确定位

**What to build:** 从基础平面STEP文件得到完整不可变Source snapshot及inspect记录；遇到文件/引用错误，调用者得到明确失败和原始位置。

**Blocked by:** [01：从样件设计得到可重复的独立验收证据](01-fixture-evidence.md)。

**Status:** closed

**Assignee:** codex

**Execution:** completed。2026-10-16按用户授权实现并验收02票：自研STEP解析与inspect，不依赖OCCT读取。03及后续源语义/native票未开始。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [x] 自研路径读取首profile的完整HEADER/DATA、simple和已准入complex原值；保留字符串、typed values、聚集、`$`/`*`、ID/引用与位置，不建立native模型。
- [x] 基础51记录输入可inspect；N02/N03等索引/引用错误准确拒绝，既有部分记录不冒充完整源成功。
- [x] 前向引用和记录乱序不改变已支持范围的原始图；不按最大ID分配无限容器，不读第一个shape就停止。
- [x] 输入捕获后修改原路径不会改写Source snapshot；输入/记录/报告的已配置基本限额生效，未支持载体或编码明确拒绝。
- [x] 本票只声明已实现载体范围，不能以一个ASCII单DATA样件宣布全部Part21或总体P1通过。

**Resolution:** [02实际验收记录](../../../docs/step-import/ticket02-acceptance.md)。独立C++17 STEP_SOURCE与CLI已交付，ASCII `2;1`单DATA范围。49项源检查合同匹配（47 Inspected＋N02/N03准确Rejected），Debug/Release各10组C++契约检查通过，两版Python各31测试通过；完整几何导入仍NotRun。01工具及303个冻结材料未改。
