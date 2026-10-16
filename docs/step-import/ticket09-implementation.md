# 09 实施合同：源单位、物理几何和参数

2026-10-17，用户在08完成后要求继续，开始09，基点 `712e4cc`；10—12保持on-hold。
继续C++20自研读取，不依赖OCCT；沿已确认的ImportBytes/File、模型/面视图查询及03/04/native CLI验收。
不改冻结输入、真值、质量Q、SMLib/SM_API或测试包机制。

先完成P06/P07的SI厘米红绿闭环，再完成P11的转换单位和N31/N32/N33的因果拒绝。
源点与PLANE位置转换为canonical毫米，方向保持无量纲，VECTOR magnitude按源长度解释；
源LINE无量纲参数不重复缩放，目标参数仍[0,1]，源PLANE UV和目标毫米UV分别记录映射。
所有工作容差、派生误差、几何查询、面积与体积预算继续使用canonical单位。

03保留已验收的源类型/产品/SI检查合同。它的conversion结果仍明确Deferred并保留完整typed快照；
04仅在binding/types/part已通过、SI单位解释明确Deferred且无诊断时接续conversion，
执行固定schema必要量纲规则，再执行项目正尺度/无环解释和几何检查。
04的完整单位解释是native准入前提，不能把上游Deferred本身当作通过。
不把Rational反向放入03而制造层依赖，也不改写03的历史资格。

CONVERSION_BASED_UNIT的显式dimensions按七个精确指数比较；源WHERE不递归factor链。
单位尺度另行按typed measure和unit_component有界递归到SI根，共享子链记忆化，明确识别环。
数值容量不足返回Indeterminate，量纲源反证归D3，零负尺度及环归项目D4；
不按inch名字猜127/5，不取绝对值、补单位、静默修复或扩大预算。

单位解释和每条factor链的来源随证据/导入报告保留；Source snapshot原字节与源参数保持。
本票不授予optional uncertainty目标资格：P13组合正例仍受10/12门槛，
通用环检测可提供N34源级诊断，完整组合端到端资格留12。

完成SI/转换与共享链、错误标注名字、量纲/零负/环/预算反控后，执行双配置相关回归与两轴审查，
仅本地提交。10可选来源声明、11进一步取消/生命周期和12汇总不自动开始。
