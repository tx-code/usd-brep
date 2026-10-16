# Import evidence package：执行、发布与复核重构

2026-10-16，用户选定架构候选一，并确认本实施/验收约定。
审查固定点为已验收STEP基线提交`7560aa13b403aa1cb6aba6024a46ced4f9c1a20d`。

只重构02—04的执行、发布与复核module；01工具、冻结303材料、独立数学/类型/规则见证及native C++保持原字节。
原始源、源语义、平面检查的分工和qualification保持。05未启动。

## 已确认的seam

调用者和测试继续使用各stage的`run(binary, corpus, output_root, selected)`、`check(run)`及既有CLI入口。
共享module集中实际执行、来源身份、暂存、封口、发布和复核。三个真实stage adapter保留各自compare、见证、scope、
观察文件名与状态/计数字段；没有任意插件系统。源码比较器可以原逻辑迁移为独立文件，以解除其与执行module的反向依赖。

旧`t02/t03/t04.evidence.v1`按其记录协议离线复核，不要求原始可执行文件、staging输入路径或当前源码仍存在；不升级旧资格。
新包显式标注共享package协议和完整执行依赖身份；保留三个stage原有wire格式及返回字段。未知共享协议拒绝。
已有正式包与历史源码身份不改写。

## 验收

- 三个stage的真实CLI、现有`run/check`入口、固定49项结果和资格含义保持；native stdout原字节保留。
- 共享发布协议只有一份implementation：失败留在staging，不发布半包；重试有新ID，旧run不覆盖。
- 新包复核身份、完整payload、选择/计数、材料/准备/独立核算/观察/比较的对应关系；矛盾或未声明文件不能靠重新封口通过。
- 新执行统一记录和前后复核二进制及实际工具依赖；离线复核不读取当前二进制或当前源码来冒充历史身份。
- canonical路径及实际发布位置可核验；不能通过路径别名把staging当成已发布run。
- HTML从stage描述生成范围；04不得写03；全部完整导入/native模型仍NotRun，import success count为null。
- 逐个red→green切片验证上述行为；不测试私有方法，不mock自有module；末尾一次完整STEP范围测试、两个独立审查轴及本轮提交。

固定语料正确性、native算法与有界JSON输出机械行为不属于本轮新实现。SHA/一致性复核不宣称对恶意重新编写的执行来源作真实性认证。
