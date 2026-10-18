# STEP 固定目录重构验收

此工具为 STEP 固定目录编译期迁移提供第一批基线与验收入口。
它比较真实 Catalog 的全部已登记事实，不通过输入样例推测未出现字段。
固定快照是重构等价依据，不是独立 EXPRESS 见证、完整 schema 验证或导入资格。

## 构建与检查

在仓库根目录执行，CMake 需要 C++20 工具链和 Python 3.10+；验收工具仅在
`STEP_SEMANTICS_BUILD_TESTS=ON` 时构建，不成为产品运行依赖：

```powershell
cmake -S source/STEP_SEMANTICS -B _build/catalog-validation -A x64
cmake --build _build/catalog-validation --config Release --parallel 4
ctest --test-dir _build/catalog-validation -C Release --output-on-failure
python -m tools.step_catalog_validation check --snapshot _build/catalog-validation/Release/step_catalog_snapshot.exe --baseline source/STEP_SEMANTICS/tests/catalog/baseline
```

`step_catalog_snapshot` 输出四个 profile 的身份/artifact、原声明顺序、父序、本地字段的全部属性及嵌套维度、
祖先位、展开字段的原声明/本地索引、name→ID、SELECT、枚举、complex signature、derived 槽、
measure 分支、互斥组和 SI 前缀。只有独立名称索引按名称排序；其余数组次序参与比较。
快照通过 STEP_SOURCE 的 JSON writer 完整发布，固定事实从产品目录读取，前缀与互斥组不抄到测试中。

`check` 验证冻结载荷 hash，然后比较完整 JSON 树。它不更新期望值，报告具体路径差异，
不忽略数组次序、bool/整数差异或任何目录字段。`registered_types` 与声明次序也做结构核对。

## 新建基线

只在确实需要新的重构比较起点时显式捕获到**不存在**的目录；检查失败不能通过重新捕获原路径消除。
例如，下面命令会构建真实工具，再执行两次完整快照及两轮各四个新进程的分配测量：

```powershell
python -m tools.step_catalog_validation capture --snapshot _build/catalog-validation/Release/step_catalog_snapshot.exe --allocations _build/catalog-validation/Release/step_catalog_allocations.exe --build-dir _build/catalog-validation --build-info _build/catalog-validation/catalog-build-Release.txt --config Release --output D:/Temp/new-catalog-baseline
```

仅当完整数据一致、计数器正对照成功、直接源文件在构建/测量期间未变，才发布新目录。
提交的 `catalog.json` 保存全部固定事实，`manifest.json` 绑定载荷 SHA256，二者是可重复使用的比较输入。
两轮原始分配计数、源清单、工具二进制指纹、构建命令和工具链信息保存在构建目录中新建的
`catalog-capture-*.json`；工具输出实际路径，不把机器运行记录提交到 Git。
Git HEAD 不代表未提交源内容，本地收据中的实际输入以源指纹为准；这仍不是完整传递依赖审计。

分配 probe 仅在独立、单线程可执行文件中替换 new/new[]，不影响产品库。
普通/数组、aligned、nothrow 共八条路径分别以已知 17 字节请求验证计数器。
首次 Schema、1000 次重复 Schema、短名/长名/未知名 Find 分开计数，窗口外准备和输出。
计数是成功请求和累计请求字节，包含已释放临时量，不是峰值/存活内存；不覆盖直接 malloc。
快照 owner/pointer 地址不跨二进制比较，源 Value 身份继续由原语义合同在进程内检查。

## 正反编译驱动

```powershell
python -m tools.step_catalog_validation.compile_contracts --build-dir D:/Temp/catalog-compile-controls --config Release --generator "Visual Studio 18 2026" --architecture x64
```

驱动按[case manifest](compile/cases.json)编译真实 object target，保存每例日志和 results.json。
负例必须有同环境通过的正对照、非零退出码，以及实际 compiler error 行上的指定标记。
命令行/文件名/警告中的标记、缺头文件、错误期望和失效正对照不能算通过。
每次重新配置/构建，不复用过去的退出码；独立 object target 避免把缺链接依赖算作编译拒绝。

**本批只有驱动控制和当前产品头文件的编译检查。** 人为 static_assert 控制证明的是驱动识别拒绝的机制，
不是未来目录校验器的保障。生产 consteval 构建器的真实 profile、菱形/容量正例与元数据负例，
在第二批实现后加入同一 manifest 和生产头文件，不能先用另一份 validator 代替。

CTest 注册全量基线比较、工具破坏控制、编译驱动、四个分配 probe，以及既有源/语义合同。
工具单测覆盖顺序、未被样例使用的字段、嵌套维度、祖先高位、字段来源、每类登记表、篡改 hash、
缺失载荷、非法 JSON 和错误编译标记。需要测试新的候选基线时，可临时设置 `STEP_CATALOG_TEST_BASELINE`；
正常 CTest 使用仓库内冻结目录。

完整源语义/CLI 诊断、预算和独立 N2635 witness 仍由
[语义验证工具](../step_semantic_validation/README.md)负责。本工具不替代它们，
也不因固定目录等价就授予数值、几何、native 导入或跨平台资格。
