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

## 随附配置

`SilverWolf999.json` 定义normal／ultra阶段，待机分别为StandBy／StandBy_Transfer。Skill03在进度0.806进入ultra，进度1返回阶段待机；道具动作使用hold，退出动作完成后返回normal。
