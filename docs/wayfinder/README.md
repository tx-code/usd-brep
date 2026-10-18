# 本地决策跟踪

本仓库的 Wayfinder 地图与决策票使用本地 Markdown。用户要求不创建远端 issue 或 PR；此目录不调用远端 tracker。

## Wayfinding operations

- 地图：每个专题使用 `<专题>/map.md`，标签 `wayfinder:map`，作为该努力的唯一地图索引。
- 专题票：`<专题>/tickets/*.md`，元数据记录标题、父地图、标签、状态、负责人。
- 领取：开始工作前，将票的 `assignee` 从 `null` 改为执行者；已领取的票由其他会话跳过。
- 依赖：本 tracker 没有原生阻塞关系，票体 `Blocked by` 使用具名相对链接。前置票全部关闭才可开始。
- frontier：状态 open、assignee 为 null、全部前置票 closed 的子票。按票文件名顺序选择，但对用户始终以标题指代。
- 研究进行中：保持 open 并填写 assignee；结果写单独证据文件，票的 Context 链接其路径。
- 解决：在 `Resolution comments` 追加结果与证据，状态改为 closed；地图 `Decisions so far` 仅追加具名链接与一行结论。
- 需要人类取舍的票必须经过真实对话。不得把推荐答案当成人类已批准。
- 地图只保存索引；具体决策只在对应票保存。用户已明确的努力约束记录于地图 Notes。
- 各专题的终点与执行范围由其地图定义。默认先调查与决策，实施须有该专题的明确授权。

## 专题索引

- [C++20 深化与编译期保障](cpp20-deepening/map.md)：进行中；评估编译期约束、固定描述与派生数据的深化路线。
- [独立 STEP 导入与受限修复的设计规格](step-import/map.md)：设计地图已关闭，保留既有决定与实施依据。

规格0.1确认后的P0文档细化见[STEP设计附件索引](../step-import/README.md)。它不重开已完成地图，不改设计基线或自动启动编码；实体/版次资格与用例运行状态分别记录。
