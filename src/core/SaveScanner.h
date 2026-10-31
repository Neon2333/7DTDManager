#pragma once

#include "PathUtils.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// 一个七日杀存档（<世界>/<存档名>/main.ttw）。
struct SaveInfo
{
    QString world;
    QString saveName;
    QString directory;

    // 扫描来源根目录，用于区分“游戏存档”和“存档库”。
    QString sourceRoot;

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

// 存档库目录重命名计划：给缺少时间戳后缀的存档目录追加保存时间。
struct SaveRenamePlan
{
    QString fromPath;
    QString toPath;
    QString fromName;
    QString toName;
};

// 扫描七日杀存档目录并解析版本头。
class SaveScanner
{
public:
    // 读取单个 main.ttw 的版本串。
    static bool readTtwVersion(const QString &ttwPath, QString *version, QString *error = nullptr);

    // 递归扫描目录下所有 main.ttw，并向上定位 <存档根>/<世界>/<存档>。
    static SaveScanResult scan(const QString &savesRoot);

    // 递归查找 main.ttw，返回它们向上两级对应的实际存档根。
    static QStringList discoverSavesRoots(const QString &root);

    // 默认存档根目录：%APPDATA%\7DaysToDie\Saves。
    static QString defaultSavesRoot();

    // 目录名是否已带 _yyyyMMdd_HHmmss（可再带 _N）后缀。
    static bool hasTimestampSuffix(const QString &saveName);

    // 去掉末尾的时间戳后缀；没有后缀时返回原名。
    static QString stripTimestampSuffix(const QString &saveName);

    // 生成 baseName_yyyyMMdd_HHmmss 目录名，并避开 existingNames 中的重名。
    static QString timestampedName(const QString &baseName, const QDateTime &when,
                                   const QStringList &existingNames);

    // 递归扫描存档库，列出需要重命名为 7DTDSaves_<版本>_<时间戳> 的存档目录（不修改磁盘）。
    static QVector<SaveRenamePlan> planTimestampRenames(const QString &savesRoot);

    // 执行重命名计划；失败时返回 false 并在 error 中说明。
    static bool applyRenames(const QVector<SaveRenamePlan> &plans, QString *error);
};
