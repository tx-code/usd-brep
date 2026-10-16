# STEP 导入设计附件

已确认总体基线：[独立STEP导入与受限修复规格0.1](../step-import-repair-spec.md)，原文保持不变。
当前01—04已实现，三个STEP目标采用C++20；进入05的前置和剩余资格边界见
[05之前准备完成记录](pre05-readiness.md)。05及SMLib构建尚未开工。

下表是原设计附件，保留当时Designed/NotRun语境；其中“未生成样件／未编码”描述设计阶段，不能替代当前实施状态。

后续已授权的实施状态单独见[01](ticket01-acceptance.md)、[02](ticket02-acceptance.md)、
[03](ticket03-acceptance.md)、[04](ticket04-acceptance.md)、
[证据包重构](evidence-runner-refactor-acceptance.md)和
[有界报告与位置重构](report-mechanics-refactor-acceptance.md)。下文设计阶段的NotRun是原阶段记录。

| 材料 | 内容 |
|---|---|
| [P0平面/盒体切片](p0-planar-slice.md) | 能力包络、根/schema登记、属性清单入口、单位/参数/方向和验收顺序 |
| [独立真值与用例](p0-planar-cases.md) | 明确几何和有向拓扑、测试专用Q、正例/拒绝例/工程门槛，全部NotRun |
| [实体属性来源](p0-entity-source-audit.md) | 38类几何/拓扑/表示/产品记录的精确字段、类型/聚集/派生、局部限制与主源 |
| [单位/context来源](p0-unit-source-audit.md) | 15类记录、complex字段和单位/轴/源参数的依据 |
| [SMLib映射来源](p0-smlib-source-audit.md) | 固定当前源码的构造/UV/window/方向/所有权事实与未执行门槛 |
| [Schema候选与冻结门槛](p0-schema-profiles.md) | 已取得的发布文件、证据等级、实际版次差异、规则闭包和后续顺序 |
| [AP203来源](p0-ap203-provenance.md) | e2官方当前文件与e1修改副本、optional/单位/旧context及管理约束差异 |
| [AP214来源](p0-ap214-provenance.md) | e3迁移后的官方下载、派生dimensions、OID/APD冲突及必要规则入口 |
| [AP242来源](p0-ap242-provenance.md) | e1/e2/e3发布文件、OID、APD及shape/单位规则差异 |
| [AP203e2首个profile](ap203e2-planar-profile.md) | 封闭输入清单、36种simple叶与六complex签名、必要/不触发依据、验收控制及完成边界 |
| [AP203e2全局规则](ap203e2-planar-global-rules.md) | N2635全部47条RULE：5必查、1条件必查、41项有守卫的不触发证明 |
| [AP203e2产品/context/单位闭包](ap203e2-planar-context-closure.md) | 精确字段、祖先、SELECT、INVERSE/UNIQUE、单位签名及函数依赖 |
| [AP203e2几何/拓扑闭包](ap203e2-planar-geometry-closure.md) | 祖先与局部约束、AF/ABSR/MSSR各WHERE、函数和源文件缺陷边界 |
| [AP203e2样件总册](ap203e2-sample-design.md) | 固定HEADER／12条共同记录、13个正例、独立真值、逻辑检查阶段与诊断合同 |
| [平面样件记录](ap203e2-sample-plane.md) | F1全部记录及F2孔／F3重参数增量，DATA设计数51／81／81 |
| [盒体样件记录](ap203e2-sample-box.md) | B1全部字段／有向使用／平面frame、B2角色替换，DATA设计数145 |
| [单缺陷反例目录](ap203e2-sample-cases.md) | 36项指定父例与old/new、独立反证、最早诊断及连带后果 |
| [反例诊断审计](ap203e2-sample-negative-audit.md) | 单注入与单规则区别、误报风险、不能当反例的合法变化及候选依据 |

来源证据、数学推导、项目切片限制与动态通过分别记录。P0-A0.4在已有AP203e2静态闭包上细化13正／36反共49项样件设计；这是对63条主用例和C203控制的具体化，不把重叠数量相加作为通过数。由于N2635普通MSSR面分支的访问问题，本profile两种表示均只准ADVANCED_FACE，普通FACE_SURFACE留后续。

上述设计附件发布时只有Markdown记录，样件与控制尚未运行；后续01已冻结实际STEP及独立真值，02—04已取得其限定范围的源检查证据。
当前完整导入/native模型、模型库长跑和总体P0退出仍未获得资格；当前事实以各验收及准备记录为准。
