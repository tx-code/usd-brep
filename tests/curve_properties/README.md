<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# 曲线属性测试

测试通过公开原生 `SmCurve` / `SmBSplineCurve` / `SmCircle` / `SmEllipse` 接口，
覆盖曲线求值、参数对应、反向、参数域编辑及裁剪区间的紧包围盒。
`probe.cpp` 仅构造曲线和返回观测，Python 独立判定几何关系。
不增加 Python 产品绑定，也不更改内核算法、精度、容差或默认依赖。

## 判据与验收

| 属性 | 独立预期或关系 |
|---|---|
| 多项式／正权有理 Bézier 的点、一阶、二阶导数 | Bernstein 基函数、齐次商法则及实际参数域的链式法则 |
| 多项式紧包围盒 | 区间端点及一、二、三次曲线导数的解析实根 |
| 圆／椭圆紧包围盒 | 角度区间端点及每个坐标的三角函数驻点，包含内部极值 |
| 圆／椭圆参数对应 | 实际 NURBS 参数转换到角度后的解析空间点，以及内部参数的往返对应 |
| 反向 | 点保持、奇数阶导数反号、返回的裁剪区间按自然域反射、包围盒保持 |
| 参数域编辑 | 点保持，一／二阶导数按域长度比的一／二次幂变化、包围盒保持 |
| 独立点求值 | `EvaluatePoint` 与带导数求值的空间点一致 |
| 正权有理包围盒 | 控制点凸包限制及选定参数的包含关系；不声称证明其紧致性 |

固定输入必须全部通过，所有输出必须有限。Bézier 空间点预算为
`1e-9 + 64 * epsilon * coordinateScale`；导数另加 `1e-9 * max(1, |expectedDerivative|)`。
输入尺度受生成器限制，该数值是测试预算，不是新的产品默认。
圆／椭圆沿用 `tests/conic_bounds/bounds.cpp` 的角度转换预算：
完整周期的坐标预算 `1e-10`，裁剪区间为 `2e-6 * maxRadius`，另加浮点坐标舍入预算。
参数域编辑不改变几何精度；测试不会用增大容差使失败通过。

解析极值判据适用于列出的曲线族和区间。采样包含、同一内核的蜕变关系及这些回归通过，
均不构成任意 NURBS 的连续误差证明，也不建立 STEP 导入正确性资格。
多段节点、周期接缝跨域、派生类回退和曲面内部极值继续由已有专项测试覆盖。

## 运行

SDK 的 `SMLIB_BUILD_VALIDATION=ON` 注册 `curve_properties`，使用同一 SDK 的库和布局宏：

```shell
cmake --build <sdk-build> --config Release --target curve_property_probe
ctest --test-dir <sdk-build> -C Release --output-on-failure -R '^curve_properties$'
python -m unittest discover -s tests/curve_properties -p test_harness.py -v
```

独立构建可指定匹配的 SDK `SMLIB_LIBRARY_DIR` / `SMLIB_RUNTIME_DIR`，或从当前源独立编译内核：

```shell
cmake -S tests/curve_properties -B <separate-build> -DCURVE_HARNESS_BUILD_KERNEL=ON
cmake --build <separate-build> --config Release --target curve_property_probe --parallel 4
ctest --test-dir <separate-build> -C Release --output-on-failure
```

Windows 消费者使用匹配的 C++20、Release `/MD` 或 Debug `/MDd` 和内核布局宏。
每个用例由独立子进程执行，设置超时；原生上下文活到曲线销毁之后。
测试不使用线程并发访问内核。构建产物、失败输入和本机运行收据保留在 Git 外。
