# 05—08 原生导入验证

使用 01 冻结的输入、数学真值和完整 Q，通过真实 `step_native_import` CLI 验证结果。
读取原有 03 的延期/不支持清单；不依赖 OCCT，也不从 SMLib 输出生成期望值。

```powershell
$env:STEP_NATIVE_BINARY = '<build>/Release/step_native_import.exe'
python -m unittest tools.step_import_native_validation.tests.test_native -v
```

覆盖 49 个原始样件、16 组合法方向组合、非默认参数、非有理归一化、乱序/稀疏 ID、
最近点独立真值、调用/报告 N 与 N−1、最终 native 一致性边界和九种错误观测反控。
06 增加 P02/P03 孔与负 UV、32 组孔方向/源 bound SET 重排控制、解析双孔控制，
逐 loop/use 身份与实际 UV 对照，以及五种遗漏/错绑/错误 owner/平移 UV 的观测反控。
07 增加材料盒体的共享边/24次有向使用/逐面几何与UV/材料 census/独立体积区间，
源顺序/稀疏ID/边方向、平面内及三维倾斜刚体变换、斜盒与闭合表面不推广、内外与最近点，
以及13种错误材料观测反控（包含有限边界补缝与微小 du 省略的通量证据）。
08 增加P04/P05独立同形对照、闭合表面源链和native材料标志、材料查询不适用、
源顺序/稀疏ID/参数/刚体控制及11种错误角色/材料化/来源/证据观测反控。
当前每配置23组，产品报告为v2（新增角色观察，表面体积证据政策显式NotApplicable）；原始三层源报告及测试包机制不改。
49 项当前为 5 Accepted、25 源 Rejected、13 Unsupported、6 Deferred；
13 Unsupported 包含 7 项既有源范围阻断及 6 项尚未授予目标资格的正例。
没有将 49 项“合同符合”解释成 49 个模型导入成功。

所有权和普通失败清理由 `source/STEP_IMPORT/tests` 在同进程接口验证；
CLI 子进程退出本身不能证明 native 对象释放。构建、日志及资源测量保留本机；本目录不建立发布包。
