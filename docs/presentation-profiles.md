# 角色表现配置

`PresentationProfiles/*.json` 按官方战斗配置路径匹配角色，控制阶段、进度标记和返回待机。Framework启动时读取，运行时使用不可变数据；修改后需重启。

## 格式

```json
{
  "format": "hksr.presentation-profile",
  "id": "example",
  "source": "Config/ConfigCharacter/Avatar/Example_Config.json",
  "initial_phase": "normal",
  "reset_on_domain_exit": true,
  "phases": {
    "normal": {"idle": "Idle"},
    "changed": {"idle": "OtherIdle"}
  },
  "actions": {
    "ExampleAction": {
      "cues": [{"at": 0.75, "phase": "changed"}],
      "complete_at": 1,
      "return": "phase_idle"
    },
    "ExampleExit": {"on_complete": "normal"}
  }
}
```

| 字段 | 含义 |
|---|---|
| `source` | 精确匹配的官方战斗配置路径 |
| `initial_phase` | 新实例默认阶段 |
| `phases.<id>.idle` | 阶段待机状态名 |
| `actions.<state>.on_enter` | 播放调用成功提交后采用的阶段 |
| `cues` | 按归一化进度触发一次的阶段变化，时间必须严格递增 |
| `complete_at` | 非循环配置动作的完成进度，默认1 |
| `return` | `phase_idle`（默认）返回阶段待机；`hold`保持姿态 |
| `on_complete` | 到达完成边界后的阶段变化，先于返回待机 |
| `on_interrupt` | 取消或被成功播放的新动作替换时采用的阶段 |
| `reset_on_domain_exit` | 切到探索域时恢复初始阶段，默认true |

进度必须有限、非负，cue不能晚于complete_at。阶段引用必须存在，未知字段报错。

## 执行

准备态仅使用on_enter和被替换时的on_interrupt。普通配置动作使用进度与完成规则；循环动作不会自动结束。控制器自行进入后继时，不补发未观察到的on_complete。

每个实例独立保存阶段。离图保留阶段，回图／重建使用阶段待机初始化，再从头播放保存的动作。显式切到探索域按reset_on_domain_exit处理。

配置只控制视觉播放，伤害、回合和玩法Modifier不执行。附件与效果通过动画事件和退出任务处理。

错误文件记录原因，不阻断其它配置；同一source出现多个文件时，该来源不应用额外规则。

## 原生事件归属

可选的`native_events`供`VisualEventRoutes`关联官方事件产生的特效。每项指定`file`、`part`、`scope`、`key`；状态事件另有`states`，行为事件另有`behavior`。

- `part`：`time_point`、`group_enter`、`group_exit`或`behavior`。
- `states`：完整状态成员集合，不能只匹配多状态组中的一个名字。
- `scope`：编排拥有的持续效果上下文；`key`：该上下文内部的请求分组。
- `effect_keys`：可选的原始`TriggerEffect.EffectPath`到本地请求键的映射；未列出的效果沿用`key`。

调用方在模型事件构造前用`Declare`登记实体代次和规则，在播放前用`Attach`指定实际事件接收者及上下文。构造、序列和效果观察回执交给`VisualEventRoutes`，以实际任务列表引用关联定义。定义可跨次播放复用，`Detach`只阻止新的关联；已经执行的序列保留原上下文的回收责任。上下文执行`End`后持续`Service`至`Drained`。当前宿主不自动激活这些规则。

跨动作场景的执行接口见[Visual programs](visual-programs.md)。

## 随附配置

`SilverWolf999.json` 定义normal／ultra阶段，待机分别为StandBy／StandBy_Transfer。Skill03在进度0.806进入ultra，进度1返回阶段待机；道具动作使用hold，退出动作完成后返回normal。

`Sparxie.json`将Skill02_Gift、Skill02和Skill02_V1的原生时间点特效关联到`gift-scene`。它只定义原生事件归属，不包含能力模板、Timeline或完整技能调度。
