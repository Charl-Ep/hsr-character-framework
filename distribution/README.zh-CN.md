# Character Framework 运行集成

[English](README.en.md)

运行环境：Windows x64、D3D11、崩坏：星穹铁道4.6.0及Microsoft Visual C++ x64运行库。包内包含 `Hksr.Api.dll` 和 `Hksr.CharacterFramework.dll`。

## 加载

关闭游戏后，将包放入SRMI的Mods目录，在XXMI Launcher的Extra Libraries中依次登记完整路径：

1. `Hksr.Api.dll`
2. `Hksr.CharacterFramework.dll`

只登记一份。不支持运行中卸载DLL；替换前退出游戏。更新时保留 `Layouts`、`ui-settings.cfg` 和 `http-settings.cfg`。

## 运行行为

- **Alt+H**打开或隐藏中英文管理菜单。
- NPC来源使用NPC外观与动作；自机来源随动作域切换探索／战斗模型。
- 菜单管理实例创建／删除、动作、位置、旋转和注视。
- Y轴移动取消吸附，贴地按钮恢复；NPC行为也可能控制移动。
- 注视支持自动、角色目标和世界点；最大距离0不额外限制距离。
- 每地图自动恢复工作布局，动作从头播放；命名快照独立保存和加载。

视觉播放不执行伤害或回合。复杂技能可能需要额外游戏状态。已识别的相机／环境任务默认受限；初次资源加载可能造成帧停顿。

## 接口与数据

[HTTP／SSE](docs/http-api.md)默认关闭，通过 `http-settings.cfg` 配置；示例文件不作为运行设置读取。

[应用服务](docs/application-services.md)说明命令结果和持久化；[表现配置](docs/presentation-profiles.md)定义阶段待机和动作完成规则。

`framework-runtime.log`记录启动、命令和错误，下次启动会覆盖。排查时先保留日志。停用模组需关闭游戏后移除两条额外库登记。

## 许可证

[WTFPL v2](LICENSE)。依赖许可见 `licenses/`。
