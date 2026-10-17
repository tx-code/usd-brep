# 原生导入验证

使用冻结的AP203e2平面/盒体输入、独立数学真值和完整Q，调用真实`step_native_import`。
比较器从源STEP、固定的EXPRESS声明见证和数学定义计算期望，不从SMLib输出生成真值。
产品读取不依赖OCCT，本目录没有额外测试包发布协议。

```powershell
$env:STEP_NATIVE_BINARY = '<build>/Release/step_native_import.exe'
python -m unittest tools.step_import_native_validation.tests.test_native -v
```

## 可观察契约

当前49项合同为13 Accepted、29源Rejected、7源范围Unsupported。
合同符合不表示所有模型都导入成功，也不自动授予完整AP/一般几何/修复资格。
conversion在STEP_SEMANTICS仍明确Deferred，STEP_PLANAR解释及几何完整Checked后才准入native。

比较覆盖精确几何及参数、孔、源UV、共享边/邻接、材料与表面角色、完整声明和引用依赖闭包、
单位factor/量纲及canonical预算，以及全域/全段误差证明。
源顺序、稀疏ID、合法方向、重参数和刚体变换控制防止样件分派；
伪造几何/角色/来源/参数/预算/告警的观测必须被独立比较器拒绝。

## 生命周期与限额

C++合同沿公开内部接口验证模型/view/报告寿命、源捕获、冻结配置、独立可编辑副本、
所有安全边界取消、真实提交竞争、源码额定限额及报告N/N−1。
observer异常和意外bad_alloc与已配置限额区分；实际几何定义不受取消或后续失败修改。
进程内存软上限、deadline和未支持的硬内存契约通过CLI及结构化诊断核对。
当前接口语义见[STEP_IMPORT](../../source/STEP_IMPORT/README.md)和其公开header。

Windows资源程序在同进程中预热、重复成功/失败/取消/副本释放，保存全部采样及64个保留模型正控。
Debug使用匹配共享CRT、非TBB SDK的用户分配域；Release观察进程堆。
总堆、CRT、PrivateUsage、进程生命周期峰值与采样趋势分别记录；后两者不归因于单请求。
CLI子进程退出本身不证明native对象释放，普通安全边界清理也不证明任意OOM/崩溃/硬取消恢复。

运行日志、源码/二进制/SDK身份、逐轮计数和实施回顾保留本机，可放被忽略的`.local/`。
复用旧验证时必须核对适用源码身份并显式说明复用范围。
