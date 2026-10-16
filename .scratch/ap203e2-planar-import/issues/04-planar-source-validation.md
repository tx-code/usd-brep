# 04: 平面的必要规则与有向区域可独立验收

**What to build:** 通过inspect即可判定最小平面的源规则、定义一致性和区域是否满足本增量的导入前提，给出可复核报告而不依赖native构建。

**Blocked by:** [03：单零件、类型与SI单位的源语义可检查](03-product-context-si-units.md)。

**Status:** closed

**Assignee:** codex

**Execution:** completed。2026-10-16按用户授权实现并验收04，沿用01固定样件与诊断，不依赖OCCT读取、不进入SMLib构建。C++17及该STEP链路的C++20兼容性均已验证，全仓标准未变。

**Context:** 遵守[票组约束与来源](../README.md)；用户已确认12票拆分及依赖关系。

**Acceptance criteria:**

- [x] 实现相关祖先/TYPE的WHERE、UNIQUE/INVERSE，以及47条RULE在当前能力/封闭守卫下的适用性；5必查/1条件/41守卫的结论有输入证据。
- [x] SDR/PDR、EDGE_LOOP/LOOP和间接RI使用者不漏计；QUERY规定UNKNOWN与未实现/未判定严格区分，不私修原schema。
- [x] 源LINE/PLANE、四层sense、环身份、区域及误差证据独立检查；8mm、0.001mm、零LINE、PATH与首尾不同责任有对应反例。
- [x] 单次mutation的首个阻断与后果按目录报告，不要求所有后果同时输出，也不把Unsupported伪报源非法。
- [x] 只有已支持的形状/语义获得这一层证据，其他case保持准确Unsupported/NotRun；所有源阻断均未开始native构建。

**Resolution:** [04实际验收记录](../../../docs/step-import/ticket04-acceptance.md)。独立STEP_PLANAR与真实CLI交付，49项当前合同匹配：11 Checked、15新增Rejected、17上层准确阻断、6 conversion Deferred。47全局RULE的5/1/41、祖先/TYPE/UNIQUE/INVERSE、精确误差/参数/有向区域都有独立证据。C++17两种配置及C++20各14组新平面＋20组既有C++检查、两版Python各35项CLI测试通过；C++17/20全49输出相同。完整导入/native模型仍NotRun；05技术前置满足但未开工。
