# 04：必要源规则与有向平面区域验收

2026-10-16。用户授权实现04，继续使用自研读取，不进入SMLib构建；后续可能迁移C++20。
已认可总体规格0.1与01冻结语料保持原字节，原设计审计文档的Defined/NotRun保留其历史含义，实际执行以本记录为准。

## 交付与边界

新增独立内部库[STEP_PLANAR](../../source/STEP_PLANAR/README.md)、`step_planar_inspect`及
[04独立核验器](../../tools/step_planar_validation/README.md)，只链接已有STEP_SEMANTICS/STEP_SOURCE。
保持C++17最低要求；C++20在独立目录构建同一STEP组件闭包，全仓标准不变。

规则元数据来自固定N2635 `c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`。
相关72实体声明的77个WHERE/UNIQUE/INVERSE条款均有记录：对适用实例执行，空人口保留守卫证据；产品单例证明两个UNIQUE，
context inverse从精确反向角色计算，额外属性赋值人口在全文件已验证封闭签名下排除。命名measure的TYPE WHERE连同其祖先检查。
47条全局RULE逐条记录5必查／1条件／41守卫及全部FOR总体，保留SDR/PDR、EL/LOOP与间接RI。
这是一套受限profile内的等价专用检查，不是整份schema的通用执行器或完整AP认证。

G36普通名称的函数UNKNOWN、NOT UNKNOWN及空失败QUERY各自有证据。未计算、计算未判定与源LOGICAL UNKNOWN分开。
MSSR.WR6—WR14由实际AF类型证明第一析取项True；plain-FS支路未运行，原schema访问路径未改写。

源LINE/PLANE、EC/OE/bound/face四层方向与身份逐项保留。几何误差以精确平方分数比较Q；源参数无双重单位缩放。
派生placement使用未归一化精确正交基并保留根式；区域检验覆盖简单多边形、孔内含、交触、嵌套歧义和有效方向。
壳的共享身份/方向、自由边和连接性也有源层反证。`projected_region_*`仅为源边界在源平面上的诊断投影证据，不是已创建的目标UV或native模型。

## 49项固定合同

| 当前状态 | 数量 | 准确含义 |
|---|---:|---|
| Checked | 11 | P01—P10、P12取得04源规则与平面区域证据 |
| Rejected | 15 | N10—N13、N19—N27、N29、N30，本层取得第一阻断反证 |
| UpstreamFailure | 17 | 原02/03的10项源失败、7项Unsupported；04不继续执行 |
| Deferred | 6 | P11/P13/N31—N34的conversion解释待09；04规则/几何未运行 |

49项当前阶段合同全部匹配，32项准确阻断；完整导入49项均NotRun/NotChecked，成功数null，所有`model=null`。
Checked不授native建模/查询、目标trim/window、容差设置、壳自交或材料体积资格；P05保持闭合surface-model来源角色。

| 验收条目 | 证据与反控 |
|---|---|
| 相关祖先/TYPE/UNIQUE/INVERSE、47条适用性 | 独立EXP声明见证、逐实例条款清单、全FOR人口与5/1/41统计；删条目反控拒绝 |
| SDR/PDR、LOOP、RI、UNKNOWN与schema缺陷边界 | G36实际SDR及UNKNOWN计数、G47完整59总体和外部依赖锚点、RI传递使用；篡改人口/UNKNOWN反控拒绝 |
| LINE/PLANE、方向、身份、区域与数值 | N19零LINE的VECTOR.WR1仍True；N20平方残差64 mm²；N21平方残差1e-6 mm²；N22参数sense；N23 PATH两处邻接不符；N24 PATH True而EL False |
| 首个阻断与后果 | 按冻结key/stage匹配；孔接触先命中边界相交可以成立，不强迫同步列出内含后果；Unsupported沿用来源，不重报源WHERE失败 |
| 资格边界/未判定 | conversion六项Deferred；派生轴无值、精确算术容量及work/report限额控制；全部无native构建与模型 |

## 构建和正式证据

最终构建、二进制及源码集合身份、配置/解释器日志、正式run和离线入口在本机证据根：
[04证据入口](C:/Users/tangx/Documents/Codex/2026-10-13/usd/outputs/step-import-ticket04-evidence/index.html)。
正式49项run：`20261016T012932Z-ea550cb2c140`。
最终收据：`qualification/build-tests-20261016T012722Z-d23ca482d415`。
MSVC 19.50.35728、VS18 2026（instance D:/Program Files/VisualStudio）；C++17 Debug/Release各14组新平面＋10组语义＋10组原始源层检查通过，
C++20 Release同样通过。Python3.14.3/Debug与3.10.5/Release各35项真实CLI测试通过，包含49固定合同、16组合法sense组合、
非单位方向/非默认LINE参数、缺省轴/派生未判定、Q边界、自交/相触/嵌套孔、两个分离孔、700辅助placement规模及7项checker篡改反控。
C++20与C++17在49个固定输入上退出码和stdout逐字节相同；仅证明该STEP组件闭包，不是全仓C++20或混用ABI承诺。

最终源码/独立见证/测试/工具依赖集合SHA `975d9fb25a7afaca7eca76c0dac15511c48fd738657422b3988057bb5001429b`；
C++17 Release EXE SHA `75ab6c5bd8b229fce9a754823b404e312fcda5178431e41f778acaa054d6e735`；
C++20 Release EXE SHA `31a077b20006a572dbae52499f33593334a6b4c4641ac618e47ec59e130cbe36`。
新规则见证SHA `970d8ac65d56cad561004e72865fb3340000a6431e967b9a9bc9bd98cc6639b6`。
两套依赖收据仅有标准C++/C运行库和KERNEL32，无OCCT/SMLib/USD。源层局部证据以OwnerInstance引用owner，完整总体复用语义快照，避免平方级列表存储。

原01工具集合、02代码集合、03代码/见证集合分别保持 `b9823048…`、`d0c2aed3…`、`0b26fd45…`；303材料全部通过原manifest核验，
总体0.1 SHA仍为`9a6f364e38eeb43fee5bb7b05e151eb7102cc80f402ae24f508adc55a566d6fa`。
本轮较早候选收据保留原身份；最终证据只采用上述源码/二进制和当前正式run。

## 当前能力限制

精确运算采用有界int64分子/正分母及已约分操作；数字或中间量超容量返回Indeterminate，保留来源并禁止舍入兜底。
所以源层可保留任意合法数字词元，不代表04已能判定其全部几何。Q-P0-mm-01为显式测试候选，consistency=1e-7 mm，
不提供生产默认，也不以source uncertainty放宽验收；配置参数与预算延续总体设计。

没有授予conversion、plain FACE_SURFACE、其他几何/包装、全Part21/AP协议、native BRep/生命周期/取消/OOM、
跨平台、真实模型库长跑或P0退出资格。05及其后续native票未开工；网格算法、SMLib、SM_API及绑定不在本次改动中。
未commit/push、未发远端issue/PR。
