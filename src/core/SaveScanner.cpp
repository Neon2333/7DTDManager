#include "SaveScanner.h"

#include "Version.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

// 时间戳后缀：_yyyyMMdd_HHmmss，允许末尾再带 _2、_3 等去重序号。
const QRegularExpression &timestampSuffixPattern()
{
    static const QRegularExpression pattern(QStringLiteral("_\\d{8}_\\d{6}(?:_\\d+)?$"));
    return pattern;
}

// 递归查找包含 main.ttw 的存档目录；命中后不再深入该目录。
void collectSaveDirectories(const QString &directory, QStringList *saveDirs, int depth = 0)
{
    if (!saveDirs || depth > 32)
        return;

    QDir dir(directory);
    if (QFileInfo(dir.filePath(QStringLiteral("main.ttw"))).isFile()) {
        saveDirs->append(dir.absolutePath());
        return;
    }

    const QFileInfoList children =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : children) {
        const QString name = child.fileName();
        // GeneratedWorlds/SavesLocal 中的 main.ttw 是游戏世界数据，不是玩家存档。
        if (name.compare(QStringLiteral("GeneratedWorlds"), Qt::CaseInsensitive) == 0
            || name.compare(QStringLiteral("SavesLocal"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        collectSaveDirectories(child.absoluteFilePath(), saveDirs, depth + 1);
    }
}

// 由存档目录向上定位世界目录和存档根目录。
void saveAncestors(const QString &saveDir, QDir *worldDir, QDir *savesRoot)
{
    const QFileInfo saveInfo(saveDir);
    *worldDir = saveInfo.dir();
    *savesRoot = QFileInfo(worldDir->absolutePath()).dir();
}

// 是否已经是 7DTDSaves_<版本>_<yyyyMMdd_HHmmss> 形式。
bool isStandardLibraryName(const QString &name)
{
    return name.startsWith(QStringLiteral("7DTDSaves_"), Qt::CaseInsensitive)
           && timestampSuffixPattern().match(name).hasMatch();
}

// 从原始字节中按 ttw 头解析版本串；失败时用正则兜底。
bool parseVersionBytes(const QByteArray &head, QString *version)
{
    // 头格式：'t' 't' 'w' '\0' + int32 + 1 字节长度 + ASCII 版本串。
    if (head.size() >= 9 && head.at(0) == 't' && head.at(1) == 't' && head.at(2) == 'w'
        && head.at(3) == '\0') {
        const int length = static_cast<unsigned char>(head.at(8));
        if (length > 0 && length <= 64 && head.size() >= 9 + length) {
            const QString text = QString::fromLatin1(head.mid(9, length)).trimmed();
            if (!text.isEmpty()) {
                *version = text;
                return true;
            }
        }
    }

    // 兜底：在可打印字符中查找版本模式。
    const QString text = QString::fromLatin1(head);
    static const QRegularExpression re(
        QStringLiteral(R"(V\s*\d+\.\d+(?:\s*\(\s*b\d+\s*\))?)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(text);
    if (match.hasMatch()) {
        *version = match.captured(0).trimmed();
        return true;
    }

    return false;
}

} // namespace

bool SaveScanner::readTtwVersion(const QString &ttwPath, QString *version, QString *error)
{
    if (!version) {
        if (error)
            *error = QStringLiteral("输出参数为空");
        return false;
    }
    version->clear();

    QFile file(ttwPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1：%2").arg(ttwPath, file.errorString());
        return false;
    }

    const QByteArray head = file.read(512);
    if (!parseVersionBytes(head, version)) {
        if (error)
            *error = QStringLiteral("无法从 %1 解析版本").arg(ttwPath);
        return false;
    }
    return true;
}

SaveScanResult SaveScanner::scan(const QString &savesRoot)
{
    SaveScanResult result;

    QDir root(savesRoot);
    if (!root.exists()) {
        result.error = QStringLiteral("存档目录不存在：%1").arg(savesRoot);
        return result;
    }

    QStringList saveDirs;
    collectSaveDirectories(root.absolutePath(), &saveDirs);

    for (const QString &saveDir : saveDirs) {
        const QString ttwPath = QDir(saveDir).filePath(QStringLiteral("main.ttw"));
        const QFileInfo ttwInfo(ttwPath);
        if (!ttwInfo.exists() || !ttwInfo.isFile())
            continue;

        QDir worldDir;
        QDir sourceRoot;
        saveAncestors(saveDir, &worldDir, &sourceRoot);

        SaveInfo info;
        info.world = worldDir.dirName();
        info.saveName = QFileInfo(saveDir).fileName();
        info.directory = QDir::toNativeSeparators(saveDir);
        info.sourceRoot = QDir::toNativeSeparators(sourceRoot.absolutePath());
        info.lastModified = ttwInfo.lastModified();

        readTtwVersion(ttwPath, &info.lastSavedVersion, nullptr);

        const QString backupPath = ttwPath + QStringLiteral(".ext.bak");
        if (QFileInfo::exists(backupPath))
            readTtwVersion(backupPath, &info.originalVersion, nullptr);

        info.rewritten = !info.originalVersion.isEmpty() && !info.lastSavedVersion.isEmpty()
                         && info.originalVersion != info.lastSavedVersion;

        result.saves.append(info);
    }

    std::sort(result.saves.begin(), result.saves.end(), [](const SaveInfo &a, const SaveInfo &b) {
        return a.lastModified > b.lastModified;
    });

    return result;
}

QStringList SaveScanner::discoverSavesRoots(const QString &root)
{
    if (root.isEmpty())
        return {};

    const QString cleanRoot = QDir::cleanPath(root);
    if (!QFileInfo(cleanRoot).isDir())
        return {QDir::toNativeSeparators(cleanRoot)};

    QStringList saveDirs;
    collectSaveDirectories(QDir(cleanRoot).absolutePath(), &saveDirs);

    // 每个 main.ttw 向上两级即为其存档根：<存档根>/<世界>/<存档>/main.ttw。
    QStringList roots;
    QSet<QString> seen;
    for (const QString &saveDir : saveDirs) {
        QDir worldDir;
        QDir savesRoot;
        saveAncestors(saveDir, &worldDir, &savesRoot);
        const QString clean = QDir::cleanPath(savesRoot.absolutePath());
        const QString key = clean.toLower();
        if (clean.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        roots.append(QDir::toNativeSeparators(clean));
    }

    if (!roots.isEmpty())
        return roots;

    // 没有找到 main.ttw 时回退到所选目录；若目录不存在，也交给 scan() 生成错误信息。
    return {QDir::toNativeSeparators(cleanRoot)};
}

QString SaveScanner::defaultSavesRoot()
{
    return PathUtils::defaultSavesDir();
}

bool SaveScanner::hasTimestampSuffix(const QString &saveName)
{
    return timestampSuffixPattern().match(saveName).hasMatch();
}

QString SaveScanner::stripTimestampSuffix(const QString &saveName)
{
    const QRegularExpressionMatch match = timestampSuffixPattern().match(saveName);
    if (!match.hasMatch())
        return saveName;

    const QString base = saveName.left(match.capturedStart());
    return base.isEmpty() ? saveName : base;
}

QString SaveScanner::timestampedName(const QString &baseName, const QDateTime &when,
                                     const QStringList &existingNames)
{
    QSet<QString> existing;
    for (const QString &name : existingNames)
        existing.insert(name.toLower());

    const QDateTime stampTime = when.isValid() ? when : QDateTime::currentDateTime();
    const QString suffix = stampTime.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString candidate = QStringLiteral("%1_%2").arg(baseName, suffix);
    int sequence = 2;
    while (existing.contains(candidate.toLower())) {
        candidate = QStringLiteral("%1_%2_%3").arg(baseName, suffix).arg(sequence++);
    }
    return candidate;
}

QVector<SaveRenamePlan> SaveScanner::planTimestampRenames(const QString &savesRoot)
{
    QVector<SaveRenamePlan> plans;

    QDir root(savesRoot);
    if (!root.exists())
        return plans;

    QStringList saveDirs;
    collectSaveDirectories(root.absolutePath(), &saveDirs);

    QHash<QString, QStringList> existingByWorld;
    for (const QString &saveDir : saveDirs) {
        const QFileInfo saveInfo(saveDir);
        const QDir worldDir = saveInfo.dir();
        const QString name = saveInfo.fileName();
        if (isStandardLibraryName(name))
            continue;

        const QString ttwPath = QDir(saveDir).filePath(QStringLiteral("main.ttw"));
        const QFileInfo ttwInfo(ttwPath);
        if (!ttwInfo.isFile())
            continue;

        QString versionText;
        readTtwVersion(ttwPath, &versionText, nullptr);
        const QString majorMinor = majorMinorVersion(versionText);
        const QString versionLabel =
            majorMinor.isEmpty() ? QStringLiteral("Unknown")
                                 : QStringLiteral("V%1").arg(majorMinor);

        QDateTime when = ttwInfo.lastModified();
        if (!when.isValid())
            when = saveInfo.lastModified();

        const QString worldKey = worldDir.absolutePath();
        QStringList &existingNames = existingByWorld[worldKey];
        if (existingNames.isEmpty()) {
            const QFileInfoList existing =
                worldDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &item : existing)
                existingNames << item.fileName();
        }

        const QString baseName = QStringLiteral("7DTDSaves_%1").arg(versionLabel);
        const QString newName = timestampedName(baseName, when, existingNames);
        existingNames << newName;
        plans.append({saveDir, worldDir.filePath(newName), name, newName});
    }

    return plans;
}

bool SaveScanner::applyRenames(const QVector<SaveRenamePlan> &plans, QString *error)
{
    for (const SaveRenamePlan &plan : plans) {
        if (QFileInfo::exists(plan.toPath)) {
            if (error) {
                *error = QStringLiteral("目标目录已存在，无法重命名：%1").arg(plan.toPath);
            }
            return false;
        }
        if (!QDir().rename(plan.fromPath, plan.toPath)) {
            if (error) {
                *error = QStringLiteral("重命名失败：%1 → %2")
                             .arg(plan.fromPath, plan.toPath);
            }
            return false;
        }
    }
    return true;
}
