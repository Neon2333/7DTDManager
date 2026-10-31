# 七日杀多版本与存档管理工具

一个基于 **Qt 6 Widgets (C++)** 的 Windows 桌面工具，用于集中管理《7 Days to Die》的多个游戏版本与存档：

- 每个版本保留完整目录，可直接启动；
- 相同文件通过 **NTFS 硬链接**共享，减少磁盘占用；
- 以存档列表为核心，选中存档后自动用对应版本启动，并尽可能直接加载该存档。

## 功能特性

### 版本管理

- 从 Steam 安装目录复制导入新版本；也可直接登记已有完整游戏目录。
- 版本目录统一命名为 `7 Days To DieV<版本>`，例如：
  - `G:\7DTD_versions\7 Days To DieV1.4`
  - `G:\7DTD_versions\7 Days To DieV2.6`
- 通过 `QFileDialog` 明确指定基准版本目录，不依赖“第一个导入的版本”。
- 展示逻辑大小、物理占用（按文件 ID 去重统计）与文件数。
- 删除版本时可选择“仅移除记录”或“同时删除目录”；仅移除记录不会删除磁盘文件，硬链接数据只要还有其他链接存在就不会丢失。

### 硬链接去重

- 对比非基准版本与基准版本中同路径、同大小、同 SHA-256 的文件，建立硬链接。
- 去重前先展示干跑结果：可共享文件数、预计节省空间。
- 安全替换：临时链接 → 原文件改名备份 → 链接改名 → 删除旧文件，失败可回滚。
- 支持“校验基准”（检测基准清单漂移）和“解除链接”（复制替换，恢复独立文件）。
- 默认排除运行时写入、用户可编辑和 Mods 文件，避免共享后互相影响。

### 存档管理

- 扫描 `<存档目录>\<世界>\<存档>\main.ttw`。
- 同时读取 `main.ttw` 与 `main.ttw.ext.bak`，显示：
  - 原始版本；
  - 最后保存版本；
  - 是否已被其他版本升级/重写。
- 存档与游戏版本按主次版本号匹配；支持手动“指定版本”。
- 选中存档点击“从版本开始游戏”时，自动选择对应版本目录启动。
- 存档版本与所选游戏版本不一致时默认阻止，并提供“备份并强制启动”。

### 启动、自动加载与备份

- 直接运行 `<版本目录>\7DaysToDie.exe`，不把 20GB 游戏复制回 Steam 目录。
- 默认追加 `-noeac`；可在设置中启用 `-skipintro`、附加参数。
- 自动加载选中的存档（默认开启）。
- 启动前备份策略：仅版本不匹配时 / 每次启动前 / 从不备份；每个存档保留最近 N 份。
- 监视工具专属启动日志，回填游戏实际版本、`UserDataFolder`、自动加载结果与跨版本警告。

## 工作原理

### 完整目录 + 硬链接

Windows 没有现成的联合挂载，差异目录无法直接启动。因此采用：

1. 每个版本目录都保持完整文件视图；
2. 相同文件在 NTFS 上指向同一份数据（硬链接）；
3. 物理上只占用各版本真正不同的部分。

硬性要求：

- 所有参与去重的版本必须位于**同一 NTFS 卷**；
- 基准版本目录必须保持冻结，不能是 Steam 正在更新/管理的目录；
- 游戏运行时可能写入的文件（如 `boot.config`）必须排除在共享之外。

### 逻辑大小与物理占用

- 逻辑大小 = 各版本文件大小之和；
- 物理占用 = 按 `(卷序列号, 文件索引)` 去重后的大小之和；
- 因此去重后：物理占用通常明显小于逻辑大小。

### 存档版本识别

7DTD 的 `main.ttw` 头部包含版本字符串（如 `V 1.4 (b8)`）。工具解析 `main.ttw` 作为“最后保存版本”，解析 `main.ttw.ext.bak` 作为“原始版本”。

注意：游戏正常存档时会轮换备份文件，`main.ttw.ext.bak` 可能随游戏使用而变化，不保证永远保留最早的版本。

## 环境要求

- Windows 10 / 11 x64；
- 去重功能要求 NTFS 卷；
- Qt 6.5+，本项目在 **Qt 6.8.1 msvc2022_64** 下开发；
- Visual Studio 2022（MSVC x64）；
- CMake 3.21+（本项目使用 3.29.2）。

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.1/msvc2022_64

cmake --build build --config Debug
```

生成的主要目标：

```text
build\Debug\SevenDaysManager.exe
build\Debug\SevenDaysCoreTests.exe
build\Debug\SevenDaysDedupTests.exe
build\Debug\SevenDaysLogTests.exe
```

Release 构建：

```powershell
cmake --build build --config Release
```

可选部署（生成带 Qt 运行库的 `dist\`）：

```powershell
D:\Qt\6.8.1\msvc2022_64\bin\windeployqt.exe --release --no-translations `
  --dir dist build\Release\SevenDaysManager.exe
```

`app.manifest` 已启用长路径感知（`longPathAware`）与 PerMonitorV2 DPI。

## 测试

```powershell
build\Debug\SevenDaysCoreTests.exe
build\Debug\SevenDaysDedupTests.exe
build\Debug\SevenDaysLogTests.exe
```

- `SevenDaysCoreTests`：版本号解析/匹配、存档扫描与 `main.ttw` 版本解析；
- `SevenDaysDedupTests`：硬链接、去重计划/执行/解除、物理占用、备份；
- `SevenDaysLogTests`：版本、`UserDataFolder`、跨版本警告、自动加载日志解析。

去重测试只在临时目录中构造数据，不会修改真实游戏目录。

## 使用流程

1. 打开“设置...”，通过 `QFileDialog` 指定：
   - 版本汇集目录（如 `G:\7DTD_versions`）；
   - 存档目录（默认 `%APPDATA%\7DaysToDie\Saves`）；
   - Steam 安装目录（仅用于导入新版本）；
   - 备份目录。
2. 在“添加游戏版本”分组中，点击“导入 Steam 版本”输入版本标签（如 `V1.4`），复制到 `<版本根>\7 Days To DieV1.4`；或点击“添加已有目录”登记现有完整目录。
3. 在“指定基准版本”分组中，点击“指定基准版本...”，通过文件对话框选择基准目录。工具会校验：
   - 是否为完整游戏目录；
   - 是否为 NTFS；
   - 是否与其他版本同卷；
   - 是否位于 Steam 正在管理的 `steamapps\common` 下（拒绝）。
4. 选中非基准版本，在“去重与校验”分组中点击“去重”，查看干跑结果后确认执行。
5. 在“存档管理”表中选择存档，点击大按钮“从版本开始游戏”。
6. 跨版本启动时按提示备份并强制启动；“指定版本”可为单个存档手动指定游戏版本。

界面中的版本操作按 **添加游戏版本 → 指定基准版本 → 去重与校验 → 删除游戏版本** 的顺序排列，组间用箭头提示流程；每一行操作也可单独使用。“启动版本”和“从版本开始游戏”分别是版本区、存档区的主操作大按钮，用于直接启动选中项。

## 自动加载机制

自动加载已通过真实游戏验证。工具启动游戏时会追加：

```text
-LoadSaveGame=true -GameWorld=<世界名> -GameName=<存档名>
```

说明：

- `-LoadSaveGame` 是布尔开关，不是“存档路径”参数；
- 世界与存档名来自 `Saves\<世界>\<存档>` 目录；
- 游戏启动时会用命令行中的 `GameWorld`/`GameName` 覆盖 `prefs.cfg`，随后自动点击“继续游戏”、选中存档并开始；
- 成功时游戏日志包含：

```text
[LoadSaveGame] Loading existing save game 'MyGame' (world 'Pregen10k1').
```

- 若世界不存在、存档已归档或自动化失败，游戏会停留在主菜单，工具状态栏会提示；
- 可在设置中关闭“自动加载选中的存档”。

## 配置与数据文件

| 内容 | 位置 |
| --- | --- |
| 工具配置 | `%APPDATA%\7DTDManager\config.json` |
| 版本库 | `<版本根>\library.json` |
| 哈希清单 | `<版本根>\.meta\<版本ID>.manifest.json` |
| 默认备份目录 | `<版本根>\backups` |
| 单份备份 | `<备份根>\<世界>\<存档>\<时间戳>\`（含 `backup.json`） |
| 工具日志 | `%APPDATA%\7DTDManager\logs\app.log` |
| 启动日志 | `%APPDATA%\7DTDManager\logs\launch_<版本ID>_<时间戳>.log` |

存档目录默认是 `%APPDATA%\7DaysToDie\Saves`。工具只读扫描并备份，不会在启动时把存档复制到其他位置。

## 默认去重排除规则

```text
Mods/**
Data/Config/**
EasyAntiCheat/**
boot.config
serverconfig.xml
steam_appid.txt
*.log
*.dmp
Logs/**
crashes/**
```

可在设置中修改。排除规则之外的文件才会参与去重。

## 注意事项与已知限制

- **同一 NTFS 卷**：跨卷无法创建硬链接，工具会拒绝。
- **基准必须冻结**：不要将 Steam 正在更新的目录设为基准；游戏写入共享文件会同时影响所有链接版本。
- **跨版本有风险**：存档被新版本重写后，回退旧版本可能损坏存档。默认先备份再强制启动。
- **`main.ttw.ext.bak` 会轮换**：原始版本显示取决于游戏当前保留的备份文件。
- **自定义存档目录**：从游戏代码确认存在 `-userdatafolder=<目录>` 启动参数，但本工具尚未接入该参数；目前自定义存档目录主要用于扫描、备份和启动前检查。游戏实际读写位置以日志中的 `UserDataFolder` 为准，不一致时工具会警告。
- **EAC / 联机**：默认 `-noeac` 适合单机；联机场景是否可用取决于 EAC 与启动方式，需自行验证。
- **自动加载仅客户端**：`-LoadSaveGame` 用于本地客户端，专用服务器不适用。
- **路径与权限**：文件被占用或被安全软件锁定时，去重会报告失败并跳过，不会强行删除。

## 项目结构

```text
CMakeLists.txt            构建定义与三个测试目标
app.manifest              长路径 / DPI 清单
src/
  main.cpp                程序入口、日志转存
  MainWindow.*            主界面：版本表、存档表、启动流程
  SettingsDialog.*        路径、启动参数、备份策略、排除规则
  app/
    AppConfig.*           config.json 读写与默认值
    Library.*             library.json：版本、基准、存档版本覆盖
  core/
    Version.*             版本号解析、规范化、匹配
    PathUtils.*           长路径、卷/NTFS 判断、大小格式化
    FileScanner.*         递归扫描与排除规则
    HashService.*         流式 SHA-256
    HardLink.*            硬链接创建、文件 ID、解除链接
    Deduper.*             去重计划/执行/校验/占用统计
    GameImporter.*        Steam 版本导入与元数据
    SaveScanner.*         存档扫描与 main.ttw 版本解析
    GameLauncher.*        启动游戏与参数构造
    LogWatcher.*          启动日志解析
    BackupService.*       存档备份与保留策略
    TaskRunner.*          QtConcurrent 后台任务、进度与取消
tests/                    核心、去重、日志三组测试
tools/
  metadata_search/        Assembly-CSharp.dll 字符串/元数据扫描工具
  ilspycmd.exe            ILSpy 命令行反编译工具
```

## 当前状态

- 核心引擎、硬链接去重、备份、存档版本解析、日志解析均已通过测试。
- 自动加载已用真实 V1.4 游戏与 `Pregen10k1/MyGame` 存档实测通过。
- 尚未完成：
  - 在 GUI 中完整执行 20.8GB Steam 版本导入、指定基准、去重的端到端验证；
  - `-userdatafolder` 的真实游戏验证与界面接入；
  - Release 打包与无 Qt 环境运行验证。
