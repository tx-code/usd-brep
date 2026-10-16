# 01票工具控制执行清单

版本：T01-ControlRunbook-0.1，2026-10-16。配套[01实施方案](01-fixture-evidence-plan.md)与[内部合同/CTL设计](01-data-contracts-and-controls.md)。**这是执行前文档，不是执行记录。12组控制、14个子场景全部Designed/NotRun；未创建控制数据、脚本、JSON、STEP或报告。01继续on-hold，原四条验收和12票依赖不变。**

本清单解决一个具体问题：如何证明控制确实触及了目标检查，而不是被旧hash、错误父例或缺文件更早挡住后就算“测过”。产品STEP的D0—D5、49个样件与本工具控制分开，控制不会开始产品loader或native调用。

## 1. 控制前提及四个边界

后续实施时，先由正常工具核验所需的基准语料与证据包。基准材料、参考来源或正常oracle未达到本轮控制的前提，控制状态为BlockedByBaseline；不得通过改变expected使其勉强开始。

| 范围 | 可变性与用途 |
|---|---|
| 冻结合成语料 | 固定49项输入/谱系/truth/assertions/Q及其真实hash；控制不得修改 |
| 可信参考包 | 原审阅记录/数学/变换/反例锚点及固定hash；不能随控制变体更新，用来约束case真实含义 |
| 控制变体 | 每场景独立临时副本；只允许登记故障和必要的摘要/元数据维护，不把此变体发布为新golden |
| 控制证据根 | 控制器自己的control_run/attempt/结果，和被校验的candidate证据包分开；不混入正常49项统计或正式导入结果 |

控制器的control_run_id与被核验包的candidate_run_id不是同一概念。副本可保留基准包内部ID，并在专用控制根中维持真实的staging/runs结构；不能因此复用旧通过结果，也不在普通运行目录写假包。缓存复用须比较内容、配置、工具和参考hash，不能只看case/run名字。

所有控制都要求可信参考包、基准语料及原完整报告在运行前后hash相同。若控制临时路径解析后落入这些受保护目录，设置失败并停止；不继续实验再清理。当前只是路径约束设计，没有进行任何复制/移动/删除。

## 2. 统一执行顺序

未来每个子场景独立执行以下流程，不能继承前一个被破坏的副本：

1. **选择前提。** 锁定control_id/variant、基准case或run、可信参考hash、配置及工具版本；确认所需正常材料检查实际通过，subject仍可未配置/NotRun。
2. **建立独立副本。** 用新的控制attempt保存副本身份和基准hash；受保护材料不成为输出目标。
3. **核验故障设置。** 按本清单的允许变更实施故障。保存实际diff、旧/新值与定位，确认目标坏值确实存在。
4. **按场景维护摘要。** 只有要求“摘要一致”的场景才更新变体及其直接/传递引用、receipt、artifacts/completion；可信参考不改。不能调用会把目标故障修掉的正常重算流程。
5. **设置复核。** 对照允许变更集合：除注入故障和指定摘要/派生计数维护外，不应有其他编辑。旧span不符、多匹配、目标故障消失或额外改动均为SetupFailed，不算工具抓住了错误。
6. **执行同一核验路径。** 把变体及明确固定的可信参考交给正常材料/证据核验器；HarnessControl只提供隔离和控制身份，不允许跳过目标规则。记录实际到达的检查、输入与反证。
7. **独立裁决控制。** 对照本清单的目标检查、预期结果、必须证据及禁止替代结果；不能只匹配一段异常文本或任意非零退出码。
8. **提交控制证据。** 保留真实工具输出、控制器判定和基准未变证明。失败/未判定原样保留；再次尝试使用新attempt，不覆盖原证据。

步骤3—5是故障注入器的自检，步骤6才是被测工具；二者不得由同一“预期失败”布尔值贯穿后直接写Passed。

## 3. 摘要维护与目标检查的区别

| 类别 | 场景 | 设置规则 |
|---|---|---|
| 故意破坏字节身份 | CTL01 | 改input，不更新其登记hash/receipt；应先发现实际payload身份不符 |
| 合同/数学/状态故障，身份仍一致 | CTL02/03/05/06/07/09/10/11 | 更新变体中受影响的材料引用和摘要，让目标故障仍在。预期不能只靠旧hash报错；不更新可信case参考 |
| 角色缺失但已有文件摘要一致 | CTL08 | 缺失oracle文件并从变体artifacts列项中移除，重新绑定清单；必需角色由可信合同决定，不由变体自行删减 |
| 正控制 | CTL04-A/B | 不注入故障，分别使用完整P06/P07及各自正确参考；不得改回mm或父例真值 |
| 故意破坏提交绑定 | CTL12-A/B | payload及artifacts保持有效，只将completion的run绑定或artifacts hash改错；不维护目标坏字段 |

“维护摘要”只指让控制的自有文件身份一致，不是授权改变case、profile、参考版本或期望诊断。若为了达到目标必须协调修改多个元数据字段，全部列入设置receipt；这些工具控制不声称是36个STEP反例中的“单字段变更”。

对于证据包，维护顺序为payload/必要派生计数→artifacts→completion。artifacts不含自身与completion；completion绑定清单而不自含hash。被改comparison中的Matched、被删的必需oracle等故障必须在维护之后仍存在，不能被重新生成report时抵消。

## 4. 子场景、触达证明与通过条件

本表中的路径是已有内部材料的语义位置，未来编码时应有唯一字段定位映射；当前没有同名实际数据。阶段列是**工具检查责任**，不与产品D0—D5混为一套顺序。

| 子场景 | 基准与精确设置 | 必须触达的检查／证据 | 控制通过条件；不能接受的替代 |
|---|---|---|---|
| CTL01-A | P01 input的#101坐标token`10.2→10.3`，其登记input hash不变；只改这一个字节 | 实际输入读取/hash；保存旧/实际摘要及差异span | 检出Preparation.HashMismatch；不是source几何拒绝、路径不存在或未运行 |
| CTL02-A | P01 truth.area从96/1改95/1；同步该临时变体的材料hash及引用，输入与可信F1参考不变 | 身份有效后，独立数学复核实际取得可信96而候选truth为95 | Oracle.ExpectedValueConflict及95≠96；只有hash不符不算触达数学检查 |
| CTL03-A | 按合同中的四个输入字段协调改成11×8，truth/账本及变体摘要也改为面积88/边界38/形心(3.1,4.8,5)；case目标仍P01、可信原F1不变 | 已通过变体身份对账后，对实际case定义与可信12×8记录/数学锚点分别核验 | 至少有Reference.InputDefinitionMismatch；若安全继续truth核查，88与可信96也冲突。只算11×8与同一错误点的shoelace都等88不能通过此控制 |
| CTL04-A | 正常P06，只有长度声明由mm变cm，源坐标/magnitude未缩小 | 绑定P06的变换锚点，核物理10倍、面积9600、边界400和bbox | 材料/oracle结果正确且无此类语义错误；subject仍NotRun。把它当单位非法或强改回父例即控制失败 |
| CTL04-B | 正常P07，cm声明且源点/plane O/magnitude协调除10 | 绑定P07参考，世界F1不变、面积96、边界40，LINE t仍0..1 | 材料/oracle正确，世界几何与源参数映射都符合；不得沿用P06的10倍真值 |
| CTL05-A | N02原input仍含两处#100，临时发生账本删去第二处并将其非可信计数统一写51；同步变体摘要 | 直接从实际payload核得52次DATA出现及两处#100的span，而非只相互比较两个声明数 | Preparation.OccurrenceInventoryMismatch，现场52与账本51；泛型hash错误、去重后声称正常或误报生成器已修复不通过 |
| CTL06-A | N34实际改从P11而非P13派生，同步父hash/变体输入及自有receipt；可信谱系仍要求P13 | 优先核case规定父例与实际父例；反证P11缺UMWU毫米锚 | Preparation.ParentContractMismatch；若只等到scale递归或另报孤立mm，说明没完成目标谱系检查 |
| CTL07-A | 有效01证据包中，subject未配置/NotRun、observed仍null，仅comparison伪标Matched；必要汇总计数按该坏值一致更新并重新绑定摘要 | 身份有效后核状态不变量，不将metadata字段当真实观测 | Evidence.StateContradiction及NotRun不能Matched；只因summary计数未维护或旧hash失败不算触达 |
| CTL08-A | 删除一个所选case的oracle证据文件，从变体artifacts移除列项并重新绑定；仍称完整 | 按可信合同的必需角色清单核覆盖，不只遍历artifacts中的现存项 | Evidence.RequiredArtifactMissing，指出case及oracle角色；清单自洽或HTML存在不能算完整 |
| CTL09-A | 选全部49项的有效summary中用P12项再次替代P13项，保持总行数49并重新绑定；其他case实际证据不删 | 核选择集合、case/attempt唯一性及汇总覆盖，而不是只对总数49 | Evidence.CaseCoverageMismatch，明确P13缺失/P12重复；“49行所以完整”或将两attempt算两个case均不通过 |
| CTL10-A | P05候选truth的material_volume由N/A/null改为Applicable、精确0/1并同步适用性说明/摘要；输入角色仍MSSR/SBSM | 格式可解释后核资格：可信角色是闭合表面模型，不是材料实体 | Contract.ApplicabilityConflict；不是认为0数值误差大，也不能把辅助有向体积6000当授权 |
| CTL11-A | P01候选exact面积由rational(96/1)改为JSON number 96.0，单位和适用性不变；同步摘要 | exact编码合同检查，明确值本身可看似相等但表示不符合约定 | Contract.ExactValueEncodingInvalid；不能自动转回rational后当无事，也不需要虚构几何误差 |
| CTL12-A | 有效候选证据的completion.run_id改为不同ID；payload/artifacts及其hash不改 | 提交凭据与候选run、选择及清单的身份绑定 | Evidence.CommitIdentityOrHashMismatch，指明run身份；不能靠目录名或退出码恢复Complete |
| CTL12-B | 有效候选证据的completion.artifacts_hash替换成另一个真实包的不同清单hash，payload/artifacts保持原样 | 实际artifacts字节hash与提交凭据相比较 | Evidence.CommitIdentityOrHashMismatch，指出期望/实际摘要；仅检查字段为合法64位hex不够 |

CTL12-A与B从不同的干净副本开始，不能同时改两个字段而只知道“某处错了”。CTL04-A与B也分别裁决；同一控制组两子场景都满足才可将该组记为Passed。共12组、14子场景，不与49个产品样件数量相加。

允许实际诊断key更细，但须有明确映射及同一反证。没有证明所需前置、未到达目标检查、崩溃或任意报错都不能满足表中的通过条件。必要时可通过正常核验器同一实现提供的独立检查入口验证已具备前提的目标责任，不能另写测试专用放行逻辑或要求损坏输入继续做不安全下游计算。

## 5. 检查遮挡与安全停止

控制器先验证设置的正确性和必要身份前提，再裁决目标检查是否生效。下列情况必须明确区分：

| 现象 | 控制结果及原因 |
|---|---|
| CTL02的truth变更未更新manifest，工具只报HashMismatch | SetupFailed／目标未触达，不能说独立数学checker通过了控制 |
| CTL03使用变体自己改成11的参考width | SetupFailed／可信参考被替换；两公式一致不提供正确的独立性证据 |
| CTL05只引用可信表中的52，没有实际读取重复输入 | 证据不足，不能满足“实际出现/两span”要求；不能以预期值冒充观测 |
| CTL06实际仍从P13派生，或缺少父例hash证明 | 没有注入指定故障，SetupFailed，不以其他异常代替 |
| CTL07重新生成comparison时把坏Matched自动改回NotChecked | 目标故障被设置工具消掉，SetupFailed；需在最终候选包中复核坏值仍在 |
| CTL08的验证器因读缺文件而意外崩溃 | UnexpectedFailure，不是正确的RequiredArtifactMissing诊断 |
| CTL12副本不在被校验控制根的预期目录，先因位置不合法拒绝 | SetupFailed／目录前提不成立，尚未证明身份/hash提交检查有效 |
| 正常CTL04被笼统判成不支持或数学错误 | 控制失败；不能为了使阴性控制全红而牺牲合法尺度语义 |

如果某项独立验证需要的前提已经不成立，就记录检查NotReached及原因；不强行进入被破坏图中的下游计算。与源反例类似，能证明一个错误不等于可以声称所有检查已覆盖；工具控制要证明它自己的指定责任。

## 6. 控制器的独立裁决

| verdict | 条件 |
|---|---|
| NotRun | 尚未实际执行；当前所有场景即此状态 |
| BlockedByBaseline | 需要的正常基准、独立锚点或工具版本/证据尚不成立，不能开始该控制 |
| SetupFailed | 副本/故障/维护摘要/可信边界或允许diff设置不符合清单，目标检查未被有效测试 |
| Passed | 基准及设置有效，实际到达目标检查，并满足该子场景必须证据和预期结果；受保护材料未变 |
| Failed | 设置有效，但目标错误未检出、CTL04误拒、状态/完整性发生假通过，或实际反证不符 |
| UnexpectedFailure | 工具崩溃、报告/IO等意外基础设施故障使目标无法按合同完成；不是“正确拒绝坏输入” |

对阴性控制，候选材料/证据验证器正确返回Failed可以使**控制器**判为Passed；它不是产品case的Matched或导入Accepted。对CTL04正控制，也只证明材料/oracle路径，产品执行仍可NotRun。

无可靠反证或检查触达证据时不得把控制框勾上。一次失败的控制保留原attempt；修改实现或设置后重新运行，不能就地把旧报告改成绿色。

## 7. 每个子场景的最小证据

未来控制证据包至少包含下列内容，按独立control_run/variant/attempt组织；此处未创建这些文件。

| 角色 | 必需内容 |
|---|---|
| control plan引用 | 本清单版本/hash、control/variant ID、目标检查及预期反证、允许注入/摘要维护范围 |
| baseline receipt | 基准corpus/run/参考包的实际hash、所需正常检查结果、执行前保护路径身份 |
| setup receipt | 独立副本位置、真实old/new/span或文件角色、所有允许diff、维护后的有效摘要、坏值仍存在的证据 |
| validator trace | 实际工具与配置身份、输入引用、到达/未到达检查、原始诊断及数值/身份反证；不是只写预计错误名 |
| verdict | 控制器实际裁决、逐条满足/不满足的条件及证据引用；与候选验证器及subject状态分开 |
| protection receipt | 原语料、可信锚点和原完整包的运行后hash与运行前相同；没有受保护内容被覆写 |
| artifact coverage | 必需证据角色、实际hash、缺失/部分状态；控制自己的完整性仍须遵守既有提交合同 |

摘要分别显示12组和14子场景的verdict，当前实际计数只能为NotRun。以后任一子场景失败、未执行、设置失败或证据不足，该控制组就不能记为Passed；不能用某个组的第一次成功隐藏另一次失败。

## 8. 与01验收及后续工作的对应

| 原验收 | 本清单具体证据 |
|---|---|
| V03/V04 材料与单mutation | CTL01、CTL05、CTL06检出错误材料/账本/父例；49个实际样件仍需各自正常对账 |
| V05—V07 独立真值 | CTL02/03防止错误truth和共同错误自证；CTL04守住正确单位语义；CTL10/11守住资格与精确表示 |
| V09 状态防误报 | CTL07及每个控制的subject NotRun，不以没有模型冒充准确拒绝 |
| V10 工具控制 | 12组14场景的真实设置、触达证明、裁决及受保护材料不变 |
| V11 完整性/复用 | CTL08/09/12、控制自己的Partial/提交与新attempt；不冒充产品worker故障处理 |

01还需要完成A—H、49项材料/数学核算和V01—V12的全部对应证据，不能只做这些工具控制就关闭。其他11票的阻塞关系、当前53条未勾选验收及总体规格0.1不变。

本轮完成执行清单的静态细化，当前没有基准run、故障副本、触达trace或真实verdict，01仍未领取/未执行。01的可执行设计已收敛；后续按用户明确指令推进，暂不编码的限制继续有效。
