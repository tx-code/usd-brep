# 02票验收：自研STEP源解析与inspect

2026-10-16。用户明确授权实现[02票](../../.scratch/ap203e2-planar-import/issues/02-step-inspect.md)，
要求不依赖OCCT读取，按01票冻结样件和诊断合同验收。本票五条原验收条件已完成，
03及后续票未开始；总体规格0.1及01票冻结材料保持原样。

交付：[C++源读取库与CLI](../../source/STEP_SOURCE/README.md)、
[证据adapter](../../tools/step_inspect_validation/README.md)。

| 实际执行 | 结果 |
|---|---|
| 01冻结49项输入 | 02源检查合同49项匹配；47项Inspected，2项准确Rejected |
| 基础P01 | 完整HEADER、51条DATA、原值树及所有引用可inspect |
| N02 | D0 DuplicateInstanceId；第二#100为primary，第一处为related |
| N03 | D1 UnresolvedRequiredReference；#9、SDR参数1、缺失#99999及token位置 |
| MSVC Debug／Release | 两种配置构建成功，各10组C++契约检查通过 |
| CLI／adapter | Python3.14.3＋Debug、Python3.10.5＋Release，各31测试通过 |
| 边界 | 输入／DATA记录数／值数／引用数／深度／字符串／记录字节／ID位数／报告字节N及N−1 |
| 所有权 | 输入buffer改写、原文件改写/删除、outcome释放后，保留的只读snapshot仍有效 |
| 完整几何导入 | 49项NotRun、合同NotChecked；import_success_count=null |

Inspected只表示完整语法、ID索引和引用检查完成。其余34个负例的schema绑定、必需槽、
类型、enum、规则、单位和几何问题留给后续票，不因本轮源检查成功而合法化。
`native_build=NotRun`指SMLib BRep构建未运行，不指C++尚未编译。

## 交付与边界

内部`STEP_SOURCE`是独立C++17静态库；`step_inspect`输出原始源检查JSON。
独立CMake构建不需要kernel、USD或第三方解析器；root Premake已登记两个目标。
本轮验证的是该组件的独立CMake构建，没有重建整个仓库。

当前仅声明自包含ASCII、implementation level `2;1`、单个无参数DATA：
三个必需HEADER、simple/external-complex原值、typed/aggregate、整数/实数字面量、
字符串、enum、`$`/`*`、ID/引用。支持注释、跨行记录、LF/CRLF/CR及两个基本字符串转义。
多DATA、其他implementation level、额外HEADER、外部引用/签名、ZIP/XML/gzip、
BOM/非ASCII、Unicode/code-page字符串指令、binary/user-defined/value-instance明确不支持。
不据这49项宣称完整Part21、AP203e2符合性或总体P1退出。

未知普通实体和complex成员完整保留，不授予profile支持；准入类型/schema解释从03票继续。
数值保持精确字面量字符串，ID使用有界十进制字符串索引，未按最大ID分配容器。
普通引用环只建立索引，不递归扩展或提前当作源非法。

原始字节先捕获并计算SHA256，所有span及值树依附该不可变输入。
完整语法、ID和引用检查通过后才交付SourceSnapshot；失败不交付部分记录图。
捕获完整时失败仍可保留原始字节；I/O未完成或超输入字节上限时不冒充完整捕获。
这是独立的源层，不含中立几何或native模型。

## 五条原验收条件的证据

| 原条件 | 实际证据 |
|---|---|
| 完整原值且无native | 全49项native CLI输出，与01出现账本逐项核对HEADER/DATA、constituent、值kind/精确文本/slot span、引用owner/path/target；CMake只链接本库与标准运行库 |
| P01及文件/引用拒绝 | P01完整51条；N02两处ID位置、N03准确引用位置；失败时header/records/references均null、source_complete=false |
| 前向/乱序/完整读取 | 反序P01仍为同一51记录图；100位稀疏ID保持字符串；尾部错误拒绝整个源；普通环不作无限递归 |
| owning与基本资源 | C++保留snapshot后改变/删除原路径仍可查；const视图静态检查；每项基本限额N/N−1，报告超限输出为空 |
| 准确声明范围 | CLI、API文档、case/summary都声明SyntaxIndexReferences；schema_validation和SMLib构建NotRun，完整导入NotChecked |

Windows报告测试实际发现stdout文本模式将末尾LF扩为CRLF，可能超过字节预算。
已改为二进制stdout，并用实际输出长度验收恰好N和N−1；没有通过放宽预算绕过。
出现账本和输入原始换行不被改写。
最终复核还补齐记录间错误的上下文清理：分隔处坏字符保持准确位置，但不再归给上一条记录；回归断言已通过。

## 正式运行身份

证据根由调用者配置，本机路径在本地HANDOFF；该根的`index.html`为阅读入口。
以下路径均相对该根：

- 全49项：`runs/20261015T225852Z-103a9ec84690/`，机器产物/HTML及artifact/completion完整。
- 最终构建与C++检查：`qualification/build-final-20261015T225818Z-c5b1bb913f6c/`。
- Debug/Python3.14：`qualification/tests-final-Debug-python14-20261015T225850Z-be75b9363091/`，31测试、exit0。
- Release/Python3.10：`qualification/tests-final-Release-python10-20261015T225850Z-7d6e8b139db1/`，31测试、exit0。
- 汇总与来源：`qualification/acceptance-final.json`；最终Release EXE另存于`qualification/native/`。

MSVC 19.50.35728、CMake VS2026 generator；真实命令、配置、日志和源码hash均记录。
Release EXE SHA256：`32e0c7a6edfb3a4f271cfd11686d20c8158ece89562df31b4b16f89dd32056e4`。
组件/adapter源码集合SHA256：`d0c2aed3a49e7787ec0088ff32f163b7a3363ebd266a14cd328ca5e81519132a`
（按acceptance-final.json的sorted compact JSON映射计算）。

Release依赖审计只有C++/C运行库和KERNEL32，未出现OCCT、SMLib或USD。
这与源码/构建依赖隔离一起证明本轮读取路径没有借用OCCT。
没有调用CAD读取器作为真值或后台fallback。

N02真实位置：primary `[2482,2486)`，第59行第1列；related `[1281,1285)`，第23行第1列。
N03真实位置：`[903,909)`，第16行第36列；owner #9、component
`SHAPE_DEFINITION_REPRESENTATION`、parameter_path `[1]`、target_id `99999`。
原始字段名definition由已固定合同识别；源解析器对其他实体不猜属性名或类型。

## 完整性与仍未验证的能力

新adapter使用独立`t02.evidence.v1`，保存真实CLI stdout、退出码、实际输入副本、
01账本/期望及独立比较。staging校验后才改名为正式run；checker重核payload、
必需角色、scope状态及原始观察与账本，不以完成标记推断实际模型导入。
checker控制能发现丢constituent、丢引用、假完整状态；完整导入期望与观察始终分开。

01的303个冻结材料逐文件hash未变，01工具集合SHA
`b9823048879844ec657220ecbfbbaf3edf6d25b7cbcaf070c25bb810a0c6f152`未变。
总体规格0.1 SHA仍为`9a6f364e38eeb43fee5bb7b05e151eb7102cc80f402ae24f508adc55a566d6fa`。
本次只新增源层/验证工具及其集成，没有修改SMLib算法、网格化或现有绑定。

完整Part21/编码、schema/EXPRESS执行、产品/单位语义、几何转换、SMLib模型/清理、
取消/硬隔离、真实模型库及跨平台构建仍未取得资格。本轮未做全仓构建/门禁、
sanitizer或任意分配故障注入，也没有提交、推送或远端issue。
