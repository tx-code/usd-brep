# 01票验收记录：固定样件与独立证据

2026-10-16（Asia/Shanghai）。用户“可以开始实现了吗”解除01票的暂不编码限制，
本次只实施[01票](../../.scratch/ap203e2-planar-import/issues/01-fixture-evidence.md)。
01的四条原验收条件已完成；02—12仍未开始。既有设计方案中的“未生成／NotRun”
是当时状态，本记录只更新材料工具的实际验收，不改写总体规格0.1或其余阶段资格。

交付入口：[工具说明](../../tools/step_import_validation/README.md)、
[内部格式](../../tools/step_import_validation/contracts/v1/README.md)、
[冻结清单](../../tools/step_import_validation/corpus/ap203e2-planar/v1/manifest.json)。

| 实际执行维度 | 结果 |
|---|---|
| 固定输入 | 13正例、36反例，各有完整STEP、材料hash、父例和有序变更 |
| 材料核验 | 49项实际完成，Passed；负例的预定坏输入原样保留 |
| 独立核算 | 49项实际完成，Passed；正例精确数学与负例事实分开 |
| 双目录复现 | 303个材料文件全部字节一致，包含input/truth/assertions及清单 |
| 工具控制 | 12组14子场景全部触达指定检查，原始语料/基准证据不变 |
| 自动测试 | Python 3.10.5和3.14.3各28项通过 |
| 报告 | P01/N20/N02各自单例包和全49项包通过完整性检查 |
| 中断续跑 | 保留真实写入中断的partial，完整性检查拒绝；另一个新ID重跑成功 |
| 产品导入 | NotConfigured / NotRun=49；observed=null、comparison=NotChecked |

没有产品读取器、native adapter、CAD库、模型库读取或编译参与此次验证。
import_success_count为null，未填内核误差、调用数或泄漏数。

## 冻结身份与资源口径

- Corpus：`ap203e2-planar/v1`，格式`t01.v1`。
- Manifest SHA256：`6305839e767a4884858f6b2b19df32c2644a0ca2f49d10cee50b834050bc1f82`。
- Reference SHA256：`c8455f8c3d36150b37bf611ac8dacfe84d6f18b1327a53bcf7b53c7c971b6d4a`。
- 工具源码集合SHA256：`b9823048879844ec657220ecbfbbaf3edf6d25b7cbcaf070c25bb810a0c6f152`。
- 测试源码SHA256：`e44e4fe4772220e7ac6b5a70b321333fc58e140888ecacb81b0511635c99116f`。
- 总输入162,906字节，最大单例5,966字节、149次DATA出现；记录括号最大嵌套3。
- 工具上限：单例11,932字节、合计325,812字节、1,000条记录、记录嵌套16、单报告文件10MB。

嵌套指每条DATA value排除字符串后的括号嵌套，不是产品引用图深度或Part21完整限额。
这些是材料工具的有限配置。Q仍为原设计的显式合成测试配置，不是生产默认或native实测容差。
固定HEADER时间仍为2026-10-15T00:00:00；运行UTC在15日19时，对应本地16日。
语料STEP/JSON禁用Git换行转换，避免Windows检出改变字节身份。

## V01—V12与原票四条验收的证据

证据根目录由调用者指定；本机路径记录于本地HANDOFF，输出入口为`index.html`。
下表的路径均相对该根目录，运行ID不是设计期占位值。

| 验收 | 实际证据 |
|---|---|
| V01 清单 | 冻结manifest的49个唯一ID、各case的profile/Q/#7/repair与来源绑定；全组preparation |
| V02 复现 | `qualification/corpus-reproduction.json`逐文件记录303项相同hash |
| V03 记录/图 | 每例inventory及input_proof；N02为52次/51身份，N08/N28保留5/11个孤立RI |
| V04 单变更 | 36个mutation逐一核父hash、old/new、span、base64、完整字节重放；CTL06正确拦截错父例 |
| V05 数学 | oracle中的shoelace/矩形积、轴区间/行列式体积、精确最近点及查询分类；实际字节另核定义/身份 |
| V06 单位/资格 | P06面积9600、P07面积96、inch精确127/5；P05体积N/A；P03负UV；Q变更负控制 |
| V07 数值界 | √37/2、√21含有理包含证明，宽度≤1e-12mm；测试验证包含及预算 |
| V08 单例 | P01 `runs/20261015T193215Z-eef63572787a`；N20 `runs/20261015T193216Z-3a49640f6b1c`；N02 `runs/20261015T193218Z-abdcc65c83f0` |
| V09 状态 | 全组及单例observed/comparison/summary明确NotRun/null/NotChecked；CTL07拒绝假Matched |
| V10 控制 | `controls/20261015T193220Z-9755c31c9abf`：14份设置diff、真实检查trace、verdict及保护收据 |
| V11 中断 | `qualification/interruption-demo.json`及保留的partial；CTL08/09/12验证缺产物、错覆盖、错提交身份 |
| V12 全组 | `runs/20261015T193210Z-47a1a9717dfe`完整包；两版28测试的日志/收据；本记录 |

原四条条件分别对应V01—V04、V05—V07、V09—V11、V08/V12，原条件文字未改。
实现将完整出现账本放在`inventory.json`，recipe/mutation只保存生成谱系和有序编辑，
这是对方案中合并描述的文件组织细化。

测试日志和解释器身份分别在：

- `qualification/python-3.10-20261015T193016Z-ac67e98e/`：28项、exit 0。
- `qualification/python-3.14-20261015T193016Z-34918b38/`：28项、exit 0。

运行包使用staging→hash/必要角色检查→同文件系统改名至runs的提交过程。
Complete证明该包完整，报告检查不重新证明数学执行，也不替代真正的产品结果。
工具控制副本的重封hash只为触达指定检查，不能回写冻结材料。

## 可复用边界与后续事项

后续票可复用case选择、固定输入身份、expected和证据发布路径；01未提供产品观察adapter。
当前记录扫描器只适用于自行定义的一行一记录ASCII样件，不是Part21读取器或EXPRESS执行器。
source规则诊断来自固定合同和独立事实，不能据此宣布49项已被正确导入／拒绝。

可信根为设计和分别转录的reference；没有声称人工独立复核或完整AP认证。
N2635仍明确为MBx实现稿，未声称与最终ISO规范字节相同。
生产默认精度、SMLib构建/清理、一般故障取消、真实模型库和后续建模资格继续待验证。

总体规格0.1 SHA256保持
`9a6f364e38eeb43fee5bb7b05e151eb7102cc80f402ae24f508adc55a566d6fa`。
本次未改生产source、未动网格算法，也没有提交、推送或远端issue。
