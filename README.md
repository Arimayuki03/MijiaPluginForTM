<div align="center">

# 米家插座功率 TrafficMonitor 插件

**在 Windows 任务栏实时显示米家/酷控（cuco）智能插座的功率**
多设备同显 · 总功率合计 · 功率历史统计 · 单文件 DLL · 零第三方依赖

> [!NOTE]
> 本项目 fork 自 [cxhoyo/MijiaPluginForTM](https://github.com/cxhoyo/MijiaPluginForTM)，并在此基础上持续迭代（v1.1.0 起的多设备支持、DPI 适配与多轮稳定性修复均由本 fork 完成）。感谢原作者的奠基工作。

[![Release](https://img.shields.io/github/v/release/Arimayuki03/MijiaPluginForTM?sort=semver&color=success)](https://github.com/Arimayuki03/MijiaPluginForTM/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Arimayuki03/MijiaPluginForTM/total?color=success)](https://github.com/Arimayuki03/MijiaPluginForTM/releases)
[![License](https://img.shields.io/github/license/Arimayuki03/MijiaPluginForTM?color=blue)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078D6)](https://github.com/zhongyang219/TrafficMonitor)
[![Host](https://img.shields.io/badge/TrafficMonitor-%E2%89%A51.74-00A4EF)](https://github.com/zhongyang219/TrafficMonitor)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org)

![任务栏效果](任务栏效果.png)

*任务栏实时功率显示*

![插件设置界面](插件截图.png)

*插件设置界面（多设备管理）*

[✨ 功能特性](#-功能特性) · [🚀 快速开始](#-快速开始) · [🔑 获取 IP 与 Token](#-获取-ip-与-token) · [🔧 配置说明](#-配置说明) · [❓ 常见问题](#-常见问题与注意事项) · [🔨 从源码构建](#-从源码构建) · [📜 更新日志](CHANGELOG.md)

</div>

---

## ✨ 功能特性

- 📊 **实时功率**：任务栏直接显示插座当前功率（W），断线自动重连
- 🔌 **多设备同显**（v1.1.0+）：最多 8 个插座同时显示，每个插座一个独立显示项
- ➕ **总功率合计**（v1.2.0+）：可自由勾选哪些插座计入"总功率"条目
- ⏸️ **单独禁用**（v1.2.1+）：每个插座可单独禁用/启用，禁用后不建立连接、不采集
- 💾 **功率历史记录**（可选）：按分钟采样，每个插座独立保存 7 天数据
- 📈 **悬停统计**：鼠标悬停显示当前功率与 N 小时内最高/最低/平均功率（时段 1~24 小时可调，v1.2.2+，需开启历史记录）
- ⚙️ **图形化设置**：设备增删改、逐个测试连接、显示格式调整；完整适配高 DPI 与混合 DPI 多屏（v1.2.3 / v1.2.4）
- 🪶 **零依赖单文件**：miIO 协议（AES-128-CBC + MD5 + UDP）纯 C++ 实现，静态链接，无需安装任何运行库

---

## 🚀 快速开始

### 环境要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10 / 11（x64） |
| 宿主程序 | [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) v1.74 或更高版本 |
| 网络 | 智能插座与电脑处于**同一局域网** |
| 设备 | 已接入米家 APP 的米家/酷控智能插座（已知兼容 `cuco.plug.v3`） |

### 安装步骤

1. **获取 IP 和 Token** → 使用 [Xiaomi Cloud Tokens Extractor](https://github.com/PiotrMachowski/Xiaomi-cloud-tokens-extractor/releases)（详见[下文](#-获取-ip-与-token)）
2. **打开插件目录** → TrafficMonitor 右键 → 选项 → 常规设置 → 下滑找到插件管理 → 打开插件目录
3. **放入 DLL** → 从 [Releases](https://github.com/Arimayuki03/MijiaPluginForTM/releases/latest) 下载 `MijiaPower.dll`，复制到目录内
4. **重启 TrafficMonitor** → 完全退出后重新启动
5. **填写配置** → 右键 TrafficMonitor → 选项 → 左侧选择 **MijiaPowerPlugin**，填入 IP 和 Token，点击"测试连接"

安装完成后，任务栏即显示 `设备名: 196.0W`（可在设置中关闭名称前缀）；多设备时可在 TrafficMonitor 的"显示设置"中勾选"米家插座总功率"查看合计。

> 💡 遇到问题？请看[常见问题](#-常见问题与注意事项)。

---

## 🔑 获取 IP 与 Token

插件通过 miIO 协议（UDP 端口 54321）与插座直接通信，需要设备的**局域网 IP** 和 **Token**。

### 🌟 推荐方案：Xiaomi Cloud Tokens Extractor

最便捷的方法是使用 [Xiaomi-cloud-tokens-extractor](https://github.com/PiotrMachowski/Xiaomi-cloud-tokens-extractor)，它可以自动从米家云提取所有设备的 Token 和 IP：

1. **下载工具**：访问 [Release 页面](https://github.com/PiotrMachowski/Xiaomi-cloud-tokens-extractor/releases)，Windows 用户下载 `.exe` 文件
2. **运行提取器**：双击 `.exe` 文件运行，或在命令行中执行 `xiaomi_cloud_tokens_extractor.exe`
3. **输入米家账户信息**：
   ```
   Username: 你的米家账户（邮箱或手机号）
   Password: 你的米家密码
   Server:   China（中国用户选择此项）
   ```
4. **查看结果**：工具会自动列出所有米家设备，复制插座设备的 **IP** 和 **Token**：
   ```
   设备名称: 客厅插座
   Model:    cuco.plug.v3
   IP:       192.0.2.100
   Token:    a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6
   ```
5. **保存信息**：⚠️ Token 等同于设备控制权，请妥善保管，不要分享给他人

<details>
<summary><b>📖 其他获取方法</b></summary>

**方法 B — Python miio 工具**

```bash
# 安装 python-miio
pip install python-miio

# 提取所有设备的 Token（会显示所有设备的信息）
python -m miio.extract_tokens
```

**方法 C — MiToolKit**

项目地址：[MiToolKit](https://github.com/ultraicy/MiToolKit)。下载运行后，选择"提取 Token"功能，选择要提取的设备。

**方法 D — 米家 APP 抓包提取**

使用 Fiddler 或 Charles 代理，在米家 APP 中操作设备，从代理日志的 JSON 响应中提取 Token 字段。

</details>

---

## 🔧 配置说明

### 多设备配置（v1.1.0+）

打开插件设置对话框后：

1. **添加设备** → 点击"添加设备"，最多支持 8 个插座
2. **选中设备** → 在列表中选中要配置的插座
3. **填写信息** → 为选中的插座填写：
   - **名称**：显示在任务栏标签和悬停提示中（如"总控"、"客厅插座"）
   - **设备 IP**：米家插座的局域网 IP 地址
   - **Token**：32 位十六进制字符串（获取方法见上文）
4. **行内勾选** → 点击列表行首的复选框切换**启用**（禁用后不采集连接，任务栏显示"已禁用"）；点击行尾的复选框切换**计入总功率**（也可选中后按空格切换启用）
5. **测试连接** → 逐个点击验证每个插座的配置
6. 点击 **确定** 保存

> 💡 **注意**：新增或删除设备后，需要**重启 TrafficMonitor** 才能显示/隐藏对应的任务栏条目（TrafficMonitor 只在启动时枚举插件的显示项）；修改名称、IP、Token 等属性则立即生效。

### 任务栏显示项

每个插座在 TrafficMonitor 的"显示设置"中对应一个独立条目：

| 显示项 | ItemId | 说明 |
|--------|--------|------|
| 米家插座功率(设备名) | `MijiaPwr1`~`MijiaPwr8` | 各插座的实时功率 |
| 米家插座总功率 | `MijiaPwrTotal` | 已勾选"计入总功率"的插座功率合计（2 个及以上设备时出现） |

任务栏每个条目的标签为设备名称（如 `总控:196.0W`），可在插件设置中关闭标签。

> ⚠️ **TrafficMonitor 的标签缓存机制**：任务栏窗口会按显示项 ID 在 `config.ini` 的 `[plugin_display_str_taskbar_window]` 中缓存标签文本并在之后覆盖插件提供的实时标签。修改设备名称后，任务栏标签要**重启 TrafficMonitor** 才会更新；若仍显示旧名称，可在「任务栏窗口设置 → 显示设置」中修改对应条目的标签文本。v1.1.0 及更早版本使用的旧 ID（`MijiaPowerW` 系列）下存在 v1.0 写死的"功率："缓存，v1.1.1 起已换用新 ID，需在显示设置中重新勾选功率条目。

### 配置选项

安装完成后，可以在插件选项中调整：

| 选项 | 默认值 | 推荐值 | 说明 |
|------|--------|--------|------|
| 启用功率历史记录 | 开启 | 开启 | 按分钟采样，每个插座独立保存 7 天数据 |
| 显示设备名称标签 | 开启 | 开启 | 任务栏显示设备名前缀（如"总控:"） |
| 显示总功率项 | 开启 | 开启 | 多设备时提供"总功率"合计条目（合计范围由设备列表中各行的勾选决定） |
| 显示 W 单位 | 开启 | 开启 | 数值后显示"W" |
| 采集间隔（秒） | 3 | 3-5 | 查询设备的间隔（多设备时为轮询所有设备的间隔），越小说明更新越快但消耗越多资源 |
| 小数位数 | 1 | 1 | 显示精度：0=整数，1=一位小数，2=两位小数 |
| 悬浮统计时段 | 1 小时 | 按需 | 悬停提示中"最高/最低/平均"统计的时间窗口（1/2/3/6/12/24 小时） |

---

## 📁 配置文件

插件配置文件保存在 TrafficMonitor 的插件配置目录：

```
<插件配置目录>\
  MijiaPower.ini                        ← 设备信息和选项
  MijiaPower_history_<IP>_s<槽位>.json  ← 各插座的功率历史（按 IP+槽位命名，如果启用）
```

历史文件按**设备身份（IP + 持久化槽位）**命名：每台设备在配置中持有一个唯一的 `HistorySlot`（1~8，随 INI 保存），文件名由 IP 与槽位共同决定。同一台设备无论删除其他设备、调整顺序还是增删同 IP 设备，其历史文件路径永远不变——即使多台设备共用同一 IP，历史也不会互相覆盖或错位（v1.2.4 及更早版本在"同 IP 多设备 + 增删/重排"场景下存在静默覆盖缺陷，v1.3.0 修复）。

> 💡 **历史文件自动迁移**：首次加载时按链式迁移，均为"目标不存在才迁移"，可重复执行、无需手动修改：
> - v1.0 `MijiaPower_history.json` → 第 1 个设备的历史文件
> - v1.1.0/1.1.1 按索引命名的 `MijiaPower_history_N.json` → 对应设备的历史文件
> - v1.2.x 按 IP 命名的 `MijiaPower_history_<IP>.json`（同 IP 多设备时 `<IP>_2/_3.json`）→ 对应设备的 IP+槽位命名文件（升级时旧配置按设备顺序分配槽位，归属关系与升级前一致）

> 💡 **从 v1.0 升级**：旧版单设备配置（`[Device]` 段）会被自动读取为第 1 个设备，历史文件自动迁移，无需手动修改。

### MijiaPower.ini 格式示例（v1.1.0 多设备）

```ini
[Plugin]
DeviceCount=3
EnableRecording=1
ShowLabel=1
ShowTotal=1
ShowUnit=1
UpdateIntervalSec=3
DecimalPlaces=1
TooltipStatsHours=1

[Device1]
IP=192.0.2.101
Token=2b7f4a9c1d3e5f60718293a4b5c6d7e8
Name=客厅插座
InTotal=1
Enabled=1
HistorySlot=1

[Device2]
IP=192.0.2.102
Token=0f1e2d3c4b5a69788796a5b4c3d2e1f0
Name=书房插座
InTotal=0
Enabled=0
HistorySlot=2

[Device3]
IP=192.0.2.103
Token=5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d
Name=卧室插座
InTotal=1
Enabled=1
HistorySlot=3
```

> 💡 **键说明**：`InTotal` 是否计入"总功率"合计（v1.2.0+）；`Enabled` 是否启用（v1.2.1+，禁用后不采集连接）；`HistorySlot` 历史文件槽位（v1.3.0+，1~8，决定历史文件名，删除/重排设备不漂移）；`TooltipStatsHours` 悬停统计时段小时数（v1.2.2+，1~24）。前三者缺省均视为 1，`HistorySlot` 缺省自动分配。

> ⚠️ **Token 安全**：Token 等同于设备控制权，以上均为虚构示例。请勿把插件配置目录纳入云同步、网盘或代码仓库——`MijiaPower.ini` 中以明文保存的 Token 会随目录外泄；如怀疑泄露，可在米家 App 中重新配网以轮换 Token。v1.3.0 起保存配置时会校验 Token 格式（32 位十六进制），格式非法的设备将被拒绝保存并提示。

---

## ❓ 常见问题与注意事项

### 故障排查

| 问题 | 症状 | 解决方案 |
|------|------|---------|
| 连接失败 | 显示"连接失败" | 检查 IP 和 Token 是否正确 |
| 没有显示 | 看不到功率数值 | 确认 DLL 文件在插件目录中，并已完全重启 TrafficMonitor |
| 数据异常 | 显示 0W 或错误值 | 检查设备是否开启，重启插件 |
| Token 提取失败 | 工具无法提取 | 确保米家账户密码正确、网络正常 |
| 一直"连接中..." | 任务栏数值不更新 | 升级到 v1.3.0+（旧版对不支持功率属性的设备会无限重连） |

### 注意事项

1. 插件通过 UDP 协议（端口 54321）直接与设备通信，**设备必须与电脑在同一局域网**
2. miIO 协议需要正确的 Token，错误的 Token 会导致连接失败
3. 采集间隔不建议设置低于 2 秒，以免对设备造成过多请求
4. 如果设备固件不支持 `siid=11,piid=2`（功率属性），v1.3.0 起任务栏会显示 `--`、设置中"测试连接"会明确提示"设备在线，但未返回功率属性"（连接保持，不再无限重连）
5. 悬停统计需开启历史记录；TrafficMonitor 重启后统计需重新积累

---

## 🧩 兼容设备

基于原项目 `mijia_plug` 的设备属性定义：

| 属性 | siid | piid | 说明 |
|------|------|------|------|
| 开关 | 2 | 1 | 插座开关状态 |
| 功率 | 11 | 2 | 当前功率（W） |
| 能耗 | 11 | 1 | 累计用电量 |
| 温度 | 12 | 2 | 插座温度 |

已知兼容型号：**`cuco.plug.v3`**（米家智能插座 3）。其他米家/酷控插座如支持相同属性也可能兼容，欢迎[反馈](https://github.com/Arimayuki03/MijiaPluginForTM/issues)。

---

## 🔨 从源码构建

插件完全无第三方依赖，所有加密算法（AES-128-CBC、MD5）均为纯 C++ 实现，仅依赖 Windows 系统 API（ws2_32、comctl32、gdi32、user32、ole32）。

- **Visual Studio**：打开 `MijiaPowerPlugin.sln` 编译 Release|x64（或运行 `compile.ps1`）
- **MinGW (GCC)**：运行 `bash build_gcc.sh`（生成静态链接的 x64 DLL，无需运行时依赖；同时构建 `test_unit`/`test_harness`/`test_dialog`/`test_dpi` 四个测试宿主）

### 测试

| 测试宿主 | 说明 |
|----------|------|
| `test_unit.exe` | 离线单元测试（19 用例 88 断言，覆盖历史读写/原子写入/时钟回拨/槽位命名迁移/Token 校验/格式化等），无需真实设备，全部通过退出码为 0 |
| `test_harness.exe <dll路径> <配置目录>` | 冒烟测试宿主（模拟 TrafficMonitor 加载 DLL） |
| `test_dialog.exe` / `test_dpi.exe` | 设置对话框 / 高 DPI 手动验证宿主 |

### 项目结构

| 文件 | 说明 |
|------|------|
| `PluginInterface.h` | TrafficMonitor 插件接口定义 |
| `MiioDevice.h/.cpp` | 纯 C++ miIO 协议实现（AES-128-CBC + UDP） |
| `PowerHistory.h/.cpp` | 功率历史采样与统计 |
| `PluginConfig.h/.cpp` | 插件配置（INI 文件读写） |
| `MijiaPowerPlugin.h/.cpp` | 插件主类（ITMPlugin/IPluginItem 实现） |
| `OptionsDlg.h/.cpp` | 设置对话框（纯 Win32，DPI 适配） |
| `pch.h/.cpp` | 预编译头 |
| `test_unit.cpp` | 离线单元测试 |
| `test_harness.cpp` | 冒烟测试宿主 |
| `test_dialog.cpp` / `test_dpi.cpp` | 对话框 / DPI 手动验证宿主 |
| `build_gcc.sh` / `compile.ps1` / `compile.bat` | 构建脚本 |

---

## 🤝 贡献

欢迎提交 [Issue](https://github.com/Arimayuki03/MijiaPluginForTM/issues) 和 [Pull Request](https://github.com/Arimayuki03/MijiaPluginForTM/pulls)：

- 🧩 新设备兼容性反馈：附上插座型号与功率显示是否正常
- 🐛 Bug 报告：请附上 TrafficMonitor 版本、Windows 版本与复现步骤
- ✅ 提交代码前请运行 `bash build_gcc.sh` 与 `test_unit.exe`，确保全部测试通过

---

## 📄 许可证

本项目以 [MIT License](LICENSE) 开源。

其中 `PluginInterface.h` 取自 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)（MIT License，Copyright (C) by Zhong Yang 2021），版权归其作者所有；miIO 协议实现参考了社区对米家设备的逆向成果（AES-128-CBC + MD5 + UDP）。

## 🙏 致谢

- [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) — 优秀的 Windows 任务栏监控工具，本项目为其插件
- [cxhoyo/MijiaPluginForTM](https://github.com/cxhoyo/MijiaPluginForTM) — 原仓库，本项目 fork 自它并持续迭代；v1.0 单设备版与设备属性定义来自原作者
- [Xiaomi-cloud-tokens-extractor](https://github.com/PiotrMachowski/Xiaomi-cloud-tokens-extractor) — 便捷的 Token 提取工具

## 📜 更新日志

完整的版本历史见 [CHANGELOG.md](CHANGELOG.md)。最近更新：

| 版本 | 日期 | 要点 |
|------|------|------|
| [v1.3.0](https://github.com/Arimayuki03/MijiaPluginForTM/releases/tag/v1.3.0) | 2026-09-20 | 修复同 IP 多设备历史覆盖/错配、历史解析 O(n²) 卡顿、Token 校验缺口、退出竞态；历史写入原子化；新增离线单元测试 |
| v1.2.4 | 2026-09-16 | 修复混合 DPI 多屏设置窗口错乱 |
| v1.2.2 | 2026-09-16 | 悬停统计时段可调（1~24 小时） |
| v1.2.0 | 2026-09-16 | 总功率合计范围可勾选 |
| v1.1.0 | 2026-09-13 | 多插座同显（最多 8 个）、总功率项、设备列表式设置 |

---

<div align="center">

如果这个项目对你有帮助，欢迎点一个 ⭐ Star 支持一下！

</div>
