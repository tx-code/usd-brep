# AP203e2 平面盒体样件的反例与诊断审计

版本：203e2-Sample-N0.1，2026-10-15。适用 [AP203e2 首个 profile](ap203e2-planar-profile.md) 固定的 N2635，源文件 SHA256 为 `c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`。本文为样件设计的审计依据，**全部 Defined / NotRun**；没有生成 STEP、expected 文件或测试实现。候选 N-A 编号只用于此审计，不是主用例目录中的新增稳定用例，也不计入已通过数量。

## 1. 单缺陷的定义和诊断边界

一个阴性样件由完整正例加一个明确的 mutation 得到。优先选择一个字段、一个记录类型名、一个聚集属性的改变；“交换同一 LIST 中两项”是一个有意义的聚集属性 mutation，但不是单字符修改。单次 mutation 可能违反数条约束。必须列出最早阻断点及连带后果，不能把“只改一个 token”说成“只违反一个规则”。

正例引用 [独立数学真值](p0-planar-cases.md)：F1 是旋转平面上的 12×8 mm 矩形，B1 是 `[2,12]×[-3,17]×[5,35] mm` 材料盒体。共同封套无 GUUNC、无 category，含一个 product、具体 representation 和 3D context，单位为 mm 与 rad；合法缺席不能成为阴性条件。精确记录布局由[样件主册](ap203e2-sample-design.md)固定，最终用例和预期诊断由[统一样件目录](ap203e2-sample-cases.md)收录。

诊断层使用语义名称，不在此声明已存在的 API 错误码：Part21/index、schema binding、typed graph/mapping、profile guard、source constraints、source geometry/quality、native construction。范围守卫先完成实际类型及必要属性识别；可明确证明的字段/局部约束反证应保留为源错误，不将所有异常都归为 Unsupported。未准入类型不应为了获得更多错误而进入未证明的原式分支。

| 诊断种类 | 可以断言什么 | 不能混用的结论 |
|---|---|---|
| SourceInvalid | 已有明确语法、字段/聚集/引用、WHERE/UNIQUE 或几何反证，记录确切证据 | 不因 native 容差较大改为通过；不把数值算法未算清当源非法 |
| UnsupportedProfile | 已识别表达超出首片范围；所需检查能力没有获得资格 | 不由此宣称整个源文件 schema 合法，也不能说源文件一定非法 |
| NumericalIndeterminate | 实现不能给满足配置的数值误差界；没有得到确定几何反证 | 不把它当 EXPRESS 的 LOGICAL UNKNOWN；不伪报阈值超限 |
| Cancelled / Failed | 请求取消、资源或内部失败按总体执行合同归类 | 不伪装成坏 STEP；没有任何候选模型发布 |

首个失败门槛决定该请求不交付模型；不保证错误输入获得所有后续诊断。例如 PATH 已断，不再以重建出来的轮廓面积判断“孔被填了”。允许附加诊断时，必须能在不依赖已失败前提的情况下独立证明；目录不能要求某个线程调度顺序或 native 错误偶然先发生。源阶段失败的阴性样件应断言 native 构建调用未开始。

## 2. 用于审计的固定定位

| 部位 | F1 记录身份和字段 |
|---|---|
| 产品封套 | `#1` AC；`#2` APD；`#3` PC；`#4` PRODUCT；`#5` formation；`#6` PDC；`#7` PD；`#8` PDS；`#9` SDR；`#10` mm；`#11` rad；`#12` complex context；`#20` MSSR；`#21` SBSM；`#22` OPEN_SHELL |
| 四顶点 | CP `#100..#103` 依次 A/B/C/D；VP `#110..#113` 引用对应 CP |
| 支撑平面 | `#130` origin CP；`#131` axis Z；`#132` ref X；`#133` AXIS2_PLACEMENT_3D；`#134` PLANE |
| 第 i 条边，i=0..3 | `#(200+10i)` DIRECTION；`#(201+10i)` VECTOR；`#(202+10i)` LINE；`#(203+10i)` EDGE_CURVE；`#(204+10i)` ORIENTED_EDGE |
| 面 | `#300` EDGE_LOOP，LIST 为 `(#204,#214,#224,#234)`；`#301` FACE_OUTER_BOUND；`#302` ADVANCED_FACE |
| B1 额外定位 | `#20` ABSR、`#21` MSB、`#22` CLOSED_SHELL；底面 `#507` ADVANCED_FACE。其顶点、12 条 EC 和 24 条 OE 身份以主册为准 |

F1 所有 EC.same_sense、OE.orientation、bound.orientation、face.same_sense 基准均为 `.T.`。表内改变一个值时，其余记录原样保留，尤其不得额外改 LINE 定义来“配合”坏顶点。文本编码、HEADER、质量配置、修复关闭状态均不随阴性 mutation 改变。

## 3. 二十四项可供主目录选用的 mutation

下表的“独立真值”指输入本身的确切事实，不是要求错误模型仍能被物化后测量。所有受影响的 source ID/字段都应进入诊断证据；不以 native 新建的编号取代它们。

| 审计候选 | Base；唯一 mutation，old → new | 独立真值和连带后果 | 最早必需诊断与依据；不应出现的标签 |
|---|---|---|---|
| N-A01 | F1；FILE_SCHEMA 中 canonical OID `{1 0 10303 403 2 1 2}` 最后一项 `2→3`，其余 schema 名不变 | 文件几何、图和数值完全不变；声明不再绑定已冻结 N2635 profile | schema binding：Unsupported schema/profile binding，记录完整原名及 OID；不能按 APD 年份或裸名猜回 e2，不能报几何失败 |
| N-A02 | F1；在 DATA 尾追加第二条同 ID `#100` 的原记录副本 | 同一实例编号出现两次；此时不能选择“最后一条”给出几何真值 | Part21/index：duplicate instance ID，给两处原位置；禁止进入根/几何检查，不能误报 CP 坐标 UNIQUE |
| N-A03 | F1；`#9.definition` 参数 1：`#8→#99999`，且全文没有 `#99999` | 只有一个必需引用悬空；几何记录未改变，但不能形成所选完整产品请求 | typed graph/index：unresolved mandatory reference；对象为 `#9` 的字段 1；不能把它当 optional 缺失或自动从 solid 倒猜根 |
| N-A04 | F1；`#3.name` 参数 1：原合法 label → `$` | PRODUCT_CONTEXT.name 是必需 label；`$` 与合法空串不同，geometry 不变 | typed graph：mandatory value absent / attribute type；不是“名称可空所以通过”，也不是 mechanical discipline 的 WHERE 错 |
| N-A05 | F1；`#204.edge_start` 参数 2：`*→$`，其他四槽不动 | OE 对继承端点作 DERIVE 重声明；simple 映射必须用 `*`，不允许把可由底层求值误解成 OPTIONAL | typed graph/mapping：derived attribute marker required；不是 source start vertex 不存在或 PATH 断开 |
| N-A06 | F1；`#300.edge_list` 从 `(#204,#214,#224,#234)` 改成 `(#204,#204,#224,#234)` | 同一 OE 身份在 LIST 重复；还会造成 A→B 后再次 A→B 的相邻连接错误 | typed graph/aggregate：PATH.edge_list 的 `OF UNIQUE` 先阻断；PATH.WR1 可作独立附加后果，不能把 same-EC 的不同 OE 也去重 |
| N-A07 | F1；`#203.edge_start` 参数 2：`#110→#100` | 目标存在，但 `#100` 是 CP 而不是 VERTEX；不同于坐标相同的 VP | typed graph：reference target type mismatch，字段声明为 EDGE.edge_start:VERTEX；不能等到端点 distance 查询才报几何偏差 |
| N-A08 | F1；`#2.application_interpreted_model_schema_name` 参数 2：正确小写、不带后缀的 APD 字符串 → 同字符串加 `_mim_lf` | HEADER 仍精确绑定；APD 文本是 label，类型本身合法，但 G02 对唯一 AC 的匹配集合为空 | source constraints：`application_protocol_definition_required.WR1`，schema L12371—12379；不能把 HEADER 合法当作 APD 自动补齐，也不归“未知 STEP schema” |
| N-A09 | F1；`#200.direction_ratios` 参数 2：`(0.6,0.8,0.)→(0.,0.,0.)` | 明确零向量，无可归一化方向；原顶点、plane 都没动 | source constraints：DIRECTION.WR1，L4674—4679；不要用随意 epsilon 说“近零”，也不能只报 endpoint 不在 LINE 上 |
| N-A10 | F1；`#201.magnitude` 参数 3：`12.→-12.` | REAL 数值有确定负号，违反 magnitude≥0；点和 ratios 不变 | source constraints：VECTOR.WR1，L12204—12210；不是 source LINE 反方向的合法表达，不能取绝对值修好 |
| N-A11 | F1；`#132.direction_ratios` 参数 2：`(.6,.8,0.)→(0.,0.,1.)`，placement 的引用不变 | axis/ref 都有值且为相同非零 Z 方向，叉积 magnitude 精确为0；LINE 不使用这两个 plane 方向实体。`#132` 仍被 placement 使用，不产生额外孤立 RI | source constraints：AXIS2_PLACEMENT_3D.WR4，L2754—2755；不得切到默认 X，不能将显式平行值当“缺省允许” |
| N-A12 | F1；`#10` 的 SI_UNIT.name：`.METRE.→.RADIAN.`，prefix 保留 `.MILLI.`、LENGTH_UNIT constituent 不动 | dimensions_for_si_unit 得到七维全零，而 LENGTH_UNIT.WR1 需要 `(1,0,0,0,0,0,0)` | supported unit 的源约束检查：LENGTH_UNIT.WR1，L6828 起；不能按 LENGTH_UNIT 名称强设 mm，也不能仅报未知单位名。若项目先做 unit role 检查，仍保留这项可证明的 source 反证 |
| N-A13 | F1；`#201.magnitude` 参数 3：`12.→0.` | VECTOR.WR1 的 ≥0 条件为 True；LINE 变成恒点 A，无非零段；B 到该定义的距离 12 mm 是后果 | source geometry：nonzero defining LINE required / magnitude=0；禁止错误标成 VECTOR.WR1 失败或把 A/B 直接连成新线 |
| N-A14 | F1；`#202.pnt` 参数 2：`#100(A)→#102(C)` | 方向与 magnitude 不变，直线平移 `12X+8Y`；其沿 X 分量不改变支撑线，法向于线的 Y 偏移为 8 mm。A/B 到新直线的距离均 8 mm，仍在同一个 plane 上 | source geometry：EC endpoint-to-LINE residual 8 mm > Q 的 `1e-7 mm`；最短证明只需一端反证，不能误报 edge-to-PLANE 距离或 LINE.WR1 维数错误 |
| N-A15 | F1；`#110.vertex_geometry` 参数 2：`#100(A)→#103(D)` | 只改变 VP 的几何引用，所有 CP/LINE/plane 定义不动。拓扑身份仍是 #110；e0 的 start 距原 LINE 8 mm，e3 的 end 与 start 在 D 重合也是后果 | source geometry：vertex/curve inconsistency；不能说 PATH 身份断开，不能称只变一条边，也不能焊接 #110/#113 消掉后果 |
| N-A16 | F1；`#214.orientation` 参数 5：`.T.→.F.` | e1 从 B→C 变 C→B；原 LIST 的 e0.end=B、e1.start=C 不同，下一邻接也断；首尾 A 保持闭合 | source constraints：PATH.WR1 / `path_head_to_tail`，L8847—8853、L15457—15471；不能标成“仅法向反了”“面区域仍有效”或 native seam 失败 |
| N-A17 | F1；`#300.edge_list` 整属性：`(#204,#214,#224,#234)→(#204,#224,#214,#234)`，即交换中间两项 | LIST 无重复，第一条 start 与末条 end 仍同为 A；中间相邻 end/start 身份不相同 | source constraints：PATH.WR1；应明确 LIST UNIQUE 和 EDGE_LOOP 收尾这两项仍满足，不能把所有 loop 错误统一归 EDGE_LOOP.WR1 |
| N-A18 | F1；`#233.edge_end` 参数 3：`#110(A)→#111(B)` | OE e3 仍从 D 开始，内部 PATH 邻接 A→B→C→D→B 保持 head-to-tail；首 A 与末 B 身份不等。原 e3 LINE 不变，终点 B 到该线的距离 12 mm 是后续几何错误 | source constraints：EDGE_LOOP.WR1，L5319—5326；PATH.WR1 在这个输入应 True；禁止只要求 generic loop failure 或先向 native 请求造封口 |
| N-A19 | F1；`#301.orientation` 参数 3：`.T.→.F.` | 所有 source 曲线/点/拓扑序列不变，bound 的有效边界变为反向；相对 +plane normal 的有向面积 `+96→-96 mm²`，face.same_sense 仍 T | source geometry：outer-bound/face orientation inconsistency；BOOL 为 F 不是 schema 字段非法。不能把每个 F 都拒绝；必须用完整 EC/OE/bound/face 组合证明 |
| N-A20 | F1；`#301` 类型名 `FACE_OUTER_BOUND→FACE_BOUND`，三个槽原样 | 源 FACE.WR2 的显式 FOB≤1 仍满足；本输入显式 FOB 数量为0，点/环/区域完全不变 | profile guard：explicit outer-bound required / UnsupportedProfile；不能报 FACE.WR2 源约束失败，也不应猜哪个普通 bound 是外环 |
| N-A21 | F1；`#302` 类型名 `ADVANCED_FACE→FACE_SURFACE`，四槽原样 | 记录字段布局仍对应 plain FS；已离开首片“所有面 AF”的守卫，MSSR 原式普通面分支存在已登记访问问题 | profile guard：Unsupported plain FACE_SURFACE；不声称整个源 schema 必然合法或非法，不进入或修补有问题分支后再接纳 |
| N-A22 | F1；`#22` 类型名 `OPEN_SHELL→CLOSED_SHELL`，faces SET 不动 | 各面仍为原唯一矩形，4 条自由边，总长40 mm；CLOSED_SHELL 名称不使几何封闭。MSSR/SBSM 可以引用 closed shell，不产生 MSB 类型冲突 | source geometry：claimed closed shell has free boundary；CONNECTED_FACE_SET/CLOSED_SHELL 没有替代几何证明的局部 WHERE。不得改回 OPEN_SHELL 后发布，也不能给材料体积资格 |
| N-A23 | B1；`#507.same_sense` 参数 4：`.T.→.F.`，该面全部 bound/OE 不变 | 底面由源 PLANE 决定的所选法向从 −Z 变 +Z；几何支撑/边界点均不动。有向 bound/face 区域以及封闭壳定向可能同时不一致，不再是合法 B1 材料边界 | source geometry：face/bound or closed-shell orientation inconsistency；法向对预期外向的点积 `−1`。不能要求唯一某种体积积分值，也不能把“字段 F”直接作为源 WHERE 反证 |
| N-A24 | F1；普通 `#3.discipline_type` 参数 3：`'mechanical'→'electrical'` | 普通 PRODUCT_CONTEXT 没有 mechanical 局部 WHERE；身份、inverse、几何及普通 part-definition 图不变 | profile guard：Unsupported product context discipline；不是 MECHANICAL_CONTEXT.WR1，因为该输入并无该 subtype；不得自动把值改回 mechanical |

N-A14 的“8 mm”是独立解析真值：旧 LINE 支撑方向为 X，新 pnt=C=A+12X+8Y；X/Y 是正交单位向量，因此两条支撑线的垂直间距为 `|8Y|=8`。不同源参数原点带来的沿线 `12X` 不影响这项残差。N-A15 的 e0 残差与 e3 长度崩塌不能合并称为“仅点离开 plane”：D 仍在同一 plane 上。

## 4. 必须保留的正控制和不能伪造的阴性用例

| 操作 | 正确结论及设计要求 |
|---|---|
| 只把 mm 的 SI prefix `.MILLI.` 改 `.CENTI.` | 合法物理几何整体放大10倍；面积×100、体积×1000。若不同时按单位等价规则缩小源数字，就不是同几何单位变换，更不是单位非法反例。质量配置继续使用 mm，不能偷缩放配置 |
| 将 `#202.pnt` 从 A 改为 B，LINE 方向仍 X | 无限支撑线不变，只平移参数原点12个源长度基准；不能作为 endpoint 残差阴性。映射必须记录新 t，不能因为某端 t 不为0而拒绝 |
| 将方向 ratios 乘正整数；或用有效非正交 ref 并做定义性的投影 | 精确几何/派生坐标系可保持不变，按原规则归一化/投影是定义解释；不是 repair |
| 同时交换 EC 的两个端点并反转 same_sense，再协调 OE/use | 可定义等价几何和使用路径；需要正例的完整协调 mutation，不宜拿一半改动作“只坏曲线方向”的阴性 |
| 直接改共享 CP 的一个坐标，作为“一个 VP 离 LINE” | 不成立。该 CP 还可被 LINE.pnt、placement.location 或其他 VP 使用，必须先列完整 incoming roles。要隔离 VP 偏差，应先设计有独立 VP-geometry CP 的专用正例，再只改那条 CP；不能悄悄修改原始最小 F1 的对象共享方式 |
| 从 B1 的 shell.faces 删除一面，仍要求第一诊断只能是非封闭 | 不成立。被移出的 AF/bounds/边/geometry 可能丧失 using-representation 锚点，RI.WR1 或封闭记录归属守卫先失败。该变更可作完整图拒绝例，但不能声称已隔离了 manifold 检查 |
| 在只有一个 formation/PDS 的最小文件追加重复记录，称仅测 UNIQUE | 追加会同时破坏恰一主链/全记录归属。schema 原式控制可核对 UR，但端到端主诊断不应强制只报 UR。若需要精确区分，应给源规则层专门输入与明确阶段，不伪称首 profile 完整接纳例 |
| 去掉 GUUNC、steradian 或 category | 本片合法缺席，不是坏源；测试专用 Q 必须仍由调用者提供，不由源 uncertainty 补出 |
| 极端近奇异输入直接规定必须 SourceInvalid | 必须先有精确反证；真正精确非零的轴可能被稳健算法证明可用，也可能因误差界不足 NumericalIndeterminate。不可把机器下溢结果当 schema 的零叉积证明 |

数值未判定控制应包含独立、确切的几何真值和失败于“证据不足”的结果。它可以在以后专用 oracle/计算资格测试中注入“不包含真值的误差区间”或不可完成的资源上界；这不属于单 token STEP 缺陷，不在上面24项中冒充必然坏源。P0未实现 evaluator，本文不假设某个精确合法数值必定击败所有实现。

## 5. 每项用例进入主目录前的核对

每项须固定 base 名与 hash、profile/Q 版本、精确字段位置和原始值/new 值、mutation 的定义、仍成立的前置守卫、首个必需失败层及理由、独立真值、可选连带诊断。派生输入 hash 和实际字节数须等 fixture 生成后计算，不能用此处记录数设计替代。

源错误与 Unsupported 都必须得到完整请求不交付模型；源阶段阻断时不得调用 native 构建。若待测实现只返回无法判定，不算满足已有明确反证的拒绝例；若它对合法能力外表达给出 Unsupported，可作为边界控制符合，不能据此声称已经核准该表达的全部源规则。取消/失败/清理和发布原子性沿用总体规格，另有工程用例，不由这些源 mutation 自动证明。

必需报告证据包括原始 source ID、类型及字段、定位、适用规则/项目合同、精确或有界的 observed/limit、受影响源 ID 链、`repair=None`、`model=None`。候选诊断名称是语义标签，不冻结 C++ enum、公开绑定、消息字符串或错误出现次序；最终编码前另作接口规格和实现验证。
