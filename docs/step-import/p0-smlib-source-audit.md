# P0：首个 LINE／PLANE 切片的 SMLib 构建契约审计

日期：2026-10-15。源码固定为 `f0d3897388d99a652dc3e1c0f3f52eccdbc1db6a`。

本次只读当前源码及既有合成探针报告，没有编译、运行新探针、修改生产代码或读取真实模型库。受控 `SmBrepConstructor` 已是规格 0.1 首个平面／盒体切片的路径；本文细化其适配约束，不重新选择后端。源码事实、既有动态证据、独立数学推导和将来验收条件分开记录。

## 1. 结论与证据范围

首个切片可以限定为：中立层保留源 LINE、PLANE、拓扑身份和全部方向语义；适配层在单次候选 context 中为每条目标边、每个目标面材料化独立的 native 定义几何，显式指定顶点／共享边及参数区间，关闭 stitching，延后逐 occurrence 安装精确派生 UV。构建结束仍须逐项核对源语义，不能用 builder 成功或通用 validator 成功代替。

既有 top-down 探针动态证明了两共面片的共享边／反向 use、四面体、单空腔及相应有限方向组合；没有读取 STEP，也没有构建本次将定义的完整平面／带孔面／盒体验收族。特别是孔、所有 source sense 组合和有限域边界负例需要 P2 运行验证。完整证据见[既有构建研究](../wayfinder/step-import/builder-feasibility-assessment.md)及本机原始报告：

- [top-down 报告](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/builder-feasibility/topdown/findings.zh.md)。
- [data-path 报告](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/builder-feasibility/data-path/findings.zh.md)。
- [cleanup 报告](C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/builder-feasibility/cleanup/findings.zh.md)。

这些探针加载既有 `SMLib.dll`，SHA256 为 `8cd99a41d961734baf2925879e5f46382d83097dd568b6924e75dbaf351068b2`，不是本轮从固定 HEAD 新编译的二进制。其头文件宏不等价于 DLL 的运行时配置查询。

## 2. LINE：源参数与 native 参数必须分别记录

### 2.1 已核对的 native 定义

公开 [SmLine.h:64](../../source/SMLib/inc/SmLine.h:64) 给出两点线的方程 `C(t)=A+t(B-A)`、默认域 `[0,1]`；工厂接口位于 [SmLine.h:90](../../source/SMLib/inc/SmLine.h:90)。[CreateLineSegment 实现](../../source/SMLib/src/SmLine.cpp:221) 计算 `V=B-A`、单位方向 `V/|V|`、scale `|V|`，显式建 `[0,1]`；非空 `pOptInterval` 是**输入重参数域**，不是输出参数。因此首个切片传空指针，不把该参数当作查询结果。

[GetNaturalInterval](../../source/SMLib/src/SmLine.cpp:796) 在已物化 NURBS 时返回 B-spline 域，否则返回 analytic 域；[EvaluatePoint](../../source/SMLib/src/SmLine.cpp:1131) 先把 NURBS 参数转换为 analytic／STEP 参数，再代入直线公式。即使两点线预期两个域一致，适配器也应读取并核对实际自然域，不把所有 SmCurve 都假定为 `[0,1]`。

`SmLine` 的 STEP 参数是该 native 对象的 analytic 参数；它不自动等于输入 STEP 文件的 LINE 参数。线段采用 `[0,1]` 是目标表示规范化，需保留可逆的源参数映射。

### 2.2 首片映射建议（独立数学推导）

设源直线为 `C_s(λ)=P_s+λ*m_s*d̂_s`，长度换算系数为 `k`（源长度单位到 mm），所选边的参数上下界为 `λa<λb`。目标线段可取：

`A=k*C_s(λa)`，`B=k*C_s(λb)`，`C_n(t)=A+t(B-A)`，`t=(λ-λa)/(λb-λa)`。

该策略保持源 LINE 的递增方向；源 topological edge 的端点顺序、`EDGE_CURVE.same_sense` 和 occurrence 遍历仍在中立层独立保留，不以改变定义线方向掩盖。必须预先证明端点位于定义线上且符合源方向／区间，不能把偏离源线的源顶点直接连起来并称为严格导入。

当源点、方向或区间不合法，或两端间距不足以满足目标及质量配置要求时，应在 native 构建前拒绝；工厂能返回退化线或错误不构成导入器的退化边政策。P2 应以源解析方程和目标实测自然域检验该映射。

## 3. PLANE：native UV 的毫米含义是适配政策

### 3.1 已核对的构造与求值

[SmPlane.h:34](../../source/SMLib/inc/SmPlane.h:34) 定义：

`S_n(u,v)=O+u*sx*X+v*sy*Y`。

有限构造接口 [SmPlane.h:54](../../source/SMLib/inc/SmPlane.h:54) 接受参数原点 O、X/Y 方向、UV scale `(sx,sy)`、自然域矩形；[实现](../../source/SMLib/src/SmPlane.cpp:55) 存储 scale／域并调用 `SmAxis2Placement::SetCanonical`。O 是 `(0,0)` 的几何位置，**不是自然域左下角的位置**。

[SetCanonical](../../source/SMLib/src/SmAxis2Placement.cpp:64) 会单位化轴，并在不垂直时重新构造正交轴；这不是原始文件语义校验。适配层先按 STEP placement 规则生成并验证规范正交基，再移交 native，之后对账实际轴和方程；不能让此函数替代源 placement 规则，也不能容忍未记录的方向改变。

[SmPlane::EvaluatePoint](../../source/SMLib/src/SmPlane.cpp:1458) 默认可能把域外 UV **clamp 到自然域后返回成功**。不夹取的解析式可从 [EvaluatePointFast](../../source/SMLib/src/SmPlane.cpp:4371) 逐项核对。独立验收不能先把错误 UV 交给会夹取的求值器再把成功状态当作参数有效。

[MakeNurb](../../source/SMLib/src/SmPlane.cpp:2993) 用自然域四角的解析求值构造双一次面，结点取该域边界；[SmPlane.h:38](../../source/SMLib/inc/SmPlane.h:38) 要求解析和 NURBS 参数域一致。这一特例可以精确表示平面，不是对任意曲面重参数的通用保证。

### 3.2 首片 UV 选择（适配政策＋数学推导）

采用 canonical mm 的 O、单位正交 X/Y 及 `sx=sy=1`，则 `u/v` 的数值等于沿 X/Y 的毫米距离：

`u=(Q-O)·X`，`v=(Q-O)·Y`，法向残差 `r=(Q-O)·(X×Y)`。

源 PLANE 参数若为源长度单位数值，则在保留同一 placement 基和原点时 `u_n=k*u_s`、`v_n=k*v_s`。这是 **PLANE 专属** 的已声明映射；不能推广为全部曲面 UV、曲线参数或 B-spline 结点都乘 k。方向分量不乘长度单位，LINE 的 λ 也按其定义单独映射。

平面法向为 X×Y，正 faceuse 法向同向，反 faceuse 法向反向，见 [SmFace.h:108](../../source/SMLib/inc/SmFace.h:108) 和 [SmFaceuse.h:40](../../source/SMLib/inc/SmFaceuse.h:40)。目标曲面不因 `ADVANCED_FACE.same_sense` 为假而偷偷反转参数轴；应保留 surface 参数映射并选择正确 faceuse／所属 shell。

### 3.3 如何由 trim 得到有限 window

首片只考虑经验证的简单非自交平面直线多边形及其互不相交、严格位于外环内的孔。对**全部已裁剪边段**端点做上述 UV 变换，以所有端点的坐标极值求包围矩形；直线段的 UV 为仿射函数，所以每个分量的极值必在端点。简单多边形的填充域位于该矩形中；孔不会扩大外环包围范围。这里的全域结论来自直线／平面的数学特性，不能用作曲边 trim 的端点包围规则。

native plane 自然域和 face domain 必须包含完整外环、所有孔与其填充语义，可使用同一个经向外舍入的非零矩形，或使用自然域更大而 face domain 为已证明包围域的矩形。舍入／数值保护宽度应来自明确的计算政策并记录；不得通过移动 trim 或放宽验收条件完成包含。中立层仍保留源无限 PLANE 与有限边界，native window 只限制目标求值范围，不变成新边界环。

[SetFaceSurface](../../source/SMLib/src/SmBrepData.cpp:1041) 直接保存传入 face domain，不替适配器检查其与自然域的关系。既有 top-down 超域负例保留了错误域并由 pointer report 11 报告；data-path 会裁剪而仍可能通过。首片必须在分配前证明包含，构建后核对原值及 trim 区域，禁用把域外求值 clamp 当作证明的检查方式。零面积／非有限域、无法可靠包围时拒绝，不自动补默认范围。

## 4. 方向：四个不同角色不能压成一个未经验证的 XOR

### 4.1 native 构造器的可执行含义

| 调用／对象 | 已核对含义 | 首片约束 |
|---|---|---|
| `StartFace(eOutsideFaceuse)` | [实现](../../source/SMLib/src/SmBrepData.cpp:308) 总建 SAME／OPPOSITE faceuse；入参选择哪个进入 outer shell，另一侧进入 inner shell（开放 sheet 则两侧同一 shell）。 | 源面几何方向、源面 occurrence 和实体内部／外部语义先算清，再选择 shell 的目标侧。表面参数法向与选中侧分别对账。 |
| `StartLoop(eOrientationToSurface)` | [实现](../../source/SMLib/src/SmBrepData.cpp:460) 把入参缓存给后续 `StartEdge`；首环自动分类 outer/SAME，之后自动 inner/OPPOSITE。 | 入参**不选择外环或孔**；明确外环先建、孔后建。源 loop/bound 方向先解析，不靠几何猜或“首个 bound”猜外环。 |
| `StartEdge(eOrientationInLoop,...)` | [实现](../../source/SMLib/src/SmBrepData.cpp:685) 比较 edge 入参与 loop 的缓存；相等则 primary-loop 侧 EU 为 SAME，否则 OPPOSITE 且交换传入顶点。loop 缓存又决定 [CCW／CW 插入方向](../../source/SMLib/src/SmBrepData.cpp:714)。 | 明确每条 occurrence 在 primary-loop 侧的目标有向端点、参数方向及列表顺序；不能单看传入枚举。 |
| `EndLoop()` | [实现](../../source/SMLib/src/SmBrepData.cpp:509) 给尚未绑定的 VU 补顶点／连接，不反转整条边序，也不从入参推断外环／孔。 | 源顶点显式预创建并复用；关闭前后逐 occurrence 检查身份，防止自动生成顶点被误作源身份。 |

`StartEdge` 注释 [SmBrepData.cpp:641](../../source/SMLib/src/SmBrepData.cpp:641) 称端点按 loop orientation 传入，但可执行路径在反向分支交换参数，既有 `tetraloopvertices` 负对照已得到端点几何报告，而按 curve min/max 传入的 `tetra` 通过。首片建议先将 native loop-side 策略固定、外部计算明确的目标有向边序，再用该固定策略的已验证端点顺序；**不能把现有 SAME-loop 的有限实测推广为任意 loop flag 组合均已正确**。

首片可选约束为所有 native `StartLoop(SAME)`：预先把完整 source use 语义转换为 primary surface 侧的明确路径（外环按 +normal 侧材料在左、孔相反），逐 occurrence 的方向相对目标曲线 min/max 求得；输入顶点按 native curve min/max 传入，`StartEdge` 按 SAME／OPPOSITE 选择。该约束是待 P2 证明的适配策略，尤其孔与反向 source face 的组合尚未由既有探针覆盖。不得把它写成当前已授予的能力。

### 4.2 应交给中立层／独立真值的方向数据

分别保存并核对：源底层 LINE 递增参数、源 `EDGE_CURVE` 的 topological start/end 与 same_sense、`ORIENTED_EDGE` 遍历、`FACE_BOUND` 对 loop 的取向、`ADVANCED_FACE` 与 surface 的取向、shell 使用的面侧。源声明之间不一致不能自动反向“修到能构建”。源 source-to-target 的参数反向若发生，记录映射和 occurrence 的方向含义，不覆盖源值。

native 验收逐层比较：

1. 实际 curve min/max 的几何点对应源解析线段的哪端；目标拓扑边 start/end 的对象身份是否与声明一致。
2. 每个逻辑 face-edge occurrence 所在 face／loop／选定 FU，遍历起点／终点与连续前后项的身份是否一致；不能只比较点坐标。
3. source bound 顺序经有记录的反向映射后，与选定 faceuse 上的**有向**目标路径相等；mate 路径为另一侧的逆遍历。
4. 外环／孔身份、顺序、包含关系、二维有向面积与所选 FU 的法向配套；native `loopuse.orientation` 的 outer/inner 分类与几何 winding 是不同字段。
5. 盒体每条逻辑边恰有两个面 occurrence、四个 native EU；selected shell 的有向边界相消，外法向、点分类及体积符号符合独立盒体真值。

这一表述将 STEP 语义的解析责任留在源／中立层，将 native 实际路径验证留在适配器；不是把 native 枚举同名直接当 STEP 布尔值。

## 5. UV：按 occurrence、按目标边参数、按正确 owner 安装

[SmEdgeuse.h:124](../../source/SMLib/inc/SmEdgeuse.h:124) 规定一对 EU/mate 共享一条 UV，实际存储在 SAME member；getter 可以从任意一侧取到该曲线。因此 getter 非空不证明存储侧和 owner 正确。

`constructor.SetEdgeCurve(uv,TRUE)` 的 [直接安装实现](../../source/SMLib/src/SmBrepData.cpp:1124) 把 UV 写入当前 EU，可能是 OPPOSITE，且当 3D curve 未安装时会 [LiftCurve 生成定义曲线](../../source/SMLib/src/SmBrepData.cpp:1127)。既有 `ctoruv` 动态负对照表明错误存储侧可通过 pointer／LEVEL2。首片不使用此 UV 安装分支。

先装入精确 3D 线段和显式区间，完成可靠的图连接后，逐逻辑 occurrence 创建 fresh 2D 线段并调用 [SetUVTrimCurve](../../source/SMLib/src/SmEdgeuse.cpp:4473)。setter [选择 SAME member 并设置 owner](../../source/SMLib/src/SmEdgeuse.cpp:4544)，但 [同一指针再次传入会提前返回](../../source/SMLib/src/SmEdgeuse.cpp:4505)，不能靠重复调用修正旧错误 owner。

目标 UV 曲线应以目标 3D edge 的递增 `t∈[0,1]` 参数化，而不是每个 occurrence 的行走方向；由 occurrence 决定沿 UV 正向或反向遍历。平面／直线下可精确得到 `q(t)=q0+t(q1-q0)`。lift `S(q(t))` 与 `C_n(t)` 都为 t 的仿射函数，两端相等就能数学证明精确实数下全区间相等；实际浮点误差还须独立有界计算，不能把两端抽样误差直接当成一般曲线的全域界。

setter 的 `dMaxDistanceToSurface` 会影响 edge tolerance，接口文档说明容差至少调到该值的两倍，见 [SmEdgeuse.h:148](../../source/SMLib/inc/SmEdgeuse.h:148)。既有精确复制探针传零只适用于其已知同构场景；生产切片应先独立测量／界定差异，按质量配置裁决并记录 setter 前后容差，不能无条件照抄零值，也不能利用 setter 自动增容差来吞掉冲突。保留源 pcurve 与 master 语义；本首片无源 pcurve 的精确平面反解不能成为以后丢弃源 pcurve 的理由。

## 6. 身份与几何所有权

- 源 topology ID 映射为候选内稳定的 `SmVertex*`／`SmEdge*`。同坐标独立源顶点不自动合并；同一 source edge 的两个面使用必须复用同一 native edge。复用 existing edge 时 [StartEdge](../../source/SMLib/src/SmBrepData.cpp:779) 使用已有顶点，调用参数中的新顶点不能用来覆盖它；事前／事后均应按身份检查。
- [SetEdgeCurve(...,FALSE)](../../source/SMLib/src/SmBrepData.cpp:1107) 只允许当前 edge 尚无定义曲线时接收一次指针并设 owner。多个源 edge 可共享中立的几何定义，但各 native edge 须分别材料化曲线；一个已拥有 native curve 不能再移交另一 edge。复用 topology edge 不重复移交曲线。
- [SetFaceSurface](../../source/SMLib/src/SmBrepData.cpp:1041) 发现传入 surface 已有 face owner 时用 `new SmSurface(*pSurface)`，不是虚拟 `Copy`。首片禁止走该复用分支：每个 face 新建其正确动态类型、无主的 plane，或在移交前明确使用其虚复制机制并验证类型／参数；即使多个源 face 引用同一 PLANE 定义，也各自拥有 native surface。
- 每个 occurrence 的 native UV 是 fresh 对象，一次移交 SAME member；不跨 mate-pair／face／候选共用同一 owning 指针。
- context 不是所有对象的统一回收器。立即拥有 [StartBrep 返回的候选](../../source/SMLib/src/SmBrepData.cpp:97)，未移交的独立几何也由适配器 guard 持有；移交后由 BRep 对象图拥有。销毁顺序为借用视图失效／释放、候选 BRep、尚未移交的几何、最后 context；不能一处 guard 和 native owner 同时删同一对象。[EndBrep](../../source/SMLib/src/SmBrepData.cpp:129) 只检查状态并清空 builder 根指针，不是发布或事务提交。

`StartEdge` 第二个面附着 lamina edge 的常见盒体路径不应进入非流形 sector 求解。但源码对已有非 lamina edge 会 [FindRadialSector](../../source/SMLib/src/SmBrepData.cpp:800)，求不到时 [fallback 到 mate](../../source/SMLib/src/SmBrepData.cpp:855)，且 [EndBrep](../../source/SMLib/src/SmBrepData.cpp:129) 没有替导入器证明其来自源语义。首片预检每条边最多两个面 occurrence，无同面重复／非流形 seam，按身份统计建图并独立核对 radial/shell；任何意外进入此分支不能以“返回了 edge”接纳。未来支持 seam／非流形需要单独契约，而不是当前切片顺带晋升。

## 7. P2 必须运行的验证门槛

以下是**待执行的验收设计**，不是新增运行结果：

| 门槛 | 正例／反例与独立断言 | 尚未证明之处 |
|---|---|---|
| N01 线参数 | 非原点 LINE、非单位方向比例、源区间不为 `[0,1]`、反向 source edge；检验源↔native 参数、自然域、端点／切向／长度。零向量、重合端点、不在线顶点必须拒绝。 | STEP→native 全切片。 |
| N02 平面 placement | 原点不在 window 左下、旋转 placement、源轴非单位、允许的默认轴；独立坐标投影与求值比较。非法轴按源规则拒绝。 | 源默认／正交化规则与 native 无偏差传递。 |
| N03 有限 window | 正／负 UV、远离原点、多边形斜边、孔；证明整个填充域被包围。刻意令 window 少包一条边／孔、域外 UV、零域、非有限域均拒绝。 | `EvaluatePoint` clamp 不得制造假通过。 |
| N04 sense 组合 | 系统组合 LINE 递增方向、edge same_sense、oriented-edge、bound orientation、face same_sense；可等价反向的文件应导入为同一几何／有向语义。 | 既有 SAME-loop 有限探针不覆盖全部组合。 |
| N05 外环／孔 | 外矩形加内矩形或简单多边形孔；bound 存储顺序变化；验证 native 外环优先、孔身份、方向、面积和点分类。孔穿外环、孔互交、接触等超出已声明子集要准确拒绝。 | 没有既有 top-down 带孔动态证明。 |
| N06 共享拓扑 | 盒体8V/12E/6F、每边两个逻辑使用／四EU、完整 mate/radial/loop 身份；独立边界链抵消和解析体积／分类。共点独立 source ID 不自动焊接。 | 首片完整 STEP 盒体尚未执行。 |
| N07 所有权 | 不同 face 共用源 PLANE 定义、不同 edge 共用源 LINE 定义，仍产生分别拥有的 native 几何；同一 edge 重复 occurrence 则共享 topology。 | fresh geometry、移交点和失败后源不变需要实现后观测。 |
| N08 UV owner | SAME／OPPOSITE occurrence 均只在 SAME member 存储，owner 指回该 EU，两个面有独立 UV；错误 member／重复 pointer 负例不能靠 validator 接纳。 | 新适配实现的独立检查尚未编写。 |
| N09 容差与验证 | 记录初始／UV安装后／pointer后／LEVEL2后的逐实体 effective tol 与关键阈值，比较源定义、对象身份与派生数据；报告非空即不能仅凭 SM_SUCCESS接纳。 | 当前 HEAD 重建配置与 P2 fixture 需要新证据。 |
| N10 不完备／取消 | 在各已批准调用边界停止、普通返回错误、后验拒绝，模型不发布，输入不变且资源按已有测量方法对账。 | 任意分配失败、损坏图析构、硬中断仍未证明进程内完整清理；沿用隔离边界。 |

P0 可完成的是清单、参数契约和真值定义；N01—N10 的实际通过状态保持未执行。P2 前不能把旧合成 native 探针换名为 STEP 导入通过。若实现发现接口约束无法保持源语义，应回到明确失败／能力门槛，不静默改线、翻面、焊接或增长容差。
