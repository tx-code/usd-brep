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
多段节点与周期接缝见下节；派生类回退和曲面内部极值继续由已有专项测试覆盖。

## 多段、重复节点与周期接缝

`nurbs` 输入保留一次至三次、2..8 个非空跨度的 clamped NURBS，内部节点重数
`1..degree`，因此覆盖 C2／C1／C0 连接，不接受位置不连续的 `degree+1` 内重数。
`spline_oracle.py` 从实际二进制浮点节点建立逐段的精确有理系数多项式，再以齐次商法则
独立核对点、一阶和二阶导数。常权曲线逐段求导数实根，包围盒包含所有内部节点及极值；
一般正权曲线仍只建立控制凸包、采样包含和蜕变回归，未宣称独立证明紧致性。
所有数值预算沿用上表，不改变原生精度、求解设置或参数语义。

每个内部节点同时检查精确值、实际参数两侧的一个 ULP 邻点及左右两侧导数。
`SmBSplineCurve::Evaluate` 的既有 `bFromLeft` 指点在所选分段的左端：TRUE 是数学上的
右极限，FALSE 是左极限，见 `source/SMLib/inc/SmBSplineCurve.h` 的接口注释。
原生程序报告反向／参数域编辑后实际送入求值器的浮点参数；独立参考使用对应变换后的
实际节点和控制网。邻点经变换舍入到节点时，按该实际参数的一侧语义验收，不能假装
两次浮点变换可逆，也不能跨越导数跳变直接套用光滑点的链式比较。

圆／椭圆的跨接缝角度窗口通过现有 `ConvertIvlFromSTEPToNURBS` 转换，要求保留两个
原生区间，再核对其并集的解析包围盒。`periodic_nurbs` 是均匀周期二次 B 样条的
clamped 表达：由循环齐次控制点构造，保证接缝 C1，二阶导数可有跳变；不冒充任意
unclamped 周期节点向量。其接缝两侧独立求值，点与切线保持，跨周期的窗口显式拆分。
这些窗口不授权 `Evaluate` 在自然域外自动循环；原有域外参数行为保持。

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

## 随机生成、重放与缩减

`explore.py` 只依赖 Python 标准库。输入按曲线族构造，交替覆盖一至三次多项式／正权有理
Bézier、圆和椭圆。选择带偏移且不同长度的实际参数域、完整／不对称裁剪、坐标偏移、
不同尺度及两个正交坐标框架。它不靠内核拒绝大量非法输入来充数。
`validate` 给出本试点明确的有限输入范围；这不是 SDK 的支持范围限制。

```shell
python tests/curve_properties/explore.py run --probe <runtime>/curve_property_probe.exe \
    --seed 431 --cases 512 --output .local/curve-properties/run-431
python tests/curve_properties/explore.py replay --probe <runtime>/curve_property_probe.exe \
    --input <failure-directory>/minimized.json --output .local/curve-properties/replay
python tests/curve_properties/explore.py shrink --probe <runtime>/curve_property_probe.exe \
    --input <failure-directory>/failure.json --output .local/curve-properties/shrink
```

探索要求 probe 与匹配的 SMLib 运行库位于同一目录。收据记录原生二进制与 Python 工具的
SHA-256、Git 版本和工作区状态、Python 版本、超时、完整物化输入及成功观测。
Git 版本是调用时的源上下文，不能单凭它证明一个外部二进制的构建来源。
执行前后重新核对二进制哈希；改变时结果标记为基础设施错误。
运行中断保留 `running`，不能当成通过。失败立即保存完整输入、原始观测或进程诊断，
随后才尝试缩减。seed 用于重建探索序列，固定回放使用完整输入，不依赖 RNG 版本。
默认遇到首个失败停止，`selected` 与 `completed` 分开报告，未执行用例不算通过。

缩减尝试降低次数、简化控制点／正权、参数域、裁剪区间及圆锥曲线参数；仅接纳仍在输入域、
复杂度严格下降且触发同一个属性的候选。原生崩溃还须保留相同退出码。
默认上限为 100 次、30 秒，在两次调用之间检查；正在运行的调用仍受单例超时限制。
这是有界的贪心缩减，不保证全局最小。超时、崩溃、协议错误和几何属性错误分别保留。
每次运行创建新收据目录，拒绝覆盖；CTest 使用 `--unique-output` 创建独立子目录。

试点验收要求：固定属性全部通过；至少三个 seed、每个 512 个随机输入通过 Release，
另有 Debug 的固定用例及随机抽样；失败回放命中相同属性，缩减保留该属性并降低复杂度。
定向变异必须由几何属性断言发现，编译失败、超时或基础设施错误不算抓到变异。
这些数量是该工具的可重复验收流程，不是对全库缺陷率或生产模型的资格保证。

## 定向代码变异

```shell
python tests/curve_properties/mutations.py --revision HEAD \
    --output .local/curve-properties/mutations --config Release --parallel 4
```

该命令将指定提交的内核与测试一起导出到全新目录，独立编译未变异基线，再依次编译三个
真实的单文件生产代码变异。测试判据保持冻结，不修改工作区或复用其他构建树的对象文件。
运行前检查每个替换锚点恰好命中一次，适配原有 LF／CRLF，保存所有源文件的 SHA-256，
并核对每次编译只改变指定的生产文件。二进制哈希不变不能当作有效变异。

| 变异 | 必须触发的属性 |
|---|---|
| 重新加入裁剪区间外的节点 | `analytic_bounds` |
| 反向区间下端点错误地使用原下端点 | `reverse_interval` |
| 丢弃已请求的二阶导数 | `bernstein_d2` |

验收要求三个变异均由指定几何属性发现；各自缩减后的完整输入必须在变异版本上命中同一
属性、在保存的未变异基线上通过。编译错误、进程错误和超时不计入检测成功。
最后逐字节恢复导出的源，重建并重跑固定属性。导出源没有独立 Git 根时，探索收据的
Git 版本留空，由实验的固定提交和源清单归属；不借用父目录仓库的版本作证明。
这是三个明确故障类别的检测实验，不是全库 mutation score 或全部错误类别的覆盖率。
常规 CTest 只运行变异工具自身的合同检查；完整编译实验需显式执行。
