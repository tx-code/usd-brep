# 08: 同形闭合表面保留surface-model角色

**What to build:** 调用者导入同形盒体的两种源表示，能得到相同精确边界和不同形状角色，表面模型不会被自动升级成solid。

**Blocked by:** [07：从盒体STEP保留共享边和材料实体](07-shared-edge-solid-box.md)。

**Status:** closed

**Assignee:** codex

**Execution:** completed。2026-10-16用户明确授权的08已实现并与07材料盒体对照验收；09—12保持on-hold。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [x] P05的MSSR/SBSM/闭壳关系、共享拓扑和面积与P04对照正确，源表示和角色信息可查询。
- [x] material volume/InsideSolid资格为NotApplicable，不能返回0假成功或因有向积分可算6000就变solid。
- [x] surface与solid路径的owner/报告独立；普通FACE_SURFACE等未准入分支仍明确Unsupported。

**完成记录：** [08验收](../../../docs/step-import/ticket08-acceptance.md)。实现148dc22；双配置各11个CTest目标、23组真实CLI检查通过。P04/P05边界同形，P05保留ClosedSurfaceModel及MSSR/SBSM/闭壳源链，材料体积/内外分类NotApplicable；实际native区域全void，不按拓扑谓词升级材料。普通FACE_SURFACE等仍Unsupported；仅本地提交，09—12保持on-hold。
