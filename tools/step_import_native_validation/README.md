# 原生导入验证

使用冻结的AP203e2平面/盒体输入、独立数学真值和完整Q，调用真实`step_native_import`。
比较器从源STEP、固定的EXPRESS声明见证和数学定义计算期望，不从SMLib输出生成真值。
产品读取不依赖OCCT，本目录没有额外测试包发布协议。

```powershell
$env:STEP_NATIVE_BINARY = '<build>/Release/step_native_import.exe'
python -m unittest tools.step_import_native_validation.tests.test_native -v
```

完整首profile复核使用同一配置的四个真实CLI，输出到新的本地目录：

```powershell
python -m tools.step_import_native_validation `
  --source <step_inspect.exe> --semantic <step_semantic_inspect.exe> `
  --planar <step_planar_inspect.exe> --native <step_native_import.exe> `
  --output .local/step-import/qualification-release
```

命令重新执行全部49项的四层检查，不读取旧运行结果。各层独立比较器保持原样，
额外核对相同源图、P13/N34组合、以及真值明确列出的CLI查询。
仅全项匹配且源码/检查器/303项冻结材料/二进制与已观测runtime身份在运行期间不变，
才写入`qualification.json`的Matched；失败时保留已执行输出并非完整通过收据。
已有输出目录明确拒绝，不能混入上一轮结果。原始stdout/stderr/exit、命令、hash及逐项诊断留本地。
这里的源码身份是检出目录观测，不证明给定二进制由它构建；构建日志、匹配SDK身份和依赖检查另行保存。
本机Windows收据另绑定相邻SMLib/SM_API DLL与实际kernel路径；不据此推导其他平台依赖闭包。

CLI只暴露边界closest与材料分类，未暴露的面内UV分类明确不计本命令通过项。
owning模型/view/报告的真实寿命、独立副本、取消/预算以及进程内释放仍由C++合同和资源程序检查，
不能用CLI进程退出替代。组合P13也纳入C++声明owner与报告N/N−1检查。
总体P0—P7映射和剩余范围见[模块资格边界](../../source/STEP_IMPORT/README.md#首profile的资格边界)。

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

## 开放共享面片增量

原49项与303材料保持冻结；独立S1输入在`source/STEP_IMPORT/tests/fixtures/open_shared_sheet.step`，
共面相邻输入为同目录的`coplanar_shared_sheet.step`。真值来自明确矩形坐标，不使用CAD输出生成期望。单独运行：

```powershell
python -m unittest tools.step_import_native_validation.tests.test_sheet -v
```

核对6V/7E/2F、8逻辑use/16native EU、每边2/4 EU及自己的UV、6自由边、
整体面积/形心/唯一边长度和两个面的查询；比较器反控必须拒绝第一面属性冒充整体、共享边重复计长、
丢失第二面UV或错误材料资格。折角与共面各64组方向/面集合顺序/稀疏ID变换及SI重述单独检查。
共面情况还检查相异的UV原点/坐标轴、斜放世界平面、部分重叠/内嵌/完全重合的反控；
源检查通过的重叠矩形在native前以目标范围拒绝，不修改源规则或自动合并面。
复制共享边、错误方向、伪闭壳仍按原源诊断拒绝；跨过共享支撑线或额外触线的边界明确为目标Unsupported，
不能靠删点或凸包化继续发布。
C++公开接口合同及两类面片的普通失败/取消副本资源检查沿用现有机制，不建立新的材料打包或发布协议。
这项增量不改变Frozen49Contracts的计数，也不授予一般开放片或完整P2资格。

严格凸多边形输入为同目录的`convex_shared_sheet.step`（矩形＋梯形）、
`triangular_shared_sheet.step`（双三角）和`pentagonal_shared_sheet.step`（矩形＋五边形）。
独立坐标给出面积、面积矩/形心、边长；含根式的边长另用整数开方得到有理区间，
比较整个真值区间的最坏误差，不能用中点近似放宽Q。
检查可变V/E/use与逐实体容差、全部UV、折角/共面、方向和ID变换、SI重述、三维刚体变换；
反控拒绝漏顶点/use、矩形化、错形心/UV及材料晋升。原源层规则、49项及矩形面片分别回归。

`concave_shared_sheet.step`与`collinear_shared_sheet.step`保留同一五边形的全部源实体，
仅改变远端角点：凹口由面积7梯形减去面积2三角形，得到总面积17、
形心(44/17,79/51,4/17)、唯一边长16+3√5；共线样件仍是总面积19的梯形形状，
但保留额外顶点及两条分段边，两者均7V/8E/2F、9逻辑use和7自由边。
核对凹口外查询确实落在凹口边上、两张面同时含凹角、共面异侧/同侧、三维坐标变换、
单位重述与全部方向/身份变体；不能以凸包或合并共线边获得正确面积却丢失源语义。
源Checked的简单环与全部非共享顶点严格同侧共同证明共享支撑边条件；构建与资源检查沿原机制。

带孔两面使用`perforated_shared_sheet.step`与`multi_hole_shared_sheet.step`：前者两矩形各扣一个单位方孔，
净面积16、形心(85/32,25/16,5/16)、唯一边长29，14V/15E/2F、4环、16逻辑use、14自由边；
后者沿凹外环并分别扣两个/一个孔，净面积61/4、形心(637/244,1165/732,15/61)、
唯一边长25+3√5，19V/20E/2F、5环、21逻辑use、19自由边。真值由独立区域相减和面积矩获得。
`tests.test_perforated_sheet`核对单边带孔、全部孔的源身份/方向/UV、孔外最近点、折角/共面、
各内外环独立方向、面sense、SET顺序及稀疏ID、SI重述、负UV/旋转框架与斜放世界坐标。
孔接触外环/共享边、重叠、嵌套和错方向按原D4诊断拒绝；共享孔边的合法源在native前明确目标Unsupported。
C++合同核对孔内/边/角分类、独立建模副本仍保留孔、owner/view/report及全部预算/取消边界。
只有外环取得共享支撑边资格；孔严格位于其内，全部孔顶点另核实精确平面支撑，不依赖native validator补证。
这不授予一般多面片、共享孔边或缝的资格，不改变原冻结49项、源检查规则或测试包协议。
