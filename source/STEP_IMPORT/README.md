# STEP_IMPORT（内部 C++20）

05—09 的 STEP→SMLib 闭环：单零件、canonical 毫米；源长度可SI前缀或已验证conversion链，
几何为开放 LINE/PLANE 平面及孔，或经过独立证明的矩形盒体边界，
基于 02—04 自研读取和源检查。没有 OCCT 读取依赖。

`ImportFile` / `ImportBytes` 显式接收 `Options`。先保存不可变原始文档，完成类型、单位、规则与区域检查，
再建立不含 SMLib 指针的中立计划。候选 context、BRep 与尚未移交的几何分别有所有者。
完成 UV、指针/LEVEL2 验证、独立后验检查和必需报告后，才发布 `Accepted` 与模型。

```cpp
#include "StepImport.h"

auto result = step_import::ImportFile(path, step_import::Options::PlanarFixture());
if (result.model) {
    auto face = result.model->Face();
    result.model.reset();
    auto closest = face.Closest({4.2, 9.6, 8.0}); // owning view 保留模型及 context
}
// result.report 独立拥有源材料、诊断、实际容差及规范化记录。
```

`PlanarFixture()` 是明确选择的合成样件配置 Q-P0-mm-01，不是生产默认值。
该名称的数值不可改写；其他配置必须另取 ID。无效 options 抛出 `invalid_argument`。
定义曲线和支撑面不拟合；源顶点、递增 LINE 参数、四层方向各自保留。
目标曲线重参数化到 [0,1]，源根式参数映射留在报告，UV 在拓扑构建后装到 SAME member。
06 保留各源 bound/loop 与每次 ORIENTED_EDGE 使用的有向顶点、实际 UV、SAME owner 和所在侧。
内核首环必须为外环；仅将源 SET 中的外环提前，源身份与声明方向不改，规范化政策显式记录。
孔沿源语义构建，不翻向、填充或放宽容差。负 UV 与平移后的源原点保持。
有限 window 为 trim 包围域加有记录的浮点保护；域外求值先拒绝，不使用内核夹取掩盖错误。
实际解析系数及 NURBS 控制点、节点向量共同用于全段/全域误差界，不能由点采样替代。
最终 native 顶点—曲线端点、顶点—平面和全段曲线—平面的一致性另行证明，不能由源残差或工作 tol 代替。
保守上界不足以证明 Q 时返回 `Indeterminate`，不会把它称为已证明的源缺陷。

模型没有编辑或 native 指针接口，面视图强引用模型，报告不保活 native BRep。
`MaterialVolumeMm3()` 对所有表面模型返回空值。面查询在同一模型内串行；尚未授予一般多线程内核资格。
07 的材料盒体按八角、三组正交边、六个完整外向矩形面证明，支持有理刚体变换和参数/顺序变化，
没有使用盒体 primitive 替代源几何。唯一源边只构建一次，其他面复用同一 native edge；
每面的 mate pair 有各自的 SAME UV，逐项核验实际 radial 邻接和区域/壳的 owner。
`Faces()` / `Face(index)` 返回带源身份的 owning 面视图，`FaceView::Info()` 为每面属性。
整体 `Info()` 的 normal/window 对多面形体为空，`Closest()` 查询整个边界。
`ClassifyMaterial()` 对表面返回空值；材料实体返回内/外/面/边/顶点，
native 工作容差给出的边界分类若不能满足查询误差则返回 Indeterminate。

08 保留 MSSR→SBSM→CLOSED_SHELL 的 `ClosedSurfaceModel`，与07同边界材料盒体分开资格。
闭合表面的内外空间区域均标记void；有界void是空间单元，不表示材料包围的空腔。
原生 `IsManifoldSolid()` 只检查拓扑条件，在此可为真；它不能授予材料资格。
`source_shape` 保存原表示/形状/壳身份与角色，`native_role` 保存实际区域/壳数量、材料/有界void数量及该原生谓词。
表面面积、面积形心和边界查询由各面量测汇总；材料volume/census/积分证据均不适用，不能用0冒充成功。
P04材料路径继续计算材料属性；P05全部材料分类返回空值/NotApplicable，即使边界封闭且具有几何内部。

材料体积预算取完整样件 Q 的 1e-5 mm³；解析源盒体、实际目标边界的区间通量积分与 native mass properties
分别对照。积分读取内核实际生成的有向有限边界副本，包围解析/NURBS 两种表示，
采用与 native 量测相同的 z 通量及 Green 原函数。每段为至多四次多项式，以区间三点 Gauss 精确积分。
逐段累加有限端点补缝和可能被 native 微小 du 分支跳过的通量绝对上界，避免靠不同接点的抵消放行。
补缝物理残差、有限域、曲面不翻折及总积分误差分别受限；不改写 UV、源边界或工作容差。
临时曲线副本逐次释放，报告的逐环映射不充当积分输入。
产品 JSON 为 `step-native-import.v2` / `PlanarNativeImport`，增加逐面、共享邻接和材料证据；
旧 v1 验收保留原资格，不引入测试包迁移。实际 source/semantic/planar 报告协议保持。
普通失败与调用/报告限额不会交付部分模型，结果中的诊断区分源拒绝、目标不支持、数值证据不足和 native 失败。
`nativeCalls` 只计构造、UV 安装、validator、主要测量的调用检查点；系数/拓扑只读访问不逐项计费，
它不是计时、总内存或硬取消限制。任意 OOM、崩溃恢复、worker、公开绑定均未在本票授予资格。

09保留源单位与参数：VP/LINE/PLANE位置统一到毫米，方向比和源LINE t保持无量纲，
源PLANE UV通过单独长度比例映射到目标UV。unit_normalization记录原单位、精确尺度、typed factor和共享定义链。
03的conversion类型快照仍Deferred，04补齐源量纲及正尺度/无环解释，完整Checked后才准入native。
名字不决定factor；零负尺度、环、量纲反证各归正确D3/D4，容差与误差预算继续是canonical单位。
可选声明的目标资格仍留10；
一般多面体、开放共享片、截面/切割及一般修复没有因材料盒体通过而自动获得资格。
原始来源仍保留；不支持时返回明确诊断，不静默少件。`Repair=None`。

## 构建与验证

根 Premake 注册两个项目。独立 CMake 可接收已有 `SMLib` / `SM_API` targets；本机 Windows 路径使用
匹配源码、配置、CRT、宏及工具链的 SDK，目录中分别为 `release/`、`debug/` 的 DLL 与 import lib：

```powershell
cmake -S source/STEP_IMPORT -B <build> -DSTEP_IMPORT_NATIVE_DIR=<sdk>
cmake --build <build> --config Release
ctest --test-dir <build> -C Release --output-on-failure
<build>/Release/step_native_import --fixture-limits --quality Q-P0-mm-01 input.step --query 4.2 9.6 8
$env:STEP_NATIVE_BINARY = '<build>/Release/step_native_import.exe'
python -m unittest tools.step_import_native_validation.tests.test_native -v
```

CLI 返回 0 Accepted、1 Rejected、2 Unsupported/参数错误、3 ResourceLimit、4 SourceFailure、
5 必需输出未发布、6 BuildFailure/调用异常、7 Deferred、8 Indeterminate、9 PostcheckFailure。
JSON 输出为 binary stdout，字节预算不受 Windows 换行转换影响。
测试直接沿内部公开接口和真实 CLI；没有另建测试包发布机制。
Windows 资源检查包含成功、各调用预算边界、后验预算不足、报告超限及保留模型的阳性对照。
当前匹配的共享调试 CRT、非 TBB SDK 下，Debug 以 CRT 用户分配判断残留，Release 以进程堆判断；
两者都保留总堆/PrivateUsage 观察及 64 模型正控。原全进程判据的无模型误报与修正依据见 08 验收，
该分配域的覆盖不能推广到其他 SDK 分配器。

本机已验证的 C++20 适配器配套原生内核仍为 C++17；不代表全仓升级或一般 ABI/跨平台资格。
`SmIterationValue::operator==` 仅补 `const` 以使头文件能在 C++20 equality rewriting 下编译，
函数原有恒 FALSE 主体与求解算法不变。详细实现和验收见
[实施合同](../../docs/step-import/ticket05-implementation.md)与
[05 验收](../../docs/step-import/ticket05-acceptance.md)和
[06 验收](../../docs/step-import/ticket06-acceptance.md)和
[07 验收](../../docs/step-import/ticket07-acceptance.md)。
08 的角色与原生表示约束见[实施合同](../../docs/step-import/ticket08-implementation.md)与[验收记录](../../docs/step-import/ticket08-acceptance.md)。
09 的单位/参数与转换解释见[实施合同](../../docs/step-import/ticket09-implementation.md)与[验收记录](../../docs/step-import/ticket09-acceptance.md)。
