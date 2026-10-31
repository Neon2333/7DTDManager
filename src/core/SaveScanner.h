#pragma once

#include "PathUtils.h"

#include <QDateTime>
#include <QString>
#include <QVector>

// 一个七日杀存档（<世界>/<存档名>/main.ttw）。
struct SaveInfo
{
    QString world;
    QString saveName;
    QString directory;

    // main.ttw 中记录的最后保存版本，如 "V 1.4 (b8)"。
    QString lastSavedVersion;

    // main.ttw.ext.bak 中保留的原始版本；不存在时为空。
    QString originalVersion;

    QDateTime lastModified;

    // 存档已被其他版本重写（原始版本与最后保存版本不同）。
    bool rewritten = false;

    QString displayName() const { return world + QLatin1Char('/') + saveName; }
};

// 存档扫描结果。
struct SaveScanResult
{
    QVector<SaveInfo> saves;
    QString error;
};

// 扫描七日杀存档目录并解析版本头。
class SaveScanner
{
public:
    // 读取单个 main.ttw 的版本串。
    static bool readTtwVersion(const QString &ttwPath, QString *version, QString *error = nullptr);

    // 扫描存档根目录下的 <世界>/<存档>/main.ttw。
    static SaveScanResult scan(const QString &savesRoot);

    // 默认存档根目录：%APPDATA%\7DaysToDie\Saves。
    static QString defaultSavesRoot();
};
