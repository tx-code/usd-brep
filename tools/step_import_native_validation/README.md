# 05 原生导入验证

使用 01 冻结的输入、数学真值和完整 Q，通过真实 `step_native_import` CLI 验证结果。
读取原有 03 的延期/不支持清单；不依赖 OCCT，也不从 SMLib 输出生成期望值。

```powershell
$env:STEP_NATIVE_BINARY = '<build>/Release/step_native_import.exe'
python -m unittest tools.step_import_native_validation.tests.test_native -v
```

覆盖 49 个原始样件、16 组合法方向组合、非默认参数、非有理归一化、乱序/稀疏 ID、
最近点独立真值、调用/报告 N 与 N−1、最终 native 一致性边界和九种错误观测反控。
49 项当前为 1 Accepted、25 源 Rejected、17 Unsupported、6 Deferred；
17 Unsupported 包含 7 项既有源范围阻断及 10 项尚未授予目标资格的正例。
没有将 49 项“合同符合”解释成 49 个模型导入成功。

所有权和普通失败清理由 `source/STEP_IMPORT/tests` 在同进程接口验证；
CLI 子进程退出本身不能证明 native 对象释放。构建、日志及资源测量保留本机；本目录不建立发布包。
