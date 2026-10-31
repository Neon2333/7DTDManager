#pragma once

#include <QString>
#include <QStringList>

// 应用全局配置，持久化到 %APPDATA%\7DTDManager\config.json。
class AppConfig
{
public:
    QString versionRoot;     // 版本汇集目录
    QString savesRoot;       // 游戏存档目录（启动时复制目标）
    QString saveLibraryRoot; // 存档库目录（额外扫描的外部存档目录，可为空）
    QString steamRoot;       // Steam 安装目录（仅导入用）
    QString backupRoot;      // 备份目录

    bool useNoEac = true;        // 启动时加 -noeac
    bool skipIntro = false;      // 启动时加 -skipintro
    bool autoLoadSave = true;    // -LoadSaveGame=true + -GameWorld/-GameName

    // 备份策略："mismatch"（版本不匹配时）、"always"（每次启动前）、"never"。
    QString backupPolicy = QStringLiteral("mismatch");
    int backupKeep = 5;

    QString extraArgs;           // 附加启动参数
    QStringList excludes;        // 去重排除规则

    // 读取配置；文件不存在时返回默认配置。
    static AppConfig load(QString *error = nullptr);

    // 保存配置，使用 QSaveFile 原子写入。
    bool save(QString *error = nullptr) const;

    // 配置文件的完整路径。
    static QString configPath();

    // 应用默认值（用于首次运行）。
    static AppConfig defaults();
};
