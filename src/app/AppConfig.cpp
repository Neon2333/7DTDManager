#include "AppConfig.h"

#include "core/FileScanner.h"
#include "core/PathUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

// 从 JSON 对象读取字符串字段，缺省时保留原值。
QString readString(const QJsonObject &object, const QString &key, const QString &fallback)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

// 从 JSON 对象读取布尔字段，缺省时保留原值。
bool readBool(const QJsonObject &object, const QString &key, bool fallback)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

} // namespace

AppConfig AppConfig::defaults()
{
    AppConfig config;
    // 首次运行不预设任何本机路径，避免把开发/测试环境的目录带入安装包。
    config.excludes = FileScanner::defaultExcludes();
    return config;
}

QString AppConfig::configPath()
{
    return QDir::toNativeSeparators(
        QDir(PathUtils::appDataDir()).filePath(QStringLiteral("config.json")));
}

AppConfig AppConfig::load(QString *error)
{
    AppConfig config = defaults();

    const QString path = configPath();
    QFile file(path);
    if (!file.exists())
        return config;

    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取配置 %1：%2").arg(path, file.errorString());
        return config;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("配置文件格式错误：%1").arg(parseError.errorString());
        return config;
    }

    const QJsonObject object = document.object();
    config.versionRoot = readString(object, QStringLiteral("versionRoot"), config.versionRoot);
    config.savesRoot = readString(object, QStringLiteral("savesRoot"), config.savesRoot);
    config.saveLibraryRoot =
        readString(object, QStringLiteral("saveLibraryRoot"), config.saveLibraryRoot);
    config.steamRoot = readString(object, QStringLiteral("steamRoot"), config.steamRoot);
    config.useNoEac = readBool(object, QStringLiteral("useNoEac"), config.useNoEac);
    config.skipIntro = readBool(object, QStringLiteral("skipIntro"), config.skipIntro);
    config.autoLoadSave = readBool(object, QStringLiteral("autoLoadSave"), config.autoLoadSave);
    config.extraArgs = readString(object, QStringLiteral("extraArgs"), config.extraArgs);

    const QJsonValue excludesValue = object.value(QStringLiteral("excludes"));
    if (excludesValue.isArray()) {
        QStringList excludes;
        for (const QJsonValue &item : excludesValue.toArray()) {
            if (item.isString())
                excludes << item.toString();
        }
        if (!excludes.isEmpty())
            config.excludes = excludes;
    }

    return config;
}

bool AppConfig::save(QString *error) const
{
    const QString path = configPath();
    if (!PathUtils::ensureDir(QFileInfo(path).absolutePath(), error))
        return false;

    QJsonObject object;
    object.insert(QStringLiteral("versionRoot"), versionRoot);
    object.insert(QStringLiteral("savesRoot"), savesRoot);
    object.insert(QStringLiteral("saveLibraryRoot"), saveLibraryRoot);
    object.insert(QStringLiteral("steamRoot"), steamRoot);
    object.insert(QStringLiteral("useNoEac"), useNoEac);
    object.insert(QStringLiteral("skipIntro"), skipIntro);
    object.insert(QStringLiteral("autoLoadSave"), autoLoadSave);
    object.insert(QStringLiteral("extraArgs"), extraArgs);
    object.insert(QStringLiteral("excludes"), QJsonArray::fromStringList(excludes));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入配置 %1：%2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("保存配置失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}
