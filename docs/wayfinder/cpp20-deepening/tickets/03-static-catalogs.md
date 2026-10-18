---
title: 哪些固定目录应在编译期验证并生成派生数据
parent: ../map.md
labels: ["wayfinder:grilling"]
state: closed
assignee: codex-primary
---

## Question

STEP schema、规则、协议身份以及其他被证据基线识别出的固定登记表中，哪些事实应成为唯一的静态描述，
哪些引用、唯一性、容量和覆盖约束应由 consteval/static_assert 保证，哪些索引与闭包适合编译期生成？

明确生成结果的存储归属、对现有查询 seam 和源位置/诊断的影响，以及如何隔离动态源数据。
对各容器和算法的 C++20 常量求值能力逐项核实，避免靠全面模板化或重复字面量制造假保障。

## Blocked by

- [编译期深化应采用什么收益与兼容性门槛](01-priorities-and-compatibility.md)
- [建立已有编译期保障与候选点的代码基线](02-code-evidence-baseline.md)

## Context

已有 `StepRuleCatalog.h` 的编译期登记校验，待调查 `StepCatalog.cpp` 的运行期固定目录构造；见[初步代码线索](../initial-scan.md)。

[代码基线](../evidence/code-baseline.md)已确认各 profile 的目录按首次访问缓存。
本机编译观察区分了临时 vector 计算、最终持久存储和 bitset 修改；不能将当前 Catalog 整体原样标为 constexpr。
固定状态/协议登记也有具体线索，但是否与 schema 目录一同推进仍待本票取舍。

[候选收益评估](../evidence/benefit-assessment.md)已按用户要求区分错误保障、首次构造工作的消除和未测的性能收益。
本票取舍须说明新增保障，不能将已有规则校验、已完整的状态映射或仅更换容器语法计为新增收益。

## Resolution comments

2026-10-18，用户要求“继续深入 STEP 固定目录，把编译期迁移方案、明确收益和验证办法定下来”。
依已确定的编译期保障优先、允许破坏性改动和具体收益门槛，本票确定以下方案：

- 选中四个 STEP_SEMANTICS profile 的完整目录及五个扩展登记；保留已有规则登记，状态/其他协议表不并入本批。
- 采用单一私有描述、consteval 生成、精确大小静态数组池和整数关系。四份存储集中在一个翻译单元；
  名称查找使用独立排序索引，声明/继承/字段的语义顺序保持不变，256 位 membership 采用可常量求值的四字表示。
- 明确接受 RegisteredTypes 改为 string_view span、nestedAggregates 改为静态维度 span 的公开 C++ 变化，
  不保留旧动态兼容缓存；FieldView 和源 Value 的 snapshot 所有权合同保留。
- 交付目标是固定登记错误编译拒绝、目录无运行期构造、名称查询无临时字符串及内层维度无容器副本。
  [分配基线](../evidence/catalog-baseline/README.md)已测到四目录首次取得分别 1,185/2,222/1,963/2,069 次请求，
  各 1000 次长名查询有 1000 次请求；这是当前实现观测，尚未取得迁移后的减少量或导入提速数据。
- 验收采用生产构建器的正反编译、四 profile 全量目录对比、独立 witness、旧/新 CLI 与源/平面/原生合同，
  以及分配、耗时、构建资源和体积收据。禁止把初始化前移到 main 前或忽略预算/诊断差异来通过验收。

完整技术方案、调用者迁移、负例矩阵与本候选的三批实施顺序见[迁移方案](../catalog-migration-plan.md)。
本票关闭的是设计取舍；本轮仅增加决策材料和独立基线测量工具，尚未迁移产品代码。
测量已在并行合入 AP214 曲线样式的 `713bf00` 上刷新，清洁构建前后核对了直接源指纹。
全仓总体验收与批次票继续保持其原依赖，不因本候选方案确定而提前关闭。
