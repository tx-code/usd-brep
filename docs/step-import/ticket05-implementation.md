# 05实施合同：owning开放平面

2026-10-16，用户在后续计划后要求继续，05已领取。基线`1d604a8`；01—04、Q和原样件保持。
本轮只做05，不修改SMLib算法、测试包机制或06—12的执行状态。
原生头文件 `SmIterationValue::operator==` 必须补 `const` 才能通过 C++20 头文件编译；
函数体仍为原来的恒 FALSE，内核求解算法不变，匹配 SDK 的 Debug/Release 已重建。

## 调用与验收界面

新增内部STEP_IMPORT，沿ImportBytes/ImportFile、只读ModelSnapshot/FaceView查询及原生CLI验收；
不增加私有函数测试口，不暴露owned native指针或可编辑BRep，不新增SM_API/Python/GUI接口。
Source snapshot→已检查的中立几何/拓扑计划→候选context中的受控SmBrepConstructor→独立后验核验→提交。
源检查失败保持原诊断，不运行native；未支持的目标形状明确Unsupported，不丢件或自动修复。

05限定单个开放面、一个外环、LINE/PLANE、SI毫米及最小产品/context声明；源状态/角色和原始图保持。
可选来源声明的目标资格留10，当前明确Unsupported，原声明仍在owning源图中保留。
曲线、顶点和plane的定义分别保留；不得拿偏离定义线的两个顶点直接拟合一条新线。
孔、多面实体、conversion等目标资格留后续票。合法方向组合和非默认参数不按样件ID硬编码。

## 精度与所有权

完整Q-P0-mm-01显式选择：定义1e-8mm、一致性/派生UV/查询1e-7mm、面积1e-6mm²，
初始BRep/V/E/F tol=1e-5mm、上限1e-4mm、干净例增长0、Repair=None；不读取源uncertainty补齐。
浮点转换和仿射定义/UV用保守误差界核验，不能用有限采样替代全段结论。
有限window只限制求值范围，覆盖完整trim且不夹取错误UV；窗口保护政策显式记录。

每候选单独拥有context和BRep；未移交几何由局部guard持有，移交后由BRep拥有。
销毁先BRep后context；源、报告和FaceView的生命期独立可验证。
必需报告受字节预算限制，完整后才交付模型；资源拒绝和后验拒绝均销毁候选，没有半模型。

## 需要真实通过的门槛

1. 匹配当前源码的SMLib/SM_API及配置重建，记录源码/二进制/宏与实际加载路径，不复用旧研究DLL冒充通过。
2. F1实际1F/4E/4V、面积96、边界40、bbox/法向/参数、独立最近点与分类；保持开放surface角色。
3. 逐源ID/occurrence对照native身份、方向、实际域；fresh 3D定义、UV后装SAME member及owner核验。
4. 构建、UV安装、ValidatePointers、LEVEL2/SM_WALK及后验查询前后逐对象实测tol；异常增长拒绝。
5. 几何定义、仿射lift、区域/域和误差界分别核验，validator返回成功不能代替。
6. 普通失败、native调用/报告限额和完整候选拒绝；视图保活、报告独立存活、旧结果/源不变。
7. 反复成功/失败的资源测量有分配阳性对照；未测内存字段保持未测，不宣称任意OOM或崩溃恢复。
8. 01—04回归、05独立真值/反控、规范与规格审查；原票实际通过才勾选关闭。

构建产物和运行记录留本机，不扩展打包协议。当前05仍在实施，本文不是验收通过记录。
