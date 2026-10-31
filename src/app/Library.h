#pragma once

#include <QHash>
#include <QString>
#include <QVector>

// 已登记的某个游戏版本。
struct VersionInfo
{
    QString id;              // 稳定标识，如 "v1.4"
    QString label;           // 用户标签，如 "V1.4"
    QString detectedVersion; // 从日志/存档确认的完整版本，如 "V 1.4 (b8)"
    QString path;            // 版本目录绝对路径
    bool isBase = false;     // 是否为去重基准
    bool volatileSource = false; // 位于 steamapps\common，游戏可能被 Steam 更新
    QString steamBuildId;
    QString steamBetaKey;
    qint64 logicalBytes = 0;
    int fileCount = 0;
    QString manifest;        // 相对 versionRoot 的清单路径

    // 运行时状态，不持久化。
    bool exists = false;
    qint64 physicalBytes = -1;
    QString status;
};

// 版本库，持久化到 <versionRoot>\library.json。
class Library
{
public:
    QString versionRoot;
    QString baseVersionId;
    QVector<VersionInfo> versions;

    // 存档 → 版本的“手动指定”覆盖，键为 "世界/存档名"。
    QHash<QString, QString> saveVersionOverrides;

    // 从版本汇集目录加载 library.json；不存在时返回空库。
    static Library load(const QString &versionRoot, QString *error = nullptr);

    // 保存到 <versionRoot>\library.json。
    bool save(QString *error = nullptr) const;

    static QString libraryPath(const QString &versionRoot);

    VersionInfo *findVersion(const QString &id);
    const VersionInfo *findVersion(const QString &id) const;
    VersionInfo *findByPath(const QString &path);
    const VersionInfo *findByPath(const QString &path) const;
    VersionInfo *baseVersion();
    const VersionInfo *baseVersion() const;

    // 生成唯一版本 id；标签重复时追加序号。
    QString makeId(const QString &label) const;

    // 添加版本；路径重复时拒绝。
    bool addVersion(const VersionInfo &info, QString *error = nullptr);

    // 移除版本记录（不删除磁盘文件）。
    bool removeVersion(const QString &id);

    // 设置基准版本；会同步各版本的 isBase。
    bool setBaseVersion(const QString &id, QString *error = nullptr);

    // 刷新各版本目录是否存在。
    void refreshExistence();

    // 返回存档对应的版本 id（优先手动覆盖），找不到返回空。
    QString resolveSaveVersion(const QString &world, const QString &saveName) const;
};
