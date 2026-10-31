#include "MainWindow.h"
#include "core/PathUtils.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>

#include <cstdio>

namespace {

// 将 Qt 日志写入 %APPDATA%\7DTDManager\logs\app.log，同时输出到 stderr。
void fileMessageHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    const QString logDir = QDir(PathUtils::appDataDir()).filePath(QStringLiteral("logs"));
    PathUtils::ensureDir(logDir, nullptr);

    QFile file(QDir(logDir).filePath(QStringLiteral("app.log")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QString line =
            QStringLiteral("[%1] %2\n")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate), message);
        file.write(line.toUtf8());
    }

    std::fprintf(stderr, "%s\n", qUtf8Printable(message));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("7DTDManager"));
    QCoreApplication::setApplicationName(QStringLiteral("SevenDaysManager"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    qInstallMessageHandler(fileMessageHandler);

    MainWindow window;
    window.show();

    return app.exec();
}
