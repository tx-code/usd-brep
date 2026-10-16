# 09 验收：单位重述、转换链和源参数

2026-10-17（Asia/Shanghai）。固定审查基点 `712e4cc`，实现 `78ce106`。
09四条原验收完成，10—12保持on-hold。仅本地提交，未推送。

## 交付与层边界

自研STEP读取链路支持P06/P07的SI厘米重述及P11的精确转换长度单位，交付原有owning只读模型。
源顶点、LINE位置、VECTOR幅值和PLANE位置按物理长度解释，目标统一为毫米；
方向比和源LINE参数t保持无量纲，目标LINE仍为[0,1]。
源PLANE的u/v具有源长度单位，通过单独精确比例映射为目标毫米UV，不能套用LINE参数政策。
工作容差、定义/一致性/派生UV误差和查询预算沿用canonical mm及其面积/体积次方。

03的已验收类型/单零件/SI合同及代码不变。conversion在03仍为有完整typed快照的Deferred；
04只接续binding/types/part已通过、siUnits明确Deferred且无诊断的交接，
执行必要源量纲规则、正尺度/无环解释及几何检查。只有完整04 Checked才可进入native。
因此P11报告中的source.semantics为Deferred、source.planar为Checked是明确分层，
不是绕过未完成的单位解释。unit_normalization另行明确解释为Checked。

单位逻辑保留在04现有规则/几何模块；native计划只消费已验证的精确长度比例。
Report.units及JSON unit_normalization拥有源单位、精确长度/UV比例、typed factor、
七个量纲指数和每条共享定义链的来源。模型、源快照与报告原有所有权边界保持。
源字节、实体身份、参数和名字原样保留；不从名字猜尺度，不修改定义几何或源单位。

## 独立真值与控制

仍使用01冻结输入、独立truth及Q-P0-mm-01。独立Python见证从原始STEP字段展开单位factor乘积，
以Fraction核算到实际SI根，不读取产品换算值生成期望。原数学几何真值不变。
定义曲线与曲面不拟合，原共享边、逐use UV、材料/表面角色与后验误差证明继续适用。

| 样件 | 精确源长度→mm | 面积 mm² | 唯一边长 mm | 材料体积 mm³ | 形心 mm |
|---|---:|---:|---:|---:|---|
| P06，只改单位声明 | 10 | 9600 | 400 | 不适用 | (34,52,50) |
| P07，数字与单位协调重述 | 10 | 96 | 40 | 不适用 | (3.4,5.2,5) |
| P11，转换单位材料盒体 | 127/5 | 14193.52 | 609.6 | 98322.384 | (12.7,25.4,38.1) |

P06几何长度为原F1的10倍；P07保持F1世界几何。
P11 bbox为(0,0,0)→(25.4,50.8,76.2)，保留6F/12共享E/8V和材料角色。
三者源LINE端点t仍为0、1。P07的源UV窗为[0,1.2]×[0,0.8]，对应目标trim范围为[0,12]×[0,8]；
目标finite window另有原政策记录的浮点保护。
面求值、最近点、距离、P11材料分类均与独立真值比较，而非仅检查报告元数据。

新增控制还包括保留inch名字但把factor改为2、在名字中放入SI_UNIT(fake)、
不同实体身份但七维值相等的dimensions、多级转换和共享SI基底、非单位方向与非默认源t。
这些控制分别验证实际因子、值相等和参数语义，不能按样件ID/实体ID/名字派发。
八种错误观测反控覆盖尺度、UV、LINE参数、单位身份、factor、dimensions、缺定义和预算单位；
独立比较器必须拒绝每一种错误观测。

## 源规则与项目解释的失败边界

源量纲只比较七个精确指数值。CONVERSION_BASED_UNIT.WR1读取显式dimensions及factor单位的维数，
不在这一schema函数里递归factor链。LENGTH_UNIT和measure相关规则另查名义与实际长度量纲。
普通LENGTH_MEASURE无正值WHERE；POSITIVE_LENGTH_MEASURE的TYPE约束仍在源层生效。

| 输入 | 阶段 | 预期诊断 |
|---|---|---|
| N31，显式量纲反证 | D3 | SourceRuleViolation:LENGTH_UNIT.WR1+CONVERSION_BASED_UNIT.WR1 |
| N32，负换算尺度 | D4 | UnitScaleNotPositive |
| N33，零换算尺度 | D4 | UnitScaleNotPositive |
| N34，转换定义环 | D4 | CyclicUnitDefinition |

这些输入均在native启动前阻断，保留具体单位/factor/维数来源与expected/actual事实。
D4换算用显式路径、活动集合和共享memo有界迭代到SI根，每个尺度必须正且可逆；
环报告实际闭合的unit→factor→unit链，不以递归栈溢出充当诊断。
数值容量不足返回Indeterminate，工作预算不足返回ResourceLimit，不能归为源模型非法。
覆盖两节点环、共享基底、120层转换链、工作N/N−1和数值容量反控。
80项局部身份、规则顺序与原有SI预算行为保持。

本票不依赖10的可选uncertainty目标支持：P13仍明确UnsupportedNativeOptionalDeclarationScope。
通用源检查已可拒绝N34的单位环，但P13/N34与可选声明分支的完整组合资格仍留12，
不把09与10人为改成串行依赖。

## 最终回归

Debug/Release均实际重建并通过13个CTest目标：三个既有源组件合同、
F1/孔/材料盒体/闭合表面/单位五个原生接口合同，以及五组同进程资源检查。
两配置各28组真实CLI/独立Python检查及142项01—04核心Python检查通过，无跳过。
本轮实际运行上述检查，未用旧票的测试结果代替。

49项native合同为8 Accepted、29 Rejected、12 Unsupported，无Deferred；
成功样件为P01—P07及P11。04源合同为13 Checked、19 Rejected、17 UpstreamFailure；
03保留26 Checked、6 Deferred、8 Rejected、7 Unsupported、2 SourceFailure。
“49项合同符合”不等于49个模型导入成功。

与08版本的stdout/stderr/退出码逐字节对比：两配置03共98份全部相同；
04共86份相同，另12份为每配置P11/P13/N31—N34六个单位样件的有意变化。
SI路径没有新增conversion定义行或额外计费，316个历史工作/依赖预算边界逐字节相同（每配置158个）。
03预算控制包含仍Deferred的P11；04预算回归只比较其未变的SI路径，
新增conversion路径的工作N/N−1另由当前单位合同验收，没有把变更路径称为历史字节不变。

Release实测P06/P07面积分别为9600.000000000178和96.00000000000198 mm²；
P11面积14193.520000000206 mm²、材料体积98322.38400000143 mm³，形心约(12.7,25.4,38.1) mm。
P11定义误差上界3.6970420052010973e-13 mm，最终一致性/全段UV上界2.33075346995644e-13 mm，
体积上界2.6630004867911343e-9 mm³，均符合原Q；没有扩大预算。
P06/P07/P11的工作容差均保持1e-5 mm，不随源单位变成厘米或英寸。

### 普通失败与资源清理

每配置F1/P03/P04/P05/P11分别测量880/1552/4000/3872/4000次同进程导入，
混合成功、逐native调用限额、后验不足与报告超限；失败不发布部分模型。
P11的250个native调用预算检查点全部覆盖，完整候选报告拒绝也释放候选。
同一观察器保留64个模型作阳性对照，释放后回到持有前基线。

| P11接受判定的分配域 bytes / blocks | Release，进程堆 | Debug，共享CRT用户分配 |
|---|---:|---:|
| 测量开始 | 1,929,204 / 4,107 | 67,587 / 1,085 |
| 测量结束 | 1,929,204 / 4,107 | 67,587 / 1,085 |
| 持有64模型前 | 1,929,204 / 4,107 | 67,587 / 1,085 |
| 持有64模型时 | 30,926,644 / 168,267 | 34,267,971 / 375,101 |
| 释放64模型后 | 1,929,204 / 4,107 | 67,587 / 1,085 |

沿用08已经核对的当前共享调试CRT、非TBB SDK分配域，4096字节/8块增长门槛和
65536字节/256块正控门槛均未改变。总堆/CRT/PrivateUsage和全部五次采样原值留本地。
Debug P11总堆本次也保持2,037,278字节/4,896块；这不推广为任意SDK分配器资格。
普通失败清理不等于任意OOM、崩溃或硬取消恢复，进一步生命周期合同仍留11。

## 两轴审查

### Standards

针对 `712e4cc...78ce106` 的只读审查：硬性规范违规0，可操作缺陷0，设计smell建议0。
单位源解释留在04，native消费已验证比例；没有新增无需求的公共抽象或倒置模块依赖。

### Spec

同一固定差异的只读审查：遗漏/部分实现0，未授权扩展0，错误实现0，阻断项0。
确认SI只缩放一次、LINE t与PLANE UV分开、D3/D4因果、typed Deferred交接及P13/N34资格边界。
两轴审查未执行测试；运行结果由主执行进程记录。

## 身份与范围

内部STEP组件/适配器为C++20，VS18/v145/MSVC19.50、Python3.14。
沿用05匹配源码的Debug/Release原生SDK，SMLib仍为C++17；本票不改kernel/SM_API或全仓标准。
01材料/独立真值/Q/schema见证、03实现、测试包机制均未改，没有OCCT读取依赖。
构建、二进制、运行日志和资源原始观测只留本地，不新增测试包发布或迁移。

最终脚本终态exit 0，验证前后78份源码身份一致，并核对SDK源码、DLL哈希、实际加载路径及DEPENDENTS，
无OCCT/STEPcode/USD读取依赖。该运行绑定实现提交78ce106，收尾提交仅修改文档/票据。

Release CLI SHA256：`58f994ab02178a903e0da736504a112689f4257d496d42111daee93c961450f4`。
Debug CLI SHA256：`00f41cea9a9e81abb83ee28c557ba791e7db155597898e566f03661d3c650dd3`。

本机根目录 `C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/`：

- `ticket09-qualification/validation.json`：最终运行、逐文件源码/二进制身份、两配置资源原值、
  实际查询与316个历史预算比较。SHA256 `9baef5dd9c9d97246d16d92e294813a0339232bf24cb87fa35919d8d1a79cf50`。
- 同目录保留全部stdout/stderr、13目标CTest详细日志、P01/P03/P06/P07/P11实际查询与DEPENDENTS输出。
  编排脚本为 `ticket09-qualify.py`，顶层日志为 `ticket09-qualification-run.log`。
- `ticket09-si-red.log` / `ticket09-si-green.log`：真实SI导入合同Unsupported→Accepted。
- `ticket09-conversion-red.log` / `ticket09-conversion-green.log`：真实P11合同Deferred→Accepted。
- `ticket09-mapping-red.log` / `ticket09-mapping-green.log`：真实CLI缺unit_normalization→合同通过。

早期焦点检查曾发现04新单位诊断缺related_spans序列化，已补齐有来源的相关位置及expected/actual，
最终完整回归通过。旧SI字节未因新增单位诊断字段改变；未修改冻结诊断期望来迁就实现。

10可选来源声明、11进一步生命周期/取消、12首profile汇总尚未开始。
本票不自动授予一般单位/几何/AP、修复、截面/切割、GUI/公开绑定、worker或全内核C++20/23资格。
