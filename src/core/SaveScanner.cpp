#include "SaveScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace {

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

    const QFileInfoList worlds = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &worldInfo : worlds) {
        QDir worldDir(worldInfo.absoluteFilePath());
        const QFileInfoList saves = worldDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                           QDir::Name);
        for (const QFileInfo &saveInfo : saves) {
            const QString saveDir = saveInfo.absoluteFilePath();
            const QString ttwPath = QDir(saveDir).filePath(QStringLiteral("main.ttw"));
            const QFileInfo ttwInfo(ttwPath);
            if (!ttwInfo.exists() || !ttwInfo.isFile())
                continue;

            SaveInfo info;
            info.world = worldInfo.fileName();
            info.saveName = saveInfo.fileName();
            info.directory = QDir::toNativeSeparators(saveDir);
            info.lastModified = ttwInfo.lastModified();

            readTtwVersion(ttwPath, &info.lastSavedVersion, nullptr);

            const QString backupPath = ttwPath + QStringLiteral(".ext.bak");
            if (QFileInfo::exists(backupPath))
                readTtwVersion(backupPath, &info.originalVersion, nullptr);

            info.rewritten = !info.originalVersion.isEmpty() && !info.lastSavedVersion.isEmpty()
                             && info.originalVersion != info.lastSavedVersion;

            result.saves.append(info);
        }
    }

    std::sort(result.saves.begin(), result.saves.end(), [](const SaveInfo &a, const SaveInfo &b) {
        return a.lastModified > b.lastModified;
    });

    return result;
}

QString SaveScanner::defaultSavesRoot()
{
    return PathUtils::defaultSavesDir();
}
