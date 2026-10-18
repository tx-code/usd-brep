# Rust Truck / ruststep：独立 STEP 导入与修复评估

调查日期：2026-10-15。范围仅为精确 STEP 导入、拓扑与修复；不修改 SMLib 网格化，不使用 OCCT 读取。

## 结论

Truck 值得作为实现参考和实验对照，但当前不宜直接作为“可信导入完成”的边界。它确实提供独立 Rust STEP → 精确几何/拓扑的路径，并非只会输出网格。最大的直接风险是转换过程中允许跳过失败实体、边、面后返回成功。建议优先评估 ruststep 的语法/实体引用层，自建保留源实体编号、单位、每个面上 pcurve 与诊断信息的中立导入模型，再接 SMLib；不要先把数据压进 Truck 的 CompressedShell 后再试图恢复全部 STEP 语义。

这是基于下述源码与有限探针的工程建议，不代表已经实现 SMLib STEP 导入，也不代表完成工业样件验证。

## 固定版本与实际验证

- Truck：`88ed005249e5e3a6b07f62425399435905cd3ab6`，`truck-stepio 0.3.0`。[依赖定义](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/Cargo.toml)
- ruststep 官方源码调查：`cd89e5a7cd9a6baadcb456392a5888f4cbc6b707`。实际 Truck 探针按其 Cargo 依赖解析使用 crates.io `ruststep 0.4.0`，**没有声称该发布包等同于调查 commit**。[ruststep manifest](https://github.com/ricosjp/ruststep/blob/cd89e5a7cd9a6baadcb456392a5888f4cbc6b707/ruststep/Cargo.toml)
- 本机 Rust `1.95.0`。源码副本和探针位于 `C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/`；未修改上游源码。
- 实际运行 `cargo run --manifest-path C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/probe/Cargo.toml`，退出码 0。探针依赖采用 `features=["in","derive"]`。最初仅启用 `in` 的构建失败：输入模块仍引用可选的 `truck_derivers`；恢复默认组合后成功。未修上游 feature 配置。
- 原计划运行上游 input 测试，但为避免整套依赖/资源测试范围扩大，主动中止，改为独立探针。**没有上游完整测试通过的结论。**

已验证的四个小例子（均为合成 DATA 段，不是工业整文件）：

| 输入 | 观察结果 | 能证明什么 |
|---|---|---|
| 直线边构成的平面三角形开放壳 | 3 顶点、3 边、1 面，返回成功 | 精确壳转换入口可运行；这里只检查计数 |
| `SI_UNIT(.MILLI.,.METRE.)` | 保存在 `Table.dummy` | 当前 Table 不将此实体解释为有效单位语义 |
| `CLOSED_SHELL('',(#99))`，#99 不存在 | 返回 `Ok(CompressedShell)`，面数 0 | 仅检查 Result 成功会误收缺失面模型 |
| `CARTESIAN_POINT('',42.0)` | stderr 报反序列化错误，仍返回 Table，点数 0 | 便利构表接口吞掉实体错误，调用者得不到结构化失败 |

运行日志：`C:/Users/tangx/Documents/Codex/2026-10-13/usd/work/step-research/probe/run.log`。
未运行真实球/圆锥/周期缝/装配/单位换算/修复效果回归；没有将这些源码能力写成实测成功。

## 分层能力与边界

### ruststep 是语法与实体框架，不是完整 CAD 导入器

ruststep 提供 Part 21 解析、AST、引用/holder、Serde 反序列化和 EXPRESS 相关工具。AST 区分简单与复合实体，Exchange 包含多 DATA 段及其他段。`parser::parse` 返回 AST；这一步不自动构建 CAD 几何、修复拓扑或保证完整语义。其 README 仍明确标为实验阶段。预生成 schema feature 仅列出 AP201/AP203，不能据此宣称完整 AP214/AP242 语义覆盖。[AST](https://github.com/ricosjp/ruststep/blob/cd89e5a7cd9a6baadcb456392a5888f4cbc6b707/ruststep/src/ast/mod.rs#L758-L782)、[解析器](https://github.com/ricosjp/ruststep/blob/cd89e5a7cd9a6baadcb456392a5888f4cbc6b707/ruststep/src/parser/mod.rs#L74-L81)、[features](https://github.com/ricosjp/ruststep/blob/cd89e5a7cd9a6baadcb456392a5888f4cbc6b707/ruststep/Cargo.toml)、[项目声明](https://github.com/ricosjp/ruststep/blob/cd89e5a7cd9a6baadcb456392a5888f4cbc6b707/README.md)

ruststep 的 AP203 模块有 SI/conversion-based/global-unit 类型；这不等于 Truck 实际使用它们并执行换算。Truck 定义的是自己的 Table。[AP203 表](https://github.com/ricosjp/ruststep/blob/cd89e5a7cd9a6baadcb456392a5888f4cbc6b707/ruststep/src/ap203.rs#L70-L150)、[Truck Table](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L25-L106)

### 精确几何与拓扑覆盖

Truck 的输入表与转换代码覆盖直线、多段线、圆/椭圆/双曲线/抛物线、B 样条及有理 B 样条；曲面包括平面、球、圆柱、圆锥、环面、B 样条/有理 B 样条、旋转与直纹拉伸。存在参数曲线和 surface curve 路径。[实体表](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L40-L70)、[几何转换实现](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L1060-L2239)

拓扑层有 VertexPoint、EdgeCurve、OrientedEdge、EdgeLoop、FaceBound、FaceSurface、定向面/壳、开放/闭合壳、ShellBasedSurfaceModel、ManifoldSolidBrep 和 BrepWithVoids。输出是带曲线/曲面的 `CompressedShell` / `CompressedSolid`，不必调用网格化。转换至 Truck 正式拓扑结构时才进一步检查连接与闭合，不能将压缩结构的存在视为有效性证书。[壳/实体转换](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/convert.rs#L181-L274)、[压缩拓扑约定](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-topology/src/compress.rs#L1-L55)

这是明确实体白名单，不能将“有常见 BRep 类型”扩大为全部 STEP 合规。当前输入模块未发现 TRIMMED_CURVE、单位与 uncertainty 的专门分支；未知实体会进入 Dummy。实体解析与真正转换的覆盖还必须分别测试。[输入分派与 fallback](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L109-L770)

### 单位、坐标系、装配

坐标系有 Axis1/Axis2Placement2d/3d 到矩阵的转换。装配不是完全缺失：`step_assy()` 连接产品定义、NAUO、ContextDependentShapeRepresentation 和带变换的 representation relationship，保留 id/name/description，并使用 ItemDefinedTransformation。应把它视为一个可参考的装配子集，尚未验证复杂装配、重复实例、混合单位或各种映射表示路径。[坐标系](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L925-L1058)、[装配转换](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/convert.rs#L315-L469)

未发现 Truck 导入中按 representation context 执行长度/角度单位换算。坐标直接转换，圆锥角直接进入 `tan`。结合 SI_UNIT 探针，应把单位归一化列为集成前必须补齐的功能，而不是默认为 mm/radian。[坐标转换](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L838-L869)、[圆锥转换](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L1883-L1913)

### pcurve、周期缝与方向

`SEAM_CURVE` 被存为 SurfaceCurve；PCurve 由二维曲线与基曲面组成。SurfaceCurve 按 master_representation 选择 3D 曲线或第一/第二 pcurve。EdgeCurve 解析通过端点确定区间；闭合端点的 SurfaceCurve 分支优先走 3D 曲线。它具有一定 seam/pcurve 能力，但不能推出周期展开和奇异点全部正确。[seam 分派](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L174-L185)、[pcurve/主表示选择](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L1626-L1725)、[边转换末段](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L2520-L2578)

CompressedEdgeIndex 只保存 index/orientation，CompressedFace 只保存边环、方向和曲面；它没有每个面内 edge-use 对应的完整多 pcurve 字段。因此若希望保留源 STEP 的两侧 pcurve 与 seam 使用语义，应在压缩前的实体层保存。不能仅从最终共享 3D 边再无损还原这些数据。[结构定义](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-topology/src/compress.rs#L15-L55)

### 错误与部分导入：必须改变的成功契约

`Table::from_step` 只取 `exchange.data[0]`，解析失败变为 None；`FromIterator` 将实体错误打印至 stderr 后继续。壳顶点、边、面与边界转换多次使用 filter_map，无法转换的组成部分可能被省略；ShellHolder 实现最终直接返回 Ok。这不是“失败必定完全导入中止”的 API。[入口](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/mod.rs#L771-L790)、[过滤转换](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/convert.rs#L40-L178)、[最终 Ok](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-stepio/src/in/convert.rs#L480-L492)

建议严格模式按产品/壳事务化失败；允许部分导入时显式返回 `Partial`、缺失实体清单、原始编号、引用链和保留对象，绝不能仅用成功句柄或日志判定完整性。这是本项目拟定要求，不是 Truck 现有保证。

### 修复能力的实际范围

`truck-shapeops::healing` 提供 SplitClosedEdgesAndFaces 及 robust 版本，目的是将外来形状调整到 Truck 的拓扑限制：边两端不能是同一顶点、面边界须为简单 wire。源码说明边界简化目前只实现圆柱，球等奇异曲面还未实现。robust 版增加近点参数搜索回退，不等于通用几何修复。不能把它当作已验证的缝合、容差协调、小边修复、缺面补洞或球极点修复系统。[修复模块及限制](https://github.com/ricosjp/truck/blob/88ed005249e5e3a6b07f62425399435905cd3ab6/truck-shapeops/src/healing/mod.rs#L22-L113)

## 对当前项目的落地建议

1. 前端候选：评估 ruststep AST/实体引用能否满足字符编码、复合实体、多 DATA、坏引用等需求；先作为可替换解析组件，而不是绑定整个 Truck 几何内核。
2. 中立层：保持 STEP 实体 ID、单位/角单位、placement、完整曲线/曲面定义、edge-use/pcurve、same_sense/orientation、源容差信息。每个损失或不支持实体结构化报告。
3. SMLib 接入：从中立层构建精确几何和拓扑；显式记录参数域换算和周期分支，避免通过网格或 USD 中转。
4. 修复独立阶段：先审计再修，修复前后分别报告问题；优先处理方向、端点与曲线一致性、pcurve/3D 偏差、容差和缝合。闭合边拆分是否必要由 SMLib 拓扑契约决定，不照搬 Truck 限制。
5. 首批验收：平面带孔面、盒、圆柱 seam、标准球极点、圆锥顶点、有理 B 样条、mm/inch/meter 同形件、缺引用与不支持面。按实体计数、曲面/边采样残差、壳闭合、方向和修复前后差异验收；不以“显示出来”验收。

上面为调查后的实施顺序建议。本轮仅写此调查报告并运行仓库外小探针，没有接入新导入器、修改内核、运行自动修复或改动网格化。
