# 骨骼权重处理契约分析：从模型导入到限权、去骨与网格拆分

> 分析对象：本仓库 Assimp 源码（分支 `task/38`，基线提交 `3f1e307`）
> 方法：全部结论基于对实际源码与现有测试的静态阅读，未运行构建。文中所有结论均标注 `文件:行号` 以便复核。
> 关注问题：`aiProcess_LimitBoneWeights`、`aiProcess_Debone`、`aiProcess_SplitByBoneCount` 三个处理步骤各自接受输入、各自继续处理，但组合之后，输出顶点的蒙皮位置（skinned position）是否仍有明确含义。

---

## 1. 管线顺序与各步骤的处理契约

### 1.1 固定执行顺序（代码证实）

后处理步骤按 `PostStepRegistry.cpp` 中的注册顺序实例化，`Importer.cpp:163-168` 直接按该数组顺序迭代执行，没有任何按依赖关系重排的逻辑：

| 顺序 | 步骤 | 注册位置 |
|---|---|---|
| 较早 | `SplitByBoneCountProcess` | `code/Common/PostStepRegistry.cpp:215` |
| 中间 | `JoinVerticesProcess`（在两者之间） | `code/Common/PostStepRegistry.cpp:241` |
| 较晚 | `DeboneProcess` | `code/Common/PostStepRegistry.cpp:252` |
| 最晚 | `LimitBoneWeightsProcess` | `code/Common/PostStepRegistry.cpp:255` |

即实际顺序为 **SplitByBoneCount → JoinVertices → Debone → LimitBoneWeights**。三个目标步骤中，LBW 最后执行，因此它看到的是前两步改写过的数据；它不会再触发 SBBC/Debone 的任何检查。

另外，`aiProcess_LimitBoneWeights` 被包含在常用预设 `aiProcessPreset_TargetRealtime_Quality` 中（`include/assimp/postprocess.h:671`），而 Debone 与 SBBC 不在任何预设中，必须显式请求。

### 1.2 数据结构层面的公开契约（代码证实）

`include/assimp/mesh.h:222-224` 对权重的文档约定是：

```cpp
//! The strength of the influence in the range (0...1).
//! The influence from all bones at one vertex amounts to 1.
```

即 API 文档声称：单个权重在 (0,1) 区间，**同一顶点所有骨骼权重之和为 1**。但这只是文档约定，库内没有任何非可选环节强制它（见第 3 节）。

### 1.3 三个步骤各自的契约

#### A. LimitBoneWeightsProcess（限权，默认上限 4）

`code/PostProcessing/LimitBoneWeightsProcess.cpp:107-184`：

1. 按顶点收集所有 `(bone, weight)` 条目（`ProcessMesh`，行 117-128）。
2. 只有当某顶点条目数 `> mMaxWeights` 时才动手；整网格最大值不超限时**整体直接返回，不做任何检查或归一化**（行 130-131）。
3. 超限时按权重降序排序（`Weight::operator<` 实为 `mWeight > pWeight.mWeight`，头文件行 128-130），保留前 N 个、截断其余（行 142-147）。
4. 对保留条目重新归一化：`w *= 1/sum`；**仅当 `sum != 0` 时执行**（行 150-159）。
5. 重建每根骨骼的权重数组；默认 `mRemoveEmptyBones=true`，权重数变 0 的骨骼被 `delete`（行 163-179、89-103）。
6. 上限默认值实际为 **4**：头文件 `LimitBoneWeightsProcess.h:63-65` 先定义 `AI_LMW_MAX_WEIGHTS = 0x4`；cpp 行 50-52 的 `#ifndef ... 16` 因宏已存在而永不生效。属性 `AI_CONFIG_PP_LBW_MAX_WEIGHTS` 可覆盖（行 84）。

关键边界行为：

- 越界 `mVertexId`：静默跳过（行 122-123，`if (vertexWeights.size() <= vw.mVertexId) continue;`），**无日志**。
- 同一骨骼对同一顶点的重复条目：**不去重、不警告**，作为两个独立条目计数，会挤占限额、参与排序与归一化。
- 权重 0 的条目：正常收集，参与条目计数（可能把有效权重挤出限额）；若截断后剩余全 0，则跳过归一化，0 被原样保留。
- 未加权顶点：在 `vertexWeights` 中是空数组，全程不被触碰，输出仍无任何权重。
- 截断是有信息损失的：被丢弃的是影响最小的骨骼，重归一化只保证**保留项**之和为 1，不保留原始混合结果。

#### B. DeboneProcess（去骨，默认阈值 1.0）

`code/PostProcessing/DeboneProcess.cpp`，配置见行 64-68，默认 `mThreshold = AI_DEBONE_THRESHOLD = 1.0f`（`DeboneProcess.h:59-61`）。

契约（`ConsiderMesh` 行 155-230 与 `SplitMesh` 行 234-348 各实现一遍，逻辑重复）：

1. 权重 `w == 0.0f` 的条目直接 `continue`（行 174-176、247-249）。
2. 顶点归属判定：`w >= mThreshold`（默认即 `w >= 1.0`）时把顶点记为该骨骼所有；若该顶点已被另一根骨骼声称，则标记为 `cCoowned`（行 179-191、253-265）。
3. **同一骨骼重复条目**：仅 `ASSIMP_LOG_WARN("Encountered double entry in bone weights")`（行 183、257），不去重、不拒绝，继续处理。
4. 若某骨骼对其所有顶点都满足 `w >= 阈值` 且这些面不与其他骨骼的面交错，则该骨骼可移除：相关面被切到一个 `AI_SUBMESH_FLAGS_SANS_BONES` 子网格，并对顶点位置直接施加该骨骼的 `mOffsetMatrix`（行 327-346、`ApplyTransform` 行 407-436）。
5. 不属于任何单骨的面进入保留骨骼的基础子网格（行 312-325）。
6. `mAllOrNone` 为真时要求所有骨骼都可移除才执行（行 86）。

去骨的"无损"前提是：被去骨顶点在**每一帧**都 100% 跟随该骨骼。由于判定只看静态权重 `w >= 1.0`，权重严格为 1 的单骨顶点在数学上确实等价；但浮点误差、重复条目、负权重会破坏这个前提（详见第 4 节）。

#### C. SplitByBoneCountProcess（按骨骼数拆网格，默认上限 60）

`code/PostProcessing/SplitByBoneCountProcess.cpp`，默认 `AI_SBBC_DEFAULT_MAX_BONES = 60`（`include/assimp/config.h.in:109-110`）。

契约：

1. 网格级触发：仅当 `mNumBones > mMaxBoneCount` 才拆分（行 80-90、138-140）。
2. 构建逐顶点骨骼列表时**只收 `mWeight > 0.0f` 的条目**（行 149-156），零权重被忽略；不检查 `vertexId` 上界即直接索引 `vertexBones[vertexId]`（行 150-151）。
3. **硬失败**：若某顶点的正权重骨骼数超过上限，立即 `throw DeadlyImportError("SplitByBoneCountProcess: Single face requires more bones than specified max bone count!")`（行 152-154）。这是三个步骤中唯一以异常拒绝输入的地方。
4. 贪心按面打包子网格，使每个子网格引用的不同骨骼数不超过上限（行 161-213）；顶点被复制到各子网格，权重重映射但**数值原样复制、不归一化**（行 332-348）。
5. 子网格内每个顶点的全部正权重骨骼都必须在该子网格中，否则触发 `ai_assert`（行 341）。

### 1.5 旁证：glTF2 导出器存在第二套截断契约

`code/AssetLib/glTF2/glTF2Exporter.cpp:1106-1136`：导出 glTF 时默认把每顶点关节数截到 4（`maxJointsPerVertex = 4`），按权重降序排序后丢弃超出部分，**截断后不做归一化**，空缺补 0。这与 LBW"截断后重归一化"的契约不一致，且同样不产生警告。`AI_CONFIG_EXPORT_GLTF_UNLIMITED_SKINNING_BONES_PER_VERTEX` 可关闭截断（行 1365-1367）。

---

## 2. 三类异常输入在各层的处置矩阵

| 异常输入 | 导入层（glTF2 / Collada / FBX / X） | FindInvalidData | ValidateDataStructure（可选） | SBBC | Debone | LBW |
|---|---|---|---|---|---|---|
| **零总权重顶点**（所有条目权重为 0） | glTF2、Collada 在导入时过滤 `weight > 0`，等效为"未加权顶点"；FBX 原样保留 0 条目（`FbxConverter.cpp:1740-1743`） | 不检查骨骼 | 不报错：`afSum[i]` 为 0 时条件 `afSum[i] && ...` 为假，静默通过（`ValidateDataStructure.cpp:443-455`） | 0 条目被忽略，顶点按无权重处理 | 0 条目 `continue`，顶点保持 `cUnowned` | 不截断则原样保留；截断后 sum=0 时跳过归一化，0 原样输出 |
| **未加权顶点**（无任何条目） | 自然产生（静态网格部分） | 不检查 | 同上，静默通过 | 进入任意子网格，无权重 | 进入无主面/基础子网格 | 空数组，不触碰 |
| **同骨重复条目** | X 导入器按顶点数组赋值，天然去重（`XFileImporter.cpp:365-376`）；glTF2/Collada/FBX 不去重 | 不检查 | **检测不到**：逐骨骼逐条累加，重复条目被合法累加，不报错 | 作为两个正权重条目计数，可能直接触发超限异常 | 仅 `LOG_WARN` 后继续；重复声称会污染 `cCoowned`/归属判定 | 无警告、不去重，两条目分别参与限额与归一化 |
| 越界 `mVertexId` | X 导入器跳过（行 372-375） | 不检查 | **Error → 抛异常**（`ValidateDataStructure.cpp:447-448`），但仅在显式开启该 flag 时 | 不检查上界，直接数组索引，存在越界访问风险（行 150-151） | 读权重时不检查上界 | 静默跳过（cpp 行 122-123） |
| 单权重 ≤0 或 >1 | glTF2/Collada 过滤 ≤0；FBX 保留 | 不检查 | Warning（`ValidateDataStructure.cpp:449-451`），注意格式串用 `%i` 打印 float，日志数值不可读 | 仅 >0 才收录 | ==0 跳过，负值参与归属逻辑 | 全保留 |
| 顶点权重和不在 [0.94, 1.05] | 不强制 | 不检查 | Warning（`ValidateDataStructure.cpp:443-445`），不拒绝 | 不检查 | 不检查 | 仅在被截断的顶点上重归一化；未截断顶点保持原和 |

要点：

- **拒绝输入（硬失败）只发生在两处**，且都是可选路径：SBBC 的每顶点骨骼数超限（`DeadlyImportError`），以及 ValidateDataStructure 的越界索引/重复骨名（`ReportError` 抛 `DeadlyImportError`，`ValidateDataStructure.cpp:69-81`）。
- **提示后继续**只有 Debone 的 "double entry" 警告和 ValidateDS 的各类 Warning；后者需要调用方显式加 `aiProcess_ValidateDataStructure`（仅在 `..._MaxQuality` 预设中，`postprocess.h:696-701`）。
- **默认路径（含 TargetRealtime_Quality 预设）下，三类异常没有任何一层拒绝，也几乎没有提示**：LBW 对重复条目和零权重静默处理，FindInvalidDataProcess 的 `ProcessMesh` 完全不涉及骨骼（`FindInvalidDataProcess.cpp:323-419`）。

### 导入层差异（代码证实）

- glTF2：`if (weight > 0 && bone < map.size())` 才入表，零权重与越界 joint 同时被过滤（`glTF2Importer.cpp:1234-1237`）；不做归一化，不归并重复 joint。
- Collada：`if (weight > 0.0f)` 过滤（`ColladaLoader.cpp:793-798`），空骨骼按 `AI_CONFIG_IMPORT_REMOVE_EMPTY_BONES` 删除（行 805-818）；不归一化。
- FBX：权重逐字复制，含 0 权重，无过滤、无归一化（`FbxConverter.cpp:1729-1745`）。
- JoinVertices（后处理，位于 SBBC 与 Debone 之间）：合并重复顶点时，被合并顶点的权重条目被直接丢弃，只保留代表顶点的条目，**不合并、不补偿**（`JoinVerticesProcess.cpp:333-362`）。

---

## 3. 截断后归一化改变了什么

以 5 骨骼顶点、原始权重 `(0.40, 0.30, 0.20, 0.07, 0.03)`、和为 1.0、LBW 上限 4 为例：

1. 截断丢掉 0.03，保留和 = 0.97。
2. 重归一化后变为 `(0.4124, 0.3093, 0.2062, 0.0722)`，和恢复为 1。

数学含义的变化：

- 蒙皮公式为 `p' = Σ_k w_k · M_k · p`（`mOffsetMatrix` 的用途见 `mesh.h:296-308`）。截断丢掉的是被剔除骨骼贡献的仿射项；重归一化只是对**剩余项整体乘以 `1/0.97`**，并不能恢复被丢弃的 `M_5·p` 项。
- 因此重归一化保证的是"剩余权重仍构成凸组合（系数和为 1、平移语义成立）"，改变的是剩余骨骼之间的**相对配比**——每根保留骨骼的影响被等比放大。若保留骨骼之间姿态差异大，放大后的混合点与原始混合点不再重合。
- 若原始权重和本就不是 1（例如和为 0.8 的部分蒙皮、或含负权重的 DQ 蒙皮数据），重归一化会把它强行拉到 1，**改变了作者有意写入的总增益**。
- `sum == 0` 时跳过重归一化（cpp 行 154）：全零顶点保持全零。消费端若按"权重和为 1"假设实现，零和顶点的结果是未定义的（常见实现会退化为原点或零向量），而库内没有任何标记说明该顶点是"故意静态"还是"数据损坏"。
- 对照 glTF2 导出器的截断（不重归一化）：同一份数据经过 LBW 与经过导出器截断会得到两套不同权重，库内同时存在两种互不兼容的截断契约（第 1.5 节）。

---

## 4. 组合后姿态为何可能不再等价，以及责任边界

### 4.1 已由代码证实的不等价路径

1. **限权本身的信息损失**。LBW 丢弃低影响骨骼后，无论是否重归一化，`Σ w_k M_k p` 都与原式不同。这是设计内取舍，但库只在非空 logger 下以 INFO 记录被删权重总数（cpp 行 181-183），不定位到顶点/骨骼，默认 NullLogger 下完全不可见。
2. **重复条目被静默放大**。若同一骨骼对同一顶点有两条 `(B, 0.3)`：
   - LBW 把它们算作两个名额。顶点原本 3 骨骼即可表达，重复后变 4 个条目，可能把另一条有效骨骼挤出限额；保留的两条 0.3 经归一化后该骨骼实际影响翻倍。姿态直接改变，且全程无警告。
   - Debone 中重复声称只产生一条 WARN，顶点归属可能被错误标记为 `cCoowned` 而保留骨骼（该去的没去），或在阈值临界时被错误去骨（不该去的去了）。
3. **Debone 把偏移矩阵烘焙进静态顶点**。去骨子网格的顶点位置被 `mOffsetMatrix` 改写（`DeboneProcess.cpp:342`、`407-436`），骨骼被删除。这一步对 `w==1` 的单骨顶点在绑定姿态及任意动画帧都等价；但对重复条目污染、或阈值被调低（`AI_CONFIG_PP_DB_THRESHOLD < 1.0`）后选中的顶点，烘焙位置只在绑定姿态等价，动画过程中与真实蒙皮发散，而且骨骼已被删除，消费端无法恢复。
4. **JoinVertices 丢权重**。被合并顶点若带有与代表顶点不同的权重集，其条目被静默删除（`JoinVerticesProcess.cpp:343-348`），合并后的顶点蒙皮姿态只反映代表顶点。该步骤在三个目标步骤之间执行，损失会传入 Debone/LBW。
5. **零和顶点语义悬空**。FBX 路径可产出全 0 权重顶点；ValidateDS 对和为 0 不警告（`afSum[i] &&` 短路）；LBW 不修复。输出网格里该顶点与"静态未加权顶点"在数据结构上无法区分，但前者显式出现在骨骼权重数组中、后者完全不出现，消费端难以区分处理。
6. **两套截断契约并存**。LBW 截断+重归一化 vs glTF2 导出器截断+补零不重归一化，同一资产往返导入/导出后权重和可能从 1 变为 <1，且无任何信号。
7. **SBBC 的失败依赖输入顺序**。它在 LBW 之前运行：若顶点正权重骨骼数 >60（默认）直接抛异常终止整个导入；但若同时开启 LBW（上限 4），本可在限权后满足约束，却因为管线顺序先抛异常。调用方无法通过 flag 组合表达"先限权再拆分"的意图。
8. **越界索引在 SBBC 中无防护**。`vertexBones[vertexId]` 不做上界检查（`SplitByBoneCountProcess.cpp:150-151`），而 LBW 对同一类数据是静默跳过——同一畸形输入在两个步骤中一个可能内存越界、一个静默吞掉，行为不一致。

### 4.2 责任边界

| 层 | 应承担的责任 | 实际边界（代码证实） |
|---|---|---|
| 导入器 | 格式语法校验、本格式表示到 aiMesh 的忠实转换 | glTF2/Collada 主动过滤零权重（格式约定如此）；FBX 忠实保留；均不做跨格式语义归一化，重复条目不归并 |
| FindInvalidData | 清理无效数值 | 明确不覆盖骨骼/权重（`FindInvalidDataProcess.cpp:323-419`） |
| ValidateDataStructure | 结构与语义不变量校验 | 覆盖越界索引（Error）、权重和（Warning，阈值 0.94/1.05，零和豁免）、单值范围（Warning）；**检测不到重复条目**；默认不启用，Warning 不阻断 |
| SBBC | 网格级骨骼数约束 | 只保证输出子网格骨骼数 ≤ 上限；以异常拒绝过约束顶点，不做权重修复；不防越界 |
| Debone | 在严格单骨前提下用节点动画替代顶点蒙皮 | 阈值判定是唯一正确性守卫；重复条目仅警告；去骨后信息不可逆 |
| LBW | 每顶点影响骨骼数约束 | 只对超限顶点负责，重归一化仅保证保留项之和为 1；不校验输入不变量、不检测重复、不报告零和 |
| 全局 | 端到端姿态等价性 | **没有任何一层对此负责**。步骤间通过裸 `aiMesh` 传递，没有"输入契约满足"的前置断言，也没有步骤产出不变量的后置校验 |

根因：三个步骤都以"输入大致合法"为隐含前提，而唯一能系统校验前提的 ValidateDataStructure 是可选的、且对重复条目与零和这两类关键异常没有覆盖。于是每一步单独看都"成功完成"，组合输出却可能不满足 `mesh.h:223` 声明的"权重和为 1"，蒙皮位置的含义从"凸组合混合"退化为"依赖消费端实现的未定义行为"。

---

## 5. 修订建议：兼顾旧模型结果与异常可见性

原则：**默认路径保持既有数值结果不变（兼容），新增可见性与显式 opt-in 的严格处理；把"修复"与"拒绝"做成可配置策略，而不是静默改变老资产的输出。**

### 5.1 可保持兼容、不应改变结果的部分

1. 合法输入（无重复、每顶点权重和在容差内、条目数 ≤ 各上限）经过三步组合的现有输出数值保持不变——现有 `utLimitBoneWeights.cpp` 与 `utglTF2ImportExport.cpp:636-771` 的断言应继续通过。
2. glTF2/Collada 导入时过滤零权重、X 导入器的按顶点去重，属于格式既有行为，保留。
3. Debone 对严格 `w == 1.0` 单骨顶点的烘焙去骨，数学上已证等价，保留默认路径。
4. LBW 对超限顶点"降序截断 + 重归一化"的现行数值结果保留为默认策略。
5. 未加权顶点（权重数组中完全不出现）维持现状：按静态顶点处理，不制造人工权重。

### 5.2 需要明确改变处理方式的输入（建议新增策略，默认仅告警）

1. **同骨重复条目**
   - 新增统一前置清理（建议放在 LBW 内或独立小步骤，默认开启）：按 `(vertexId, boneIndex)` 合并，权重相加；合并前后条目数不一致时发 `ASSIMP_LOG_WARN`，并在 `aiMetadata` 记录受影响顶点计数。
   - 提供严格属性（如 `AI_CONFIG_PP_STRICT_WEIGHT_CONTRACTS`）：开启后遇到重复条目直接报告错误，供 CI/资产审核使用。
   - 同步修复 Debone 两处重复逻辑（`DeboneProcess.cpp:171-201` 与 `243-271`）复用同一清理结果，消除双份实现漂移。
2. **零总权重顶点**
   - 区分两种情况并显式化：完全无条目的顶点维持静态语义；有条目但和为 0 的顶点，默认发一次聚合 WARNING（定位到 mesh/vertex 计数），严格模式下 Error。
   - 修复 ValidateDS 的零和豁免（`ValidateDataStructure.cpp:443-445` 的 `afSum[i] &&` 短路），至少 Warning；修正行 450 用 `%i` 打印 float 的格式串错误。
3. **LBW 的静默跳过与零和不归一化**
   - 越界 `mVertexId` 跳过处（cpp 行 122-123）增加 WARNING 计数；严格模式下交由 ValidateDS 拒绝。
   - `sum == 0` 跳过重归一化时增加 DEBUG/WARN 记录，避免输出与文档契约（和为 1）冲突却无任何信号。
4. **SBBC 的顺序与健壮性**
   - 在 `SplitByBoneCountProcess.cpp:150` 增加 `vertexId` 上界检查，与 LBW 行为对齐（跳过+警告，严格模式 Error），消除越界访问风险。
   - 文档与属性层面允许表达"LBW 先于 SBBC 生效"：要么在 SBBC 异常信息中提示同时启用 LBW，要么在检测到两个 flag 同时开启时内部先执行限权再拆分（可通过步骤依赖声明实现，而非重排全局注册表）。
5. **glTF2 导出器截断契约**
   - 截断发生时输出 WARNING 并给出受影响顶点数；提供属性使导出器复用 LBW 的"截断+重归一化"策略，或在文档中明确声明导出截断不保证权重和为 1。
6. **JoinVertices 丢权重**
   - 合并顶点时若两边权重集不一致，发 WARNING；严格模式下改为按骨骼合并权重（相加后归一化）而非丢弃。

### 5.3 建议新增的测试（当前证据缺口，见第 7 节）

- `utDeboneProcess.cpp`、`utSplitByBoneCountProcess.cpp`：当前仓库不存在任何针对这两个步骤的单元测试（`test/CMakeLists.txt:220` 仅编入 `utLimitBoneWeights.cpp`）。
- 在 `utLimitBoneWeights.cpp` 增补：重复条目、全零顶点、sum=0 截断、越界索引、未加权顶点混合用例。
- 端到端组合测试：同一手工构造场景分别跑 LBW、Debone、SBBC 及三者组合，断言蒙皮参考实现（按 `mesh.h:296-308` 的公式逐顶点计算）的位置偏差边界。

---

## 6. 姿态与权重对应关系的验收判据

以下判据建议同时作为单元测试断言与严格模式的运行时检查项。设顶点 v 的输入权重对为 `{(b_k, w_k)}`，输出为 `{(b'_j, w'_j)}`，蒙皮参考函数 `S(v) = Σ w_k · M_{b_k} · p_v`（绑定姿态与至少一组非平凡测试姿态各算一次）。

### 6.1 结构不变量（所有模式都必须满足）

1. 每个输出 `aiVertexWeight.mVertexId < aiMesh::mNumVertices`。
2. 同一 mesh 内每根骨骼名唯一；每个 `(vertex, bone)` 对在输出中至多出现一次（重复条目要么被合并、要么被拒绝）。
3. 输出权重满足 `0 < w'_j ≤ 1`（零权重条目不应出现在输出数组中）。
4. 输出每个顶点的条目数 ≤ 生效上限（LBW 的 `AI_CONFIG_PP_LBW_MAX_WEIGHTS`；导出器为 4）。
5. 每个输出子网格的不同骨骼数 ≤ `AI_CONFIG_PP_SBBC_MAX_BONES`；子网格内顶点引用的骨骼全部存在于该子网格（现靠 `ai_assert` 保证，应转为公开测试断言）。
6. 被 Debone 移除的骨骼不得再出现在任何输出子网格的 `mBones` 中；烘焙子网格带 `AI_SUBMESH_FLAGS_SANS_BONES` 且无骨骼数组。

### 6.2 权重和与姿态等价判据

7. 对所有在输入中至少有一个正权重的顶点：输出权重和 ∈ [1−ε, 1+ε]，建议 ε=0.01（比 ValidateDS 现行 0.06/0.05 容差更严，`ValidateDataStructure.cpp:444`）；全零输入顶点单独归类并要求显式告警，不适用本判据。
8. **未触发任何截断/去骨的顶点**：输出权重集合与输入完全相等，`||S_out(v) − S_in(v)|| = 0`（浮点误差内）。
9. **仅经 LBW 截断的顶点**：保留的骨骼必须是输入中权重最大的 N 根（并列时定义稳定 tie-break 并写入文档）；`||S_out(v) − S_in(v)|| ≤ δ_trunc`，δ 取被丢弃权重和乘以测试姿态最大骨骼位移的界，即 `δ_trunc = (1 − Σ保留原始w) · max_k ||M_k p − p||`，测试中给出实测值并要求单调不增。
10. **经 Debone 的顶点**：绑定姿态下 `||p_baked − S_in(v)|| < 1e-5`；且仅当输入为严格单骨（唯一正权重骨骼且 w=1.0、无重复条目）时允许去骨，否则必须保留在骨骼子网格中。
11. **经 SBBC 的顶点**：拆分不允许改变任何权重数值（现行实现是原样复制，判据为逐位相等）；同一源顶点在各子网格中的权重并集等于其输入正权重集合。
12. **组合步骤**：在一组非平凡测试姿态下，全网格 `max_v ||S_out(v) − S_in(v)||` 必须有上界且该上界仅由第 9 条的截断误差贡献；Debone/JoinVertices/SBBC 单独贡献的误差为 0（否则视为缺陷）。
13. **可见性判据**：每次发生截断、重复合并、零和跳过、越界跳过、导出截断时，日志中必须有包含 mesh 索引与受影响顶点计数的记录；严格模式下第 2、7、10 条的违例使导入失败并返回错误字符串。

---

## 7. 结论：代码证实部分与证据缺口

### 7.1 代码证实的部分

1. 管线固定顺序为 SplitByBoneCount →（JoinVertices）→ Debone → LimitBoneWeights，无依赖重排（`PostStepRegistry.cpp:215-256`、`Importer.cpp:163-168`）。
2. LBW 默认上限 4（头文件宏生效，cpp 中的 16 是死代码）；仅对超限顶点截断并按保留项重归一化，sum=0 时不归一化；越界索引静默跳过；不去重、不警告重复条目（`LimitBoneWeightsProcess.cpp:107-184`）。
3. Debone 是唯一对重复条目发警告的步骤，但仅警告不去重；去骨通过烘焙 `mOffsetMatrix` 实现，正确性依赖 `w >= 阈值（默认1.0）` 的静态判定（`DeboneProcess.cpp:171-201、243-346`）。
4. SBBC 忽略零权重、原样复制权重不归一化，且对每顶点超骨骼数上限直接抛 `DeadlyImportError`，对 `vertexId` 不做上界检查（`SplitByBoneCountProcess.cpp:146-157`）。
5. 导入层行为分裂：glTF2/Collada 过滤零权重，FBX 保留零权重；除 X 导入器外均不去重；各导入器均不做权重归一化（`glTF2Importer.cpp:1234`、`ColladaLoader.cpp:793`、`FbxConverter.cpp:1740-1743`、`XFileImporter.cpp:365-376`）。
6. FindInvalidDataProcess 不校验任何骨骼数据；ValidateDataStructure 是可选步骤，能报越界索引（Error）与权重和偏离（Warning，零和豁免、容差 0.94/1.05），但结构上无法识别同骨重复条目（`ValidateDataStructure.cpp:389-455`）。
7. JoinVertices 静默丢弃被合并顶点的权重（`JoinVerticesProcess.cpp:333-362`）；glTF2 导出器存在第二套"截断不归一化"契约（`glTF2Exporter.cpp:1106-1136`）。
8. 现有测试只覆盖 LBW 的单一场景（500 顶点、每顶点 2 骨骼、均匀权重，`utLimitBoneWeights.cpp:100-136`）和 glTF2 皮肤往返（`utglTF2ImportExport.cpp:636-771`）；Debone 与 SBBC 没有任何单元测试，三类异常输入（零和、未加权、重复条目）没有任何测试用例。

**总判断**：在默认管线中，每个步骤对合法输入都能保持姿态的明确定义；但对零总权重、同骨重复条目这类异常输入，没有任何一层拒绝，提示也仅在可选路径中存在。组合之后，`mesh.h:223` 声明的"权重和为 1"契约可能被破坏而无信号，顶点蒙皮位置是否仍等于作者意图的混合结果，在库内得不到验证或保证——含义仍然可以由消费端按数据本身计算出来，但不再有库级别的正确性承诺。

### 7.2 证据缺口（本次静态分析未能证实、需要动态验证或进一步调查的部分）

1. 未执行编译与测试运行，所有行为结论来自源码阅读；编译器宏被外部覆盖（如编译期 `-DAI_LMW_MAX_WEIGHTS=...`）时默认上限可能不同，实际发行配置未核实。
2. SBBC 行 150-151 的越界索引是否可被真实畸形文件利用，取决于上游导入器是否已保证 `vertexId` 合法；本次未逐一审计全部 40+ 导入器，仅核实了 glTF2/Collada/FBX/X 四个代表。
3. Debone 在 `w == 1.0` 临界浮点情形（例如 0.99999994 与 1.0000001）下的实际分类结果，需要构造数值实验确认；`>=` 比较对 NaN 权重的行为也未验证。
4. LBW 排序在权重完全相等时的 tie-break 顺序（`std::sort` 非稳定排序）对"保留哪几根骨骼"的实际影响未做动态测量。
5. 重复条目在真实资产中的出现频率与来源格式（疑似 FBX 簇重复或手工 glTF JOINTS_0/JOINTS_1 重叠）没有样本数据支撑，本报告不评估其流行率。
6. 第 6 节给出的姿态误差界 δ_trunc 是理论界；各消费引擎对零和顶点、权重和 ≠1 的实际处理（退化原点、归一化、clamp）未纳入调查。
7. ValidateDS 的 Warning 是否会设置 `AI_SCENE_FLAGS_VALIDATION_WARNING` 并被主流调用方检查，未追踪调用方生态，仅确认库内机制存在。
