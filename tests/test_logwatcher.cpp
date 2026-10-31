#include "core/LogWatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QString logDir =
        QDir(qEnvironmentVariable("APPDATA")).filePath(QStringLiteral("7DaysToDie/logs"));
    QDir dir(logDir);
    const QStringList logs =
        dir.entryList({QStringLiteral("output_log_client__*.txt")}, QDir::Files, QDir::Time);
    if (logs.isEmpty()) {
        out << "SKIP 未找到游戏日志\n";
        return 0;
    }

    const QString logPath = dir.filePath(logs.first());
    out << "log=" << logPath << "\n";

    LogWatcher watcher;
    QString detectedVersion;
    QString userDataFolder;
    bool sawMismatch = false;

    QObject::connect(&watcher, &LogWatcher::versionDetected,
                     [&](const QString &version) { detectedVersion = version; });
    QObject::connect(&watcher, &LogWatcher::userDataFolderDetected,
                     [&](const QString &path) { userDataFolder = path; });
    QObject::connect(&watcher, &LogWatcher::versionMismatchDetected,
                     [&](const QString &) { sawMismatch = true; });

    watcher.start(logPath, 5000);
    QTimer::singleShot(5000, &app, &QCoreApplication::quit);
    app.exec();

    int failures = 0;
    auto check = [&out, &failures](bool condition, const QString &name) {
        out << (condition ? "PASS " : "FAIL ") << name << "\n";
        if (!condition)
            ++failures;
    };

    check(detectedVersion == QStringLiteral("V 1.4 (b8)"),
          QStringLiteral("从日志解析版本（实际 %1）").arg(detectedVersion));
    check(userDataFolder.contains(QStringLiteral("7DaysToDie")),
          QStringLiteral("从日志解析 UserDataFolder（实际 %1）").arg(userDataFolder));
    check(sawMismatch, "识别到跨版本加载警告");

    // 用合成日志验证 -LoadSaveGame 的结论行判定（不依赖真实游戏启动）。
    QTemporaryDir temp;
    const QString syntheticLog = QDir(temp.path()).filePath(QStringLiteral("launch.log"));
    QFile synthetic(syntheticLog);
    if (synthetic.open(QIODevice::WriteOnly)) {
        synthetic.write("2026-10-31T00:50:43 0.100 INF Command line arguments: \"7DaysToDie.exe\" "
                        "-LoadSaveGame=true -GameWorld=Pregen10k1 -GameName=MyGame\n"
                        "2026-10-31T00:50:43 0.115 INF [LoadSaveGame] Loading existing save game "
                        "'MyGame' (world 'Pregen10k1').\n");
        synthetic.close();
    }

    LogWatcher autoWatcher;
    bool autoOk = false;
    QString autoDetail;
    QObject::connect(&autoWatcher, &LogWatcher::autoLoadResult,
                     [&](bool ok, const QString &detail) {
                         autoOk = ok;
                         autoDetail = detail;
                     });
    autoWatcher.start(syntheticLog, 3000);
    QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    app.exec();

    check(autoOk, QStringLiteral("识别自动加载成功（实际 %1）").arg(autoDetail));

    // 合成 Alpha 旧版本日志：解析版本，并识别 A19 不支持 -LoadSaveGame 的警告。
    const QString alphaLog = QDir(temp.path()).filePath(QStringLiteral("alpha.log"));
    QFile alpha(alphaLog);
    if (alpha.open(QIODevice::WriteOnly)) {
        alpha.write("2026-10-31T21:38:00 0.078 INF Version: Alpha 19.6 (b8) "
                    "Compatibility Version: Alpha 19.6, Build: Windows 64 Bit\n"
                    "2026-10-31T21:38:00 6.170 WRN Command line argument 'LoadSaveGame' "
                    "is not a configfile property, ignoring.\n");
        alpha.close();
    }

    LogWatcher alphaWatcher;
    QString alphaVersion;
    bool alphaAutoOk = true;
    QString alphaAutoDetail;
    QObject::connect(&alphaWatcher, &LogWatcher::versionDetected,
                     [&](const QString &version) { alphaVersion = version; });
    QObject::connect(&alphaWatcher, &LogWatcher::autoLoadResult,
                     [&](bool ok, const QString &detail) {
                         alphaAutoOk = ok;
                         alphaAutoDetail = detail;
                     });
    alphaWatcher.start(alphaLog, 3000);
    QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    app.exec();

    check(alphaVersion == QStringLiteral("Alpha 19.6 (b8)"),
          QStringLiteral("解析旧版 Alpha 版本（实际 %1）").arg(alphaVersion));
    check(!alphaAutoOk && alphaAutoDetail.contains(QStringLiteral("不支持")),
          QStringLiteral("识别 -LoadSaveGame 不受支持（实际 %1）").arg(alphaAutoDetail));

    out << (failures == 0 ? "ALL PASS\n" : QStringLiteral("FAILURES=%1\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
