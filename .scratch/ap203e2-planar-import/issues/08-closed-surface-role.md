# 08: 同形闭合表面保留surface-model角色

**What to build:** 调用者导入同形盒体的两种源表示，能得到相同精确边界和不同形状角色，表面模型不会被自动升级成solid。

**Blocked by:** [07：从盒体STEP保留共享边和材料实体](07-shared-edge-solid-box.md)。

**Status:** ready-for-agent

**Assignee:** None

**Execution:** on-hold。用户于2026-10-15明确要求暂不编码；本票仅已建档，获得后续明确开工指令前不得开始实现、生成样件或运行实施验收。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [ ] P05的MSSR/SBSM/闭壳关系、共享拓扑和面积与P04对照正确，源表示和角色信息可查询。
- [ ] material volume/InsideSolid资格为NotApplicable，不能返回0假成功或因有向积分可算6000就变solid。
- [ ] surface与solid路径的owner/报告独立；普通FACE_SURFACE等未准入分支仍明确Unsupported。
