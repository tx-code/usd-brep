# Import evidence package 重构验收

2026-10-16。按用户确认的[实施契约](evidence-runner-refactor.md)完成，仅涉及02—04证据执行、发布和复核。
已验收STEP基线为`7560aa13b403aa1cb6aba6024a46ced4f9c1a20d`；本轮代码已在同一main上本地提交。

## 结果

三个runner缩为stage adapter，共用[step_evidence](../../tools/step_evidence/README.md)的生命周期implementation。
stage分别保留独立比较器、见证、native选项、格式和状态/计数字段；不再复制发布协议或替换另一stage的中文描述。
原`run/check`及CLI入口保留。新包保留`t02/t03/t04.evidence.v1`，显式标记`step-evidence.v2`及版本化依赖身份策略。

复核同时检查实际发布位置、canonical路径和完整payload、准备计数、输入/inventory、独立核算、调用、比较及汇总。
准备和input_proof通过未修改01的函数重放；新协议依赖完整性按版本化项目源码清单验证，离线不读取当前源码或原二进制。
旧包保留旧来源覆盖和资格，不追溯补授新版证明。失败保留staging，重试使用新ID；目录别名不能充当发布。

## 验证

Python3.14.3与3.10.5各144项完整STEP范围测试通过：01的28项、02的31项、03的35项、04的35项和15项共享协议测试。
共享协议包含三个stage的30个重新封口矛盾组合、历史包原字节、移除原二进制后的离线复核、错误stage失败重试、
Windows junction、未登记payload、canonical路径、未知协议、CLI入口及真实执行中的源码变化控制。

三套最终新协议run都通过49项合同与完整性复核：

| stage | 最终run | 当前阶段状态 |
|---|---|---|
| 02 | `20261016T025611Z-9be75d643f19` | 47 Inspected、2 Rejected |
| 03 | `20261016T025625Z-62a6f3b8aaa0` | 26 Checked、8 Rejected、7 Unsupported、2 SourceFailure、6 Deferred |
| 04 | `20261016T025638Z-b10487d41a4a` | 11 Checked、15 Rejected、17 UpstreamFailure、6 Deferred |

三个旧49项包均继续通过离线复核且所有字节未改写；每个新旧run完整导入仍NotRun、成功数null、没有native模型。
三个独立比较器的非import函数体与基线AST相同；01工具/303材料、native C++、EXPRESS见证和规格0.1均无字节改动。
本轮没有重新构建native代码；沿用已验收的独立读取器二进制。

## 双轴审查

### Standards

明确标准违规0；Fowler基线异味0。新增版本化依赖清单、复核和测试修正的复审也无可行动问题。

### Spec

首轮发现2项P2：准备/input_proof可与输入计数矛盾；新协议允许缺失多数执行依赖身份。
两项都先通过三个stage的9个新增组合复现，再修正并独立复审；当前未解决项0，没有发现范围扩张。
审查详情单独保留在证据根`reviews.json`，不将两个审查轴合并或重新排序。

## 证据与限制

[离线证据入口](C:/Users/tangx/Documents/Codex/2026-10-13/usd/outputs/step-evidence-refactor/index.html)。
最终qualification：`validation-20261016T025300Z-3c4cdcb9d625`，保存两版测试命令/日志、二进制和源码身份及新旧run关联。
最终04闭包（覆盖所有前置源码/工具/见证/测试）集合SHA：
`1ba285df07313591d1514039193cf1af69d9e5edd2fcfb8f0a98b66f557a9d10`。
早期失败尝试与候选包保留各自身份，不冒充最终版本。

散列与一致性证明不认证恶意改写者声称的程序执行。没有增加完整STEP/AP、SMLib构建、修复、GUI、公开接口、
worker取消/OOM或模型库长跑资格；05及后续票仍未启动。未推送，也没有远端issue/PR。
