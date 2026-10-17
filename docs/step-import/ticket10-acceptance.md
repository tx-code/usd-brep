# 10 验收：可选来源声明与旧AP203类型

2026-10-17（Asia/Shanghai）。固定审查基点 `d492334`，实现 `8b8282b`。
10四条原验收完成，11—12保持on-hold。仅本地提交，未推送。

## 交付与来源边界

自研STEP入口现在接受已通过03/04源检查的可选声明，交付原有owning只读模型。
移除native层原先的可选声明范围门槛，源类型、属性、WHERE与项目几何检查全部保持。
01冻结输入、独立真值、质量Q和02—04实现均未改，没有OCCT读取依赖。

`Report::declarations` 拥有完整只读typed语义快照，和声明根的完整出向引用依赖闭包。
根包括应用/协议、产品/定义及其context、formation、part分类、表示context、named units和uncertainty。
记录按输入顺序保留；所有引用依赖都有记录，源身份、原始字节、位置和实际/继承类型不改写。
JSON `source_declarations` 提供相同记录、名义单位分配和单独的uncertainty观测。

C++调用者可从 `declarations.semantic->Field(...)` 读取属性及其声明类型，
用 `TypesOf` / `IsA` 区分旧子型与基类；完整源快照同时保留HEADER和全部实体。
报告不持有native指针；模型/面视图全部释放后，来源声明和原始字节仍可读取。
报告字节不足时销毁完整候选，不交付只含部分来源的成功结果。

## 正例与独立比较

| 01样件 | 保留的声明 | 几何与查询 |
|---|---|---|
| P08 | 两条不同名称的uncertainty，原值、各自unit、typed value及description缺席状态 | F1不变 |
| P09 | PRODUCT_RELATED_PRODUCT_CATEGORY的part名称、产品引用和description | F1不变 |
| P10 | MECHANICAL_CONTEXT、DESIGN_CONTEXT、PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE及继承属性 | F1不变 |
| P12 | PLANE_ANGLE_UNIT/RADIAN与SOLID_ANGLE_UNIT/STERADIAN各自身份和context分配 | F1不变 |

四个样件分别用01独立truth和完整Q比较：1F/4E/4V，面积96 mm²，边界40 mm，
bbox(-3.4,-2,5)→(10.2,12.4,5)，形心(3.4,5.2,5)。
点(4.2,9.6,8)的最近点为(4.2,9.6,5)，距离3 mm；源参数、UV、误差证明和工作容差沿用原合同。
P01缺席所有可选分支仍合法，空uncertainty列表和缺席solid-angle单位明确保留。

独立比较器从原始STEP的记录/引用及哈希固定的N2635声明见证计算类型继承和依赖闭包，
逐字节核对来源记录与位置；不从产品返回的类型或闭包生成期望。
旧context不因为deprecated变成基类记录，SOURCE的MADE/BOUGHT/NOT_KNOWN三枚举原样保留。
rad和sr的七维指数都为零，仍按不同名义类型保留，不能因量纲相同合并。
源记录倒序和稀疏ID控制、15种错误来源/数值/类型/单位/告警观测反控均纳入验收。

## Uncertainty政策与诊断

每条uncertainty使用自己的unit_component，保留typed原值、单位、可得的精确尺度及毫米值，
只与显式Q的一致性预算比较。`Within`无告警；`Exceeds`产生独立的
`SourceUncertaintyExceedsConsistencyBudget`，不进入错误diagnostics。
若附加声明的canonical尺度/值或比较超出当前有理数容量，明确 `Unestablished` / null及
`SourceUncertaintyComparisonUnestablished`，原始声明完整保留，不伪造数值或改变几何资格。

P08的0.0001/0.0002 mm均高于Q的一致性1e-7 mm预算，因此告警，工作容差仍为1e-5 mm。
独立控制让第一条uncertainty改用额外厘米单位：0.00001 cm仍为0.0001 mm；
额外毫米单位的1e-8 mm则Within。EXA米单位的1.0无法用当前有理数表示毫米尺度，
仅附加比较Unestablished，F1仍按原Q验收。
将F1顶点偏离1e-5 mm，即使小于源uncertainty，仍在D4按原Q拒绝，native调用为0。

| 输入 | 原准确诊断与阶段 |
|---|---|
| N29，uncertainty同名 | SourceRuleViolation:consistent_uncertainty.WR1，D3 |
| N30，uncertainty非正 | SourceRuleViolation:UNCERTAINTY_MEASURE_WITH_UNIT.WR1，D3 |
| N35，非profile的part分类 | UnsupportedProductCategory，D2 |
| N36，普通产品context超出机械学科范围 | UnsupportedProductContextDiscipline，D2 |
| 旧MECHANICAL_CONTEXT的错误discipline | SourceRuleViolation:MECHANICAL_CONTEXT.WR1，D3 |
| 旧DESIGN_CONTEXT的错误lifecycle | SourceRuleViolation:DESIGN_CONTEXT.WR1，D3 |
| 非法SOURCE枚举 | EnumerationValueInvalid，D1 |
| 新PRODUCT_CATEGORY_RELATIONSHIP关系 | UnsupportedEntityType，D2 |

这些阻断均保留准确源位置，native未启动；不静默忽略新关系，不添加其他AP的管理规则。
SOURCE枚举和旧类型控制没有更改原03/04检查顺序或诊断。

## 最终回归与资源

Debug/Release均实际重建并通过15个CTest目标及35组真实CLI/独立Python检查，无跳过。
15个CTest目标为三个旧源组件合同、六个native接口合同、六组同进程资源检查。
新增声明接口合同验证报告/模型共享同一不可变源、各分支完整报告N/N−1、独立来源寿命和原工作tol。

49项当前native合同为13 Accepted、29 Rejected、7 Unsupported，无Deferred；所有13个正例均可运行。
P13可沿通用路径执行，正式09/10组合和整个首profile的资格仍留12；
本票没有人为增加09→10阻塞边，也不由“49合同符合”推导49个模型导入成功。

02—04及其独立工具源码与09完全一致。最终运行逐文件核对58份未变源码后，显式复用09已通过的
每配置142项核心Python及316个历史预算边界结果，不声称10重新运行这些未变组件的Python/字节比较。
旧三个组件C++合同则在10两配置实际重建、执行；01材料/数学核验由本轮native测试实际调用。

每配置F1/P03/P04/P05/P11/P08分别测量880/1552/4000/3872/4000/880次同进程导入，
混合成功、逐native调用限额、后验预算不足和必需报告拒绝。新增P08仍为55个native调用检查点，
声明解释/序列化不改变几何调用计费。保留64个模型作阳性对照，释放后回到持有前基线。

| P08接受判定的分配域 bytes / blocks | Release，进程堆 | Debug，共享CRT用户分配 |
|---|---:|---:|
| 测量开始 | 1,931,231 / 4,109 | 65,536 / 1,085 |
| 测量结束 | 1,931,231 / 4,109 | 65,536 / 1,085 |
| 持有64模型前 | 1,931,231 / 4,109 | 65,536 / 1,085 |
| 持有64模型时 | 9,458,271 / 62,925 | 9,522,304 / 139,261 |
| 释放64模型后 | 1,931,231 / 4,109 | 65,536 / 1,085 |

Debug P08本次总堆也保持2,047,016字节/4,908块。全六组的五次采样、总堆/CRT/PrivateUsage和正控原值均留本地。
沿用08匹配共享调试CRT/非TBB SDK的观察器，原4096字节/8块增长限额和65536字节/256块正控门槛不变。
这些结果只授予当前分配域的普通失败清理证据，不推广其他SDK分配器或任意崩溃/硬取消。

## 两轴审查

### Standards

只读审查 `d492334...8b8282b`：规范违规0，可行动的smell建议0，阻断0。
来源解释与序列化留在现有准备/报告边界，没有新增内核所有权或无需求的公共抽象。

### Spec

同一固定差异只读审查：遗漏/部分实现0，未授权扩展0，错误实现0，阻断0。
完整typed来源/闭包、独立所有权、uncertainty自身单位及容量分支、旧类型/枚举和P13范围符合10票。
两轴未编辑文件或执行测试；实际运行由主执行进程记录。

## 身份与范围

内部STEP组件/适配器为C++20，沿用VS18/v145/MSVC19.50及05匹配的原生SDK，SMLib仍为C++17。
不改kernel/SM_API、生产默认值、修复预算、测试包机制或全仓C++标准。
二进制、日志、资源原值和最终收据只留本地；没有远端issue/PR或推送。

最终运行终态exit 0，绑定实现8b8282b；验证前后79份源码身份一致，SDK源码manifest、
DLL哈希、实际加载路径和DEPENDENTS均匹配，无OCCT/STEPcode/USD读取依赖。收尾提交仅修改文档/票据。

Release CLI SHA256：`dd568bdc6a5386986f5df20634b49e4c8998c147a2e1e176a5d29793184cbc62`。
Debug CLI SHA256：`366f96433080a30d4cc24b0d4b4d58dc5e339edbe275aa7ace36d8de16ed6030`。

本机根目录 `C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/`：

- `ticket10-qualification/validation.json`：两配置实际15目标/35组CLI、六组资源和10种实际query，
  源码/二进制/SDK身份及09复用条件。SHA256 `d79e73651fa7d3cfea7bfe18fe0558f038adbd266e5f0c584ac3c7ee315579b9`。
- 复用09收据SHA256 `9baef5dd9c9d97246d16d92e294813a0339232bf24cb87fa35919d8d1a79cf50`；
  每配置142核心检查、316个历史预算边界的原日志与源码身份仍保留，不新造发布/迁移协议。
- 同目录保留全部stdout/stderr、CTest细节、实际声明/query与DEPENDENTS输出；
  编排脚本为 `ticket10-qualify.py`，顶层日志为 `ticket10-qualification-run.log`。
- `ticket10-optional-red.log` / `ticket10-optional-green.log`：四个原始可选正例由Unsupported到Accepted并通过查询。
- `ticket10-declarations-red.log` / `ticket10-declarations-green.log`：报告缺source_declarations到完整来源合同通过。

新增关系控制首轮曾把既有UnsupportedEntityType责任阶段误期望为D1；核对源层合同后改为正确D2，
未更改产品或冻结诊断。最终两配置完整回归通过。

11进一步生命周期/取消和12首profile汇总尚未开始；不自动授予一般AP/几何、修复、建模/切片、
公开绑定、GUI、worker、任意OOM/崩溃/硬取消恢复或全内核C++20/23资格。
