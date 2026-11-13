# 剧本接口

剧本是 `Scripts/**/*.script.json` 下的 JSON 文件。最外层类型是 `ScriptFile`，`actions` 定义可执行入口，每个入口的 `run` 数组直接列出流程。

## 类型与名称

| 写法 | 含义 |
|---|---|
| `string` | 文本 |
| `name` | 非空名称 |
| `number` | 有限数值 |
| `int` | 整数 |
| `bool` | `true` 或 `false` |
| `value` | 布尔值、数值或文本 |
| `vec3` | `[x, y, z]` |
| `quat` | `[x, y, z, w]` 四元数 |
| `T[]` | T 的数组 |
| `map<T>` | 名称到 T 的字典 |

字段后的 `?` 表示可省略，`= 值` 表示默认值，`—` 表示没有隐含值。`null` 仅用于明确列出它的字段。

| 名称 | 含义 |
|---|---|
| `Caster` | 当前剧本的演员 |
| `body` | 当前演员的动画参与者 |
| `partner:KEY` | `resources.partners` 中 KEY 对应的伙伴 |
| `as` | 一次操作的请求名，供等待步骤引用；省略时自动命名 |
| 特效 key | `resources.effects` 中的定义名，供创建和控制指令引用 |
| 流程名 | `functions` 中的 key |
| 动作名 | `actions` 中的 key |
| 状态名 | `states` 中的 key |

## 文件结构

### 根对象：`ScriptFile`

```json
{
  "format": "hksr.script",
  "id": "demo",
  "applies_to": "Config/ConfigCharacter/Avatar/Avatar_Huohuo_00_Config.json",
  "actions": {
    "attack": {
      "run": [
        {"op":"animation", "state":"Skill01", "as":"body"},
        {"op":"wait", "for":"body"}
      ],
      "next": "idle"
    },
    "idle": {"run":[{"op":"animation", "state":"StandBy"}]}
  }
}
```

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `format` | `"hksr.script"` | 必填 | 文件类型 |
| `id` | `name` | 必填 | 剧本标识 |
| `applies_to` | `name` | 必填 | 适用的角色配置路径 |
| `actions` | `map<动作>` | 必填 | 可执行入口 |
| `functions?` | `map<步骤[]>` | `{}` | 复用流程 |
| `states?` | `map<状态>` | `{}` | 持续状态及其资源生命周期 |
| `initial_states?` | `name[]` | `[]` | 初始化时进入的纯逻辑状态 |
| `resources?` | 资源表 | `{}` | 特效、伙伴和原生配置引用 |
| `default?` | `bool` | — | 是否作为该来源的默认剧本 |
| `defaults?` | 对象 | `{}` | 目标别名、选项和空间点默认值 |
| `scope?` | `name` | `id` | 原生事件绑定作用域 |
| `imports?` | `name[]` | `[]` | 导入定义模块，路径相对于 Scripts 目录 |
| `templates?` | `map<动作>` | `{}` | 可引用的动作模板 |
| `restore?` | `视觉指令[]` | — | 场景资源恢复配方 |
| `signals?` | 信号字典 | `{}` | 可显式触发的指令组 |

每个来源只有一份剧本时自动选择，`default:false` 除外；有多份候选时指定唯一的 `default:true`，或全部标记为 false，仅显式选择。运行时递归发现匹配的文件。

`defaults`：`targets?: map<{alias:name}|{local_position:vec3}>`、`choices?: map<string>`、`anchors?: map<空间点>`。空间点包含 `position:vec3`，可选 `rotation:quat=[0,0,0,1]`。具体角色实例由调用方绑定。

### 资源表：`resources`

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `effects?` | `map<特效定义>` | `{}` | 具名特效 |
| `partners?` | `map<伙伴定义>` | `{}` | 具名伙伴 |
| `phases?` | `map<配置引用>` | `{}` | 具名阶段定义 |
| `projectiles?` | `map<配置引用>` | `{}` | 具名弹体参数 |

资源表复用的是声明。特效在创建时、伙伴在 `partner.create` 时加载，弹体配置在首次使用时读取。

### 公共定义模块

模块是普通JSON文件，例如 `Scripts/Common/Effects.json`，支持字段为 `imports`、`templates`、`functions`、`states`、`resources`、`defaults`。

```json
{
  "resources": {
    "effects": {"common.glow":{"path":"Effects/Glow.prefab","target":"Caster"}}
  },
  "templates": {
    "common.flash": {"run":[{"op":"effect.create","effects":["common.glow"]}]}
  }
}
```

角色剧本通过 `imports:["Common/Effects.json"]` 导入，动作使用 `{"use":"common.flash"}` 引用模板。引用动作可以覆盖模板字段：inputs 按参数名和参数字段合并，presentation／bindings 按字段合并，其余字段整体替换。

多个模块中同名但不同的定义会报冲突；角色本地声明优先于导入定义。`states` 的本地声明按状态字段覆盖。各角色分别持有运行对象和资源生命周期。

## 资源定义

### 特效：`effects`

```json
{"glow":{"path":"Effects/Glow.prefab","target":"Caster"}}
```

`glow` 是后续指令引用的名称。

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `path` | `name` | 必填 | 特效资源路径 |
| `target` / `anchor` | `name` | 二选一 | 角色目标 / 具名世界空间点 |
| `position_offset?` | `vec3` | `[0,0,0]` | 相对目标的位置偏移，适用于 target |
| `rotation_offset?` | `vec3` | `[0,0,0]` | 相对目标的欧拉角偏移，适用于 target |
| `attach_point?` | `name` | — | 初始挂点，适用于 target |
| `attach_to_target?` | `bool` | `false` | 持续挂在目标上，适用于 target |
| `scale?` | `vec3` | — | 指定缩放 |
| `unique_name?` | `name` | — | 传给游戏的特效名称 |
| `fade_in?` | `bool` | `true` | 启用淡入 |
| `simulate_immediately?` | `bool` | `false` | 创建时立即模拟 |
| `resident?` | `bool` | `false` | 设置常驻标记 |
| `enable_replay?` | `bool` | `false` | 启用重播 |
| `allow_position?` | `bool` | `false` | 接受位置目标；与 attach_to_target=false 搭配 |
| `mutex?` | 对象 | — | 创建间隔和数量规则 |

`mutex` 字段：`interval: number`、`maximum: int`、`scope: "global" | "entity"`。

### 伙伴：`partners`

每个伙伴包含三个必填字段：`model: name` 模型路径、`config: name` 配置路径、`member: name` 成员名。成员名在资源集内唯一。

### 配置引用：`phases`、`projectiles`

| 字段 | 类型 | 作用 |
|---|---|---|
| `root.kind` | `"ability"`、`"global_modifier"`、`"global_template"` | 根定义类别 |
| `root.name` | `name` | 根定义名称 |
| `path` | 路径步骤数组 | 从根定义定位参数 |

路径步骤包含 `owner: name`、`field: name`；可选 `index: int` 或 `key: name` 用于选择数组项或字典项。

## 动作与输入

### 动作：`actions`

```json
{"attack":{"run":[{"op":"animation","state":"Skill01","wait":"performance"}],"next":"idle"}}
```

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `run?` | `步骤[]` | — | 顺序执行的正文；省略时为尚未实现的入口 |
| `use?` | `name` | — | 引用 templates 中的动作模板 |
| `on_interrupt?` | `步骤[]` | — | 可运行中断收尾时执行的正文 |
| `next?` | `name` 或对象 | — | 正常完成后的动作；对象含 action 和可选 inputs |
| `mode?` | `"replace"` 或 `"input"` | `"replace"` | 替换当前动作 / 作为独立输入执行 |
| `trigger?` | `"user"`、`"internal"`、`"event"` | `"user"` | 任务入口类别 |
| `inputs?` | `map<输入定义>` | `{}` | 可编辑的参数 |
| `requires?` | `条件[]` | `[]` | 执行条件，全部满足时可执行 |
| `presentation?` | 展示定义 | — | 名称、类别及设置项关联 |
| `bindings?` | 对象 | — | targets、anchors 名称数组及 choices 选项字典 |
| `on?` | 对象 | — | 任务事件对应的状态写入 |
| `runtime?` | 对象 | — | 运行阶段跟踪策略及关联的技能选择 |
| `tags?` | `name[]` | `[]` | 分类标签 |

`next`：动作名，或 `{"action":"动作名","inputs":{...}}`。

`runtime`：`policy?:ready/performance/continuous/setting=performance`，可选 `selected:string`。ready 在受理时就绪；performance 经受理、实际开始到完成；continuous 完成正文时保持当前阶段；setting 仅更新受理选择与失败阶段。分支通过 `playback` 条件读取运行信息。

`presentation`：

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `label?` | `name` | — | 显示名称 |
| `kind?` | `"action"`、`"setting"`、`"hit_response"` | `"action"` | 展示类别 |
| `listed?` | `bool` | `true` | 显示在正常任务列表 |
| `contexts?` | `name[]` | `[]` | 设置项对应的状态名 |
| `ready?` | 对象 | — | 准备态关联：config、skill、variant?=-1 |
| `omissions?` | `name[]` | `[]` | 该任务的表现缺口说明 |

`setting`、`hit_response` 使用 `mode: "input"`，并指定 `contexts`。

`on` 支持 `accepted`、`started`、`completed`、`interrupted`、`replaced`、`failed`。每项是 `{"context":"状态名","values":{"变量名":值}}` 的数组。

### 输入定义：`inputs`

```json
{"charged":{"type":"bool","default":false,"label":"强化"}}
```

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `type` | `"bool"`、`"integer"`、`"number"`、`"text"` | 必填 | 参数类型 |
| `default?` | 与 type 对应的值 | — | 参数默认值 |
| `label?` | `name` | — | 显示名称 |
| `description?` | `name` | — | 参数说明 |
| `choices?` | 对象数组 | — | 选项，每项含 value、label 和 diagnostic?=false |
| `diagnostic?` | `bool` | `false` | 标记为诊断输入 |

### 条件

| `op` | 判断内容 | 参数 |
|---|---|---|
| `value` | 值相等 | `name: name`、`is: value`、`context?: name` |
| `one_of` | 值属于集合 | `name: name`、`values: value[]`、`context?: name` |
| `number` | 数值比较 | `name: name`、`compare: eq/ne/lt/le/gt/ge`、`value: number`、`context?: name` |
| `phase` | 当前阶段相等 | `is: name` |
| `participant` | 动画参与者可用 | `name: name` |
| `target` | 目标可用 | `name: name`、`allow_position?: bool = false` |
| `anchor` | 空间点可用 | `name: name` |
| `choice` | 选项相等 | `name: name`、`is: name` |
| `state` | 状态存在或不存在 | `name: name`、`present?: bool = true` |
| `playback` | 运行阶段或技能选择 | `name:selected/performance`，以及 `is:value` 或 `values:value[]` |
| `unresolved` | 记录未解决的条件 | `source: name`、`detail: name` |

`value`、`one_of`、`number` 指定 context 时读取该状态中的值；其余情况读取任务输入和当前值。

## 流程与编排节点

`run`、`on_interrupt` 和函数正文都是步骤数组。数组从上到下推进；步骤的 `op` 指定操作。`source?:name` 可提供诊断位置，省略时使用当前JSON路径。

| `op` | 作用 | 必填参数 | 可选参数及默认值 |
|---|---|---|---|
| `parallel` | 并行推进各支路并等待流程结束 | `branches: 步骤[][]` | — |
| `call` | 同作用域调用复用流程 | `function: name` | — |
| `if` | 根据条件选择首个匹配分支 | `condition: 条件`、`then: 步骤[]` | `elif?:{condition,then}[]=[]`、`else?:步骤[]=[]` |
| `switch` | 按捕获输入选择首个等值分支 | `name:name`、`cases:分支[]` | `default?:步骤[]=[]` |
| `repeat` | 重复子步骤 | `run: 步骤[]` | `count?: int`、`until_finish?: bool=false`；指定 count 或 until_finish=true |
| `for_each` | 为目标集合各启动一个支路 | `bind:name`、`targets:name[]`、`run:步骤[]` | `collection?:name`、`empty?:步骤[]` |
| `start` | 启动具名并发子流程，继续当前流程 | `id:name`、`run:步骤[]` | `join?:bool=false` |
| `join` | 等待已标记 join 的子流程 | — | — |
| `on_success` | 请求成功完成后执行子步骤 | `for:name`、`run:步骤[]` | — |
| `wait` | 等待请求或进度 | `for:name` | `until?:等待信号=performance` 或 `progress:number` |
| `wait` | 等待秒数 | `seconds:number` | — |
| `wait` | 等待下一帧 | `frame:true` | — |
| `skip` | 保留明确跳过的请求结果 | `as:name`、`reason:name` | `unit?:name`、`dependencies?:name[]` |

提交播放请求与等待播放完成是两步：

```json
[
  {"op":"animation","as":"attack","state":"Skill01"},
  {"op":"wait","for":"attack"}
]
```

提交操作可填 `wait:"performance"` 等待信号，将提交与紧随的等待合写。`as` 仅在后续引用请求时需要填写。

`start` 的 `join:true` 将子流程登记到 `join` 等待集合；同一身份正在运行时复用该执行。`call` 每次在当前作用域中执行所引用的流程。

`repeat` 的 `until_finish:true` 表示收到结束请求后，完成当前轮再退出循环。

`switch` 的每个分支包含 `is:value`、`run:步骤[]`，可选 `label:name`。各分支的 `is` 值唯一，比较对象为本次动作启动时捕获的输入。实时状态条件使用 `if`。

```json
{"op":"switch","name":"event","cases":[
  {"is":1,"label":"更新","run":[{"op":"sound","event":"updated"}]},
  {"is":2,"label":"结束","run":[{"op":"sound","event":"ended"}]}
],"default":[]}
```

### 等待信号：`until` 与提交步骤的 `wait`

| 值 | 等待的结果 |
|---|---|
| `submitted` | 请求已提交 |
| `accepted` | 请求已被接受 |
| `started` | 播放已开始 |
| `animation_end` | 动画达到 end 指定的边界 |
| `animation_finished` | 动画请求收到完成通知 |
| `state_left` | 离开该动画状态 |
| `performance` | 此次表现完成 |
| `drained` | 此次请求的资源回收完成 |
| `progress` | 进度达到 progress 指定的值 |

## 动画、声音、移动与受击

下列步骤均可填写 `as?:name` 命名请求、`wait?:等待信号` 等待该请求。

| `op` | 作用 | 必填参数 | 可选参数及默认值 |
|---|---|---|---|
| `animation` | 播放指定动画状态 | `state: name` | `participant?=body`、`layer?: int=0`、`transition?: number=0`、`start?: number=0`、`end?: number`、`restart?: bool=true`、`current_idle?: bool=false`、`track?: 动画队列` |
| `expression` | 设置表情 | `state: name` | `transition?: number=0.2`、`current_idle?: bool=false` |
| `sound` | 播放声音事件 | `event: name` | `participant?=body` |
| `ready_pose` | 播放技能准备态 | `config: name`、`skill: int` | `variant?: int=-1` |
| `reaction` | 播放视觉反应或恢复 | `config: name`、`flag: int` | `recover?: bool=false`、`occurrence?: default/die/die_hit/die_fly/die_limbo` |
| `hit` | 向目标播放受击及命中特效 | `target: name`、`animation: string` | `direction?: 角度`、`effect?: 命中特效`、`allow_position?: bool=false`、`events?: 事件数组=[]` |
| `animation_parameter` | 设置动画参数 | `participant: name`、`parameter: 动画参数` | `propagate?: bool=false` |
| `movement` | 执行一组移动片段 | `segments: 移动片段[]` | `clock?=animation`、`animation?: name`、`force_start?: bool=false` |
| `projectiles` | 按时间发射弹体 | `shots: 发射项[]` | — |

`animation.participant` 使用 body 或 partner:KEY。`sound` 和 `animation_parameter` 的当前演员使用 body。`current_idle: true` 选择当前反应状态的待机姿态，或正常待机。

### 动画队列：`track`

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `logic` | `int` | 必填 | 动画逻辑标记 |
| `extra?` | `int` | `0` | 附加标记 |
| `release_on?` | `int[]` | `[]` | 遇到哪些附加标记时释放当前保持 |
| `force?` | `bool` | `false` | 立即切到新请求 |
| `previous_end?` | `number` | — | 前一请求的交接进度 |

使用 track 时同时填写 animation.end。start、end、transition、previous_end 使用归一化动画时间。

### 动画参数：`parameter`

`name: name`、`type: float/int/bool/trigger/reset_trigger`。前三类填写对应类型的 `value`；trigger 和 reset_trigger 可省略 value，填写时取 true。

### 受击参数

`direction`：`horizontal?: number=0`、`vertical?: number=0`，单位为角度。

`effect`：`path: string` 必填；`height?: number=-1`、`angle?: number=0`、`delay?: number=0`、`minimum_interval?: number=0`、`maximum_count?: int=0`、`offset_by_scale?: bool=false`。

hit.events 支持：`[]`、`["after_hit","after_hit_all"]`、`["after_hit_all"]`、`["after_hit_miss"]`。

### 移动片段：`segments`

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `destination` | `origin/anchor/target` | 必填 | 流程起点 / 空间点 / 目标 |
| `name?` | `name` | — | anchor 或 target 的名称 |
| `offset?` | `vec3` | `[0,0,0]` | 目的地偏移 |
| `preserve_height?` | `bool` | `true` | 保持当前高度 |
| `stop_distance?` | `number` | `0` | 目标前的停止距离 |
| `target_geometry?` | `bool` | `false` | 使用目标几何信息 |
| `start?` | `number` | `0` | 动画时钟的片段起点 |
| `end?` | `number` | `1` | 动画时钟的片段终点 |
| `speed?` | `number` | `1` | speed 时钟的移动速度 |
| `exit_time?` | `number` | `-1` | 退出进度 |
| `sample?` | 对象 | — | 来源移动采样参数 |

clock：`animation` 跟随 animation 指定的动画请求；`speed` 使用速度；`instant` 立即定位。

animation、speed 模式填写动画请求名 animation。instant 模式使用一个目的地片段。origin 使用流程起点；anchor、target 填写 name。

sample 字段：`percentage: number`、`distance: number`、`direction?: vec3=[0,0,0]`、`behind_target?: bool=false`、`include_radii?: bool=false`。

### 弹体发射项：`shots`

`parameters:name` 引用 resources.projectiles，`target:name` 指定目标；`at?:number=0` 是发射时间，`ordered?:bool=true`，`values?:map<number>={}` 是参数值。

## 持续状态

### 状态定义：`states`

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `values?` | `map<value>` | `{}` | 初始变量 |
| `numbers?` | `map<数值规则>` | `{}` | 带范围的数值变量 |
| `phase?` | `name` | — | 状态提供的阶段名 |
| `state_only?` | `bool` | `false` | 定义纯逻辑状态 |
| `callbacks?` | 回调定义 | — | 进入、退出及信号对应的流程 |
| `instances?` | 实例匹配定义 | — | 状态实例的匹配与重复进入方式 |
| `hit_effects?` | 命中特效覆盖数组 | `[]` | 状态生效期间的命中特效参数 |
| `resilience?` | `0/1/2/3/4` | — | 叠加受击的权重类别 |
| `visual_reaction?` | 对象 | — | 视觉反应：source、flag、occurrence?=default |
| `configuration?` | `name` | — | resources.phases 中的官方阶段配置 |
| `enter?`、`exit?` | `视觉指令[]` | `[]` | 资源作用域的进入与退出 |
| `restore?` | `视觉指令[]` | — | 资源恢复配方；空数组表示空配方 |
| `restore_effects?` | `name[]` | `[]` | 恢复时重建的持续特效 |
| `event_scope?` | `name` | — | 此状态使用的原生事件作用域 |

数值规则：`initial: number`、`min?: number`、`max?: number`。

`state_only:true` 用于保存 `values` 和 `numbers`，资源恢复配方为空数组；省略 `restore` 时自动使用空配方。

回调定义：`enter?`、`exit?`、`reapply?`、`restore?` 为流程名；`signals?: map<流程名>`；`events?: map<流程名>` 使用下节的事件名；`activation?: after_enter/before_callbacks = after_enter` 控制状态在进入流程完成后或之前生效。

实例匹配定义：`name: name`；可选 category、receiver、caster、ability；`match?: receiver/receiver_caster/receiver_caster_ability = receiver`；`repeat?: keep/replace_inputs/new = keep`。

命中特效覆盖：`stacking: highest/multiple`、`path: string|null`、`priority: int`；可选 death_path=""、count=1、death_count=0、offset:vec3、keep_original=false、replay_on_mutex=false。

### 状态操作

状态操作使用 `as` 命名请求，省略时自动命名。

| `op` | 作用 | 参数 |
|---|---|---|
| `state.enter` | 进入状态 | `name: name` |
| `state.exit` | 退出状态 | `name: name`、`all?: bool=false`、`only_caster?: bool` |
| `state.signal` | 向状态发送信号 | `name: name`、`event: name` |
| `state.value` | 修改状态变量 | `name: name`、`variable: name`、`operation: set/add/multiply`，以及一个操作数 |
| `playback.value` | 修改当前运行信息 | `variable:selected/performance`、`operation:set`，以及一个文本操作数 |

state.value 的操作数选择 value、value_from 或 value_input。value 是直接值；value_from 为 `{"context":"状态名","name":"变量名"}`；value_input 是任务输入名。value_input 配合 set 使用。

callbacks 引用 functions 中的完整流程；直接 enter/exit 使用视觉指令。两种进入／退出方式选择一种。phase 是逻辑阶段名，configuration 是官方配置引用，二者独立。

## 视觉指令

视觉指令直接放在流程里，例如 `{"op":"effect.create","effects":["glow"]}`。`source?:name` 可标记来源；`when?:{choice:name,is:name}` 选择选项分支。

资源及播放控制指令可填 `context?:name` 指定持续状态，省略时归当前动作。variable.set 使用局部变量；scope.enter、scope.exit 通过 name 选择资源作用域。

### 特效控制

| `op` | 作用 | 必填参数 | 可选参数及默认值 |
|---|---|---|---|
| `effect.create` | 创建特效 | `effects: name[]` | — |
| `effect.remove` | 回收特效 | `effects: name[]` | `fade?: bool=true`、`immediate?: bool=false`、`optional?: name[]` |
| `effect.remove_event` | 回收指定资源路径的动画事件特效 | `path: name` | `fade?: bool=true`、`immediate?: bool=false`、`unbind?: bool=false` |
| `effect.progress` | 设置特效进度 | `effect: name`、`value: number 或 {variable:name}` | `seconds?: number=0` |
| `effect.trigger` | 设置特效触发器 | `effects: name[]`、`trigger: name` | `reset?: bool=false` |
| `effect.follow` | 设置特效跟随目标 | `effect: name`、`targets: name[]` | — |
| `effect.state` | 播放特效动画状态 | `effect: name`、`state: name` | `transition?: number=0`、`restart?: bool=false` |
| `effect.parameter` | 设置特效动画参数 | `effect: name`、`name: name`、`type: float/int/bool/trigger` | 数值和布尔类型填写 value；`selection?: first_active/first_matching_parameter = first_active` |
| `effect.layout` | 控制特效布局 | `effect: name`、`command: hide/show/perform/transfer` | show/transfer 使用 `fade?: bool=false`；perform 填 `perform: 0/1/2/3`；transfer 填 `target: name` |

effects、effect、target（布局转移）引用特效 key。optional 列出允许未创建的回收对象。follow.targets 取空数组时解除跟随。

```json
{"op":"effect.create","as":"show","effects":["glow"]}
```

### 动画附加控制

| `op` | 作用 | 必填参数 | 可选参数及默认值 |
|---|---|---|---|
| `variable.set` | 设置局部数值变量 | `variable: name`、`value: number` | — |
| `facing.target` | 面向目标 | `target: name` | `ratio?: number=1`、`angle_offset?: number=0`、`duration?: number=-1` |
| `attachment.visibility` | 设置挂点物体显隐 | `points: name[]`、`visible: bool` | `delay?: int=0` |
| `attachment.scale` | 设置挂点物体缩放 | `points: name[]`、`scale: vec3` | — |
| `animation.layer_weight` | 设置动画层权重 | `layer: name`、`weight: number` | `duration?: number=0` |
| `animation.speed` | 设置或重置动画速度 | `speed: number` 或 `reset: true` | — |
| `actor.time_scale` | 设置演员时钟倍率 | `key: name`、`factor: number` | `active?: bool=true`、`recipient?: name`、`delay?: number=0`、`fade_in?: number=0`、`fade_out?: number=0`、`duration?: number` |

actor.time_scale 的回收写法：`active:false` 配合 key 回收对应项，或配合 `finite_only:true` 回收有限时长项；`fade?: bool=false` 控制回收时的淡出。

### 渲染与模型

| `op` | 作用 | 必填参数 | 可选参数及默认值 |
|---|---|---|---|
| `render.mask` | 设置渲染器掩码 | `mask: int`、`enabled: bool` | `recipient?: name`、`node?: name|null` |
| `render.outline` | 设置轮廓目标 | — | `recipient?: name`、`reset?: bool=false`、`add?: name[]`、`remove?: name[]` |
| `render.visibility` | 设置角色显隐 | `key: name`、`visible: bool` | `recipient?: name` |
| `model.change` | 切换模型 | `key: name`、`path: name|null` | — |
| `model.configuration` | 设置模型配置 | 覆盖字段或 `reset:true` | 见下表 |
| `expression.enable` | 开启或关闭表情组件 | `enabled: bool` | `transit_out?: number=0`、`transit_in?: number=0.2` |
| `expression.material` | 设置脸部材质参数 | `cheek: number`、`shy: number`、`shadow: number` | `duration?: number=0` |
| `model.scale` | 设置整体缩放因子 | `scale: number` | — |
| `model.follow_scale` | 跟随目标模型缩放 | `target: name|null` | — |
| `model.character_scale` | 设置组合缩放与轮廓 | `scale: number`、`outline: number` 和一个目标选择项 | `multiplier?: [number,number][]`、`equal_ratio_by_min?: bool=false`、`save_targets?: name` |

model.change.path 取 null 时切回默认模型。model.follow_scale.target 取 null 时停止跟随。

model.character_scale 的目标选择项为 targets:name[]、target_collection:name、read_targets:name 三者之一。

model.configuration 字段：

| 字段 | 类型 | 作用 |
|---|---|---|
| `radius?` | `number` | 模型半径 |
| `somato?` | `int` | 体型类别 |
| `effect_adaptations?` | 对象数组 | 特效资源和挂点适配 |
| `graph_emotion?` | `name|null` | 表情图覆盖；null 使用默认值 |
| `event_files?` | `name[]` | 事件文件列表；[] 清空列表 |
| `reset?` | `bool=false` | 重置配置；可同时指定 event_files |

每项 effect_adaptations 包含 path、attach_point、position:vec3、rotation:vec3、scale:number，可选 alias:string|null。

### 伙伴控制

每条指令填写 partner，引用 resources.partners 的 key。

| `op` | 作用 | 其余参数 |
|---|---|---|
| `partner.create` | 创建伙伴 | — |
| `partner.remove` | 移除伙伴 | — |
| `partner.attach` | 对齐两侧挂点并挂接 | target、self_point、target_point；disable_when_invisible?=false、use_target_scale?=false、visibility?=none |
| `partner.animation_follow` | 设置动画跟随 | target:name|null |
| `partner.visibility_follow` | 设置显隐跟随 | target:name|null、visibility?=none |
| `partner.visibility` | 设置伙伴显隐 | key、visible；mute_collider?=false、mute_triggers?=false、mute_audio?=false |
| `partner.idle` | 播放伙伴动画 | state；normalized_start?=0、normalized_transition?=0、normalized_end?=1 |

visibility：none、visible、invisible、follow、transmit_visible、transmit_invisible、transmit。跟随目标取 null 时解除对应跟随。

### 视觉状态、Timeline 与 BGM

| `op` | 作用 | 必填参数 | 可选参数及默认值 |
|---|---|---|---|
| `scope.enter` | 进入视觉状态 | `name: name` | `fade?: bool=true` |
| `scope.exit` | 退出视觉状态 | `name: name` | `fade?: bool=true` |
| `timeline.play` | 播放 Timeline | `key: name`、`path: name` | `type?: 0`、`wrap_mode?: 0/1/2=0`、`release_volume?: false`、`shared_environment?: bool=false` |
| `timeline.stop` | 停止具名 Timeline | `key: name` | 同上 |
| `bgm.sync` | 让演员动画与当前 BGM 同步 | `state: name`、`bpm: number`、`chapters: 章节[]` | `active?: bool=true` |

bgm.sync 的停止写法为 `active:false`。章节包含 cue:string、phases:阶段[]、double_speed_phases?:阶段[]|null；每个阶段含 start、blend、offset 三个数值。double_speed_phases 省略或取 null 时传入原生空引用，[] 传入空列表。

## 任务事件

下列步骤可填写 as：

| `op` | 作用 | 其他参数 |
|---|---|---|
| `event.emit` | 触发接收方的指定视觉事件回调 | event、recipient；sender?=Caster、allow_position?=false |
| `attack.record` | 将 target 记录为本次攻击及当前片段的接收方 | target；allow_position?=false |
| `attack.finish` | 向发送方发出 after_attack；向接收方发出 after_attacked_adv、after_attacked | — |
| `attack.complete` | 完成 attack.finish，向双方发出 after_attack_end／after_attacked_end，并清除本次记录 | — |
| `attack.chunk_begin` | 开始一个片段并清空片段接收方记录 | — |
| `attack.chunk_end` | 向发送方发出 after_damage_chunk，向片段接收方发出 after_being_damage_chunk，结束片段 | — |

事件名：after_hit、after_hit_all、after_hit_miss、after_attacked_adv、after_attack、after_attacked、after_attack_end、after_attacked_end、after_damage_chunk、after_being_damage_chunk、being_limbo、before_dying。

## 复合动画与信号

`animation.cued` 把身体动画、播放前准备、独立进度轨和后继收尾封装成一个单元。它使用同一 actions 表，必须是该动作 run 中唯一的步骤。

| 字段 | 类型 | 默认值 | 作用 |
|---|---|---|---|
| `state` | `name` | 必填 | 身体动画状态 |
| `restart_scope?` | `bool` | `false` | 重建场景资源作用域；false 延续已有或已恢复的作用域 |
| `prepare?` | `视觉指令[]` | `[]` | 身体动画开始前完成 |
| `tracks?` | 指令轨道数组 | `[]` | 按动画进度执行，各轨独立推进 |
| `complete_at?` | `number` | `1` | 本段完成进度 |
| `then?` | 对象 | — | 后继动画：state，以及 at?=0 和 run?:视觉指令[] |

每条轨道是 `{"at":进度,"run":[...]}` 的数组，at 严格递增。at 使用当前身体动画的归一化进度；then.at 使用实际进入的后继动画进度。

根 signals 的值为视觉指令数组，或 `{"context":"状态名","run":[...]}`。

## 附录

- [完整字段类型声明](script-api.d.ts)
- [最小完整文件](examples/standby.script.json)
