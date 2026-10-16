# 03自身复用绑定字段

接续局部规则整理，固定审查点`5894f45`，使用C++20。
本切片沿既有Check、Snapshot字段查询与真实CLI验证，不扩大公开接口。

- 仅迁移Fields完成后的Product、Closure、UnitContext的字段读取、已验证引用和字段位置。
- Checker与Snapshot共用已有绑定的查找逻辑，不构造提前发布的Snapshot，不建第二套字段表或实体图。
- 删除业务消费者的数字属性槽位及按组成查参数；原值、可选/派生标记、引用身份和源顺序保持。
- Fields及其之前的原始槽位/引用类型检查保留，不依赖尚未完成的绑定来验证绑定本身。
- 保持源结果、失败门槛、首错、诊断内容/span/component/path、原刻意省略的字段详情、预算及退出码。
- 不更改人口/闭包算法、单位政策、04规则/几何、数值算法、冻结材料、独立见证或测试包机制；不进入05/SMLib。

先沿旧版CLI锁定继承/complex字段的失败来源与聚集成员诊断，再验证迁移后的同一合同。
末尾完成C++20 Debug/Release组件检查、01—04核心检查、两配置03/04的49样件字节比较，
代表性依赖/工作预算比较和Standards/Spec审查。本地提交，不推送。

## 完成记录

2026-10-16，实现提交`afea9a2`。Checker晚期产品/闭包/单位解释已复用既有绑定和值身份索引；
与Snapshot共用FindFieldBinding，删除ValueAt、组成查参数和独立ComponentIndex。
FieldFailure从绑定取得组成/参数位置，明确保持原SpanOnly与AttributePath诊断政策。
Scalar仍在绑定完成之前使用原源ID索引，后期引用只认已验证的原Value。

- C++20/MSVC Debug/Release CTest均通过，每配置12源、18语义、15平面合同组。
- Python3.14的01—04核心138项全部通过、无跳过；新增两组CLI合同先在旧版通过，
  再验证迁移后的继承/complex位置、聚集成员span及原字段路径。未虚构行为重构的新功能红绿失败。
- 03/04各49样件、两配置共196次stdout/stderr/退出码比较同旧；169个依赖/工作预算边界也逐字节一致。
- 现有字段视图、限定声明查找、错误查询、所有权与稀疏/重排输入检查保持通过；clang-format与差异检查通过。

### Standards

0项发现。共享查找限于已有绑定，早期原值与后期解释职责清楚，诊断详情差异有明确类型，没有额外公开接口。

### Spec

0项发现。字段名逐项对应原槽位；首成员0的optional处理、SI组成、绑定完成门槛、原异常/所有权保持。
原循环、短路与计费未改，快照没有提前构造或发布。

验证脚本、日志和旧输出保存在本机
`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/semantic-field-consumers/`，
包含validate.py及validation.json。测试包机制、01材料和独立见证未改；未进入05/SMLib或新增完整导入资格。
本地提交未推送。
