#include "GameLauncher.h"

#include "PathUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

LaunchResult GameLauncher::launch(const LaunchRequest &request)
{
    LaunchResult result;

    QDir versionDir(request.versionDir);
    const QString exePath = versionDir.filePath(QStringLiteral("7DaysToDie.exe"));
    if (!QFileInfo::exists(exePath)) {
        result.error = QStringLiteral("未找到游戏可执行文件：%1").arg(exePath);
        return result;
    }

    if (!PathUtils::ensureDir(request.logDir, &result.error))
        return result;

    result.logPath = makeLogPath(request.logDir, request.versionId);

    QStringList arguments;
    arguments << QStringLiteral("-logfile") << result.logPath;

    if (request.noEac)
        arguments << QStringLiteral("-noeac");
    if (request.skipIntro)
        arguments << QStringLiteral("-skipintro");

    // -LoadSaveGame 是布尔开关；世界与存档名分别由 GameWorld/GameName 指定。
    // 游戏启动时会用命令行值覆盖 prefs.cfg，并自动进入“继续游戏”选中该存档。
    if (request.autoLoad && !request.worldName.isEmpty() && !request.saveName.isEmpty()) {
        arguments << QStringLiteral("-LoadSaveGame=true")
                  << QStringLiteral("-GameWorld=%1").arg(request.worldName)
                  << QStringLiteral("-GameName=%1").arg(request.saveName);
    }

    if (!request.extraArgs.trimmed().isEmpty())
        arguments << QProcess::splitCommand(request.extraArgs.trimmed());

    qint64 pid = 0;
    const bool started = QProcess::startDetached(exePath, arguments,
                                                 QDir::toNativeSeparators(request.versionDir),
                                                 &pid);
    if (!started) {
        result.error = QStringLiteral("启动游戏失败：%1").arg(exePath);
        return result;
    }

    result.ok = true;
    result.pid = pid;
    return result;
}

QString GameLauncher::makeLogPath(const QString &logDir, const QString &versionId)
{
    const QString safeId = versionId.isEmpty() ? QStringLiteral("game") : versionId;
    return QDir::toNativeSeparators(QDir(logDir).filePath(
        QStringLiteral("launch_%1_%2.log").arg(safeId, PathUtils::timestamp())));
}
