# 应用服务

GUI、HTTP和开发控制器向 `ApplicationService` 提交类型化命令。`ApplicationCoordinator` 在官方帧分发，`SceneCharacters` 管理逻辑实例和引擎创建代次。

## 线程与分发

`Post`、`Snapshot`、`ReadUpdates` 可并发调用。命令按入队顺序分配serial，官方帧处理固定批次；回调中追加的命令进入下一批。

回执在锁外交给指定接收者，未指定则进入菜单队列。关闭后拒绝新命令并取消未执行请求。HTTP断线不取消已接受命令。

## 来源与动作

`PrepareSource` 返回独立的 `source_request`，通过 `CatalogSnapshot::Source(id)` 读取Queued、Preparing、Ready、Unresolved或Failed。Ready表示CharacterPlan可用；Unresolved附候选和原因。

`AddPreparedCharacter`、`ReplacePreparedCharacter` 引用准备编号。释放来源不影响已创建实例；`RetrySource` 创建新请求。

`BrowseCharacterActions{id}` 查询实例目录。`SelectCatalogAction` 使用实例ID、目录revision和动作身份：

- `ConfiguredActionKey`：动作域、状态名，包含关联退出任务。
- `ReadyActionKey`：配置路径、准备态下标。

来源或配置变化后重新读取revision。`SelectCharacterAction` 接受原始状态、NPC编号及显式 `ProgramAction`；原始动画不附加目录条目的伴随任务。`IdleCharacter` 提交 `BattleIdle`，在模型与上下文就绪后解析当前阶段的待机。

## 操作结果与历史

入队返回serial；分发返回accepted。异步命令另返回与serial相同的operation，查询命令为0。通过 `ApplicationSnapshot::operations.entries[id]` 读取结果。

| 操作 | Returned含义 |
|---|---|
| 来源准备 | CharacterPlan可用 |
| 创建、启用、重建、替换 | 实体加载且外观接入 |
| 停用 | 资源排空，保留逻辑实例 |
| 删除 | 资源排空并移除逻辑实例 |
| 动作 | 官方播放调用返回 |
| 位置 | 合成位置与旋转回写返回 |
| 注视 | 目标或自动策略已应用 |

Returned不等于动画结束；Failed不等于资源已清理。清理状态见retiring、pending_inputs及宿主closed。

Waiting、Running为非终态；Returned、Failed、Cancelled、Superseded为冻结终态。默认保留最近256条终态，未完成操作不淘汰。`operations.sequence` 与事件游标独立。

动作还带实例内递增的action_intent和实际creation代次。自动待机不覆盖用户命令回执。离图保留动作意图，回图从头播放；替换来源清除旧动作。

## 位置与注视

SDK控制回执只在官方线程读取，Framework将结果复制到快照。位置增量合成一次回写；被后续绝对位置覆盖的未提交贡献为Superseded。

注视目标通过当前模型的Head挂点解析。目标未就绪时等待；最大距离0不额外限制距离。恢复自动策略时的restoring表示官方淡出尚未完成，动作域切换等待其完成。

## 快照与事件

`SceneCharacterStatus::branch_state`提供当前表现阶段、已采用的配置选项和数值变量。具名上下文保留各自的值，以scope区分；排队中的新选项不覆盖当前值。它不读取或推测未接入的战斗玩法变量。

`ReadUpdates` 原子返回快照、游标和变化列表。首次读取、stream不匹配或序号越界返回reset。变化记录中的ID在快照中不存在表示删除或淘汰；通知可合并中间变化。

默认保留256次有变化的发布。`WaitUpdates`、`WaitForChange` 仅供传输线程使用，服务关闭时唤醒。销毁服务前结束等待线程。

新增状态字段时同步更新 `ApplicationEvents` 的比较。网络协议见[HTTP与SSE](http-api.md)。

## 地图工作布局

工作布局保存官方来源、位置、注视、动作和[表现阶段](presentation-profiles.md)。动作恢复从头播放；回到当前阶段待机后保存 `BattleIdle`，其它观测到的控制器后继按名字保存并在恢复时检查HasState。[视觉上下文](visual-programs.md#layout-reconstruction)保存有类型的值输入，按配置恢复资源后再播放动作。

`LayoutController` 先准备来源和动作，再替换当前地图实例并跟踪回收／创建。`LayoutIo` 后台编码和写盘，相同布局不重写，待写变化可合并。

首次恢复完成前不会用空启动状态覆盖磁盘。关闭时先捕获布局，再停止实例和写入生产端；worker处理已接收写入。强制结束可能丢失最后修改。

命名快照通过 `LayoutCommand` 管理，结果在 `LayoutSnapshot`。详见[布局接口](http-api.md#map-layouts)。
