# Character Framework 使用说明

[English](README.en.md)

运行环境：Windows x64、D3D11、崩坏：星穹铁道4.6.0及Microsoft Visual C++ x64运行库。包内包含 `Hksr.Api.dll` 和 `Hksr.CharacterFramework.dll`。

## 行为与风险声明

本项目是非官方第三方模组。它将 `Hksr.Api.dll` 和 `Hksr.CharacterFramework.dll` 加载到游戏进程，读取并修改进程内状态，挂接和调用游戏内部接口，以创建、控制额外角色及其视觉表现。布局、设置和日志保存在模组目录。

> [!CAUTION]
> 本项目按现状提供，不保证安全性、稳定性、兼容性或账号安全。使用前请自行了解适用的游戏及平台规则，并备份重要数据。**使用者自行决定是否使用，并承担由此产生的风险与后果；无法接受上述风险时，请勿使用。**

## 安装和启动

1. 从 [Releases](https://github.com/Charl-Ep/hsr-character-framework/releases/latest) 下载 `CharacterFramework-<版本>-win64.zip`，其中已包含两个DLL。
2. 关闭游戏，将完整的 `CharacterFramework` 文件夹解压到一个固定、可写的目录，保留包内目录结构，两个DLL放在同一文件夹。
3. 在XXMI Launcher中选择崩铁／SRMI，打开 **设置 → 高级 → 自定义库**，勾选 **注入库**。在路径列表中按下面的顺序填写两个DLL的完整路径，每行一个：

   ```text
   D:\Mods\CharacterFramework\Hksr.Api.dll
   D:\Mods\CharacterFramework\Hksr.CharacterFramework.dll
   ```

4. 将示例替换为实际路径，通过XXMI Launcher启动游戏；若出现库信任提示，确认加载这两个DLL。进入探索场景后按 **Alt+H** 打开管理菜单。

只登记一份。移动文件夹后需更新注入路径。布局、设置和日志写在DLL所在目录；更新前关闭游戏，并保留 `Layouts`、`ui-settings.cfg` 和 `http-settings.cfg`。

## 运行行为

- **Alt+H**打开或隐藏中英文管理菜单。
- **镜头演出**控制附加角色的相机空间特效和2D演出，默认关闭，设置自动保存。
- NPC来源使用NPC外观与动作；自机来源随动作域切换探索／战斗模型。
- 菜单管理实例创建／删除、动作、位置、旋转和注视。
- Y轴移动取消吸附，贴地按钮恢复；NPC行为也可能控制移动。
- 注视支持自动、角色目标和世界点；最大距离0不额外限制距离。
- 每地图自动恢复工作布局，动作从头播放；命名快照独立保存和加载。

功能用于角色动画和视觉效果。复杂技能可能需要额外游戏状态。已识别的相机／环境任务默认受限；初次资源加载可能造成帧停顿。

## 接口与数据

[HTTP／SSE](docs/http-api.md)默认关闭，通过 `http-settings.cfg` 配置；示例文件不作为运行设置读取。

[应用服务](docs/application-services.md)说明命令结果和持久化；[表现配置](docs/presentation-profiles.md)定义阶段待机和动作完成规则。

`framework-runtime.log`记录启动、命令和错误，下次启动会覆盖。排查时先保留日志。停用模组需关闭游戏后移除两条额外库登记。

## 许可证

[WTFPL v2](LICENSE)。依赖许可见 `licenses/`。
