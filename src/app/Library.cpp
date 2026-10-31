#include "Library.h"

#include "core/PathUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace {

// 从 JSON 对象读取字符串字段。
QString readString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

} // namespace

QString Library::libraryPath(const QString &versionRoot)
{
    return QDir::toNativeSeparators(QDir(versionRoot).filePath(QStringLiteral("library.json")));
}

Library Library::load(const QString &versionRoot, QString *error)
{
    Library library;
    library.versionRoot = QDir::toNativeSeparators(versionRoot);

    const QString path = libraryPath(versionRoot);
    QFile file(path);
    if (!file.exists())
        return library;

    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取版本库 %1：%2").arg(path, file.errorString());
        return library;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("版本库格式错误：%1").arg(parseError.errorString());
        return library;
    }

    const QJsonObject object = document.object();
    library.baseVersionId = readString(object, QStringLiteral("baseVersionId"));

    const QJsonArray versions = object.value(QStringLiteral("versions")).toArray();
    for (const QJsonValue &value : versions) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        VersionInfo info;
        info.id = readString(item, QStringLiteral("id"));
        info.label = readString(item, QStringLiteral("label"));
        info.detectedVersion = readString(item, QStringLiteral("detectedVersion"));
        info.path = readString(item, QStringLiteral("path"));
        info.isBase = item.value(QStringLiteral("isBase")).toBool(false);
        info.volatileSource = item.value(QStringLiteral("volatileSource")).toBool(false);
        info.steamBuildId = readString(item, QStringLiteral("steamBuildId"));
        info.steamBetaKey = readString(item, QStringLiteral("steamBetaKey"));
        info.logicalBytes = static_cast<qint64>(item.value(QStringLiteral("logicalBytes")).toDouble(0));
        info.fileCount = item.value(QStringLiteral("fileCount")).toInt(0);
        info.manifest = readString(item, QStringLiteral("manifest"));
        if (!info.id.isEmpty() && !info.path.isEmpty())
            library.versions.append(info);
    }

    const QJsonObject overrides = object.value(QStringLiteral("saveVersionOverrides")).toObject();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (it.value().isString())
            library.saveVersionOverrides.insert(it.key(), it.value().toString());
    }

    if (library.baseVersionId.isEmpty()) {
        for (const VersionInfo &info : library.versions) {
            if (info.isBase) {
                library.baseVersionId = info.id;
                break;
            }
        }
    }

    library.refreshExistence();
    return library;
}

bool Library::save(QString *error) const
{
    if (!PathUtils::ensureDir(versionRoot, error))
        return false;

    QJsonArray versionArray;
    for (const VersionInfo &info : versions) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), info.id);
        item.insert(QStringLiteral("label"), info.label);
        item.insert(QStringLiteral("detectedVersion"), info.detectedVersion);
        item.insert(QStringLiteral("path"), info.path);
        item.insert(QStringLiteral("isBase"), info.id == baseVersionId);
        item.insert(QStringLiteral("volatileSource"), info.volatileSource);
        item.insert(QStringLiteral("steamBuildId"), info.steamBuildId);
        item.insert(QStringLiteral("steamBetaKey"), info.steamBetaKey);
        item.insert(QStringLiteral("logicalBytes"), static_cast<double>(info.logicalBytes));
        item.insert(QStringLiteral("fileCount"), info.fileCount);
        item.insert(QStringLiteral("manifest"), info.manifest);
        versionArray.append(item);
    }

    QJsonObject overrideObject;
    for (auto it = saveVersionOverrides.constBegin(); it != saveVersionOverrides.constEnd(); ++it)
        overrideObject.insert(it.key(), it.value());

    QJsonObject object;
    object.insert(QStringLiteral("schema"), 1);
    object.insert(QStringLiteral("baseVersionId"), baseVersionId);
    object.insert(QStringLiteral("versions"), versionArray);
    object.insert(QStringLiteral("saveVersionOverrides"), overrideObject);

    QSaveFile file(libraryPath(versionRoot));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入版本库：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("保存版本库失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

VersionInfo *Library::findVersion(const QString &id)
{
    for (VersionInfo &info : versions) {
        if (info.id == id)
            return &info;
    }
    return nullptr;
}

const VersionInfo *Library::findVersion(const QString &id) const
{
    for (const VersionInfo &info : versions) {
        if (info.id == id)
            return &info;
    }
    return nullptr;
}

VersionInfo *Library::findByPath(const QString &path)
{
    const QString normalized = QDir::cleanPath(QDir::toNativeSeparators(path));
    for (VersionInfo &info : versions) {
        if (QDir::cleanPath(QDir::toNativeSeparators(info.path)).compare(normalized,
                                                                        Qt::CaseInsensitive)
            == 0)
            return &info;
    }
    return nullptr;
}

const VersionInfo *Library::findByPath(const QString &path) const
{
    const QString normalized = QDir::cleanPath(QDir::toNativeSeparators(path));
    for (const VersionInfo &info : versions) {
        if (QDir::cleanPath(QDir::toNativeSeparators(info.path)).compare(normalized,
                                                                        Qt::CaseInsensitive)
            == 0)
            return &info;
    }
    return nullptr;
}

VersionInfo *Library::baseVersion()
{
    return findVersion(baseVersionId);
}

const VersionInfo *Library::baseVersion() const
{
    return findVersion(baseVersionId);
}

QString Library::makeId(const QString &label) const
{
    QString base = label.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9.]+")), QStringLiteral("-"));
    while (base.startsWith(QLatin1Char('-')))
        base.remove(0, 1);
    while (base.endsWith(QLatin1Char('-')))
        base.chop(1);
    if (base.isEmpty())
        base = QStringLiteral("version");

    QString candidate = base;
    int suffix = 2;
    while (findVersion(candidate))
        candidate = QStringLiteral("%1-%2").arg(base).arg(suffix++);
    return candidate;
}

bool Library::addVersion(const VersionInfo &info, QString *error)
{
    if (info.id.isEmpty() || info.path.isEmpty()) {
        if (error)
            *error = QStringLiteral("版本 id 或路径为空");
        return false;
    }
    if (findVersion(info.id)) {
        if (error)
            *error = QStringLiteral("版本 id 已存在：%1").arg(info.id);
        return false;
    }
    if (findByPath(info.path)) {
        if (error)
            *error = QStringLiteral("该目录已登记：%1").arg(info.path);
        return false;
    }
    versions.append(info);
    return true;
}

bool Library::removeVersion(const QString &id)
{
    for (int i = 0; i < versions.size(); ++i) {
        if (versions.at(i).id == id) {
            versions.removeAt(i);
            if (baseVersionId == id)
                baseVersionId.clear();
            return true;
        }
    }
    return false;
}

bool Library::setBaseVersion(const QString &id, QString *error)
{
    VersionInfo *base = findVersion(id);
    if (!base) {
        if (error)
            *error = QStringLiteral("版本不存在：%1").arg(id);
        return false;
    }
    if (base->volatileSource) {
        if (error)
            *error = QStringLiteral("该目录位于 Steam 管理范围，不能作为基准");
        return false;
    }
    baseVersionId = id;
    for (VersionInfo &info : versions)
        info.isBase = (info.id == id);
    return true;
}

void Library::refreshExistence()
{
    for (VersionInfo &info : versions) {
        info.exists = PathUtils::isDirectory(info.path);
        info.isBase = (info.id == baseVersionId);
    }
}

QString Library::resolveSaveVersion(const QString &world, const QString &saveName) const
{
    const QString key = world + QLatin1Char('/') + saveName;
    return saveVersionOverrides.value(key);
}
