#include "core/TaskRunner.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    int failures = 0;
    auto check = [&out, &failures](bool condition, const QString &name) {
        out << (condition ? "PASS " : "FAIL ") << name << "\n";
        if (!condition)
            ++failures;
    };

    TaskRunner runner;
    bool firstFinished = false;
    bool secondFinished = false;

    // 第一个任务结束后立即启动第二个任务，复现“完成后立刻启动统计”的场景。
    QObject::connect(&runner, &TaskRunner::finished, &app,
                     [&](bool ok, const QString &) {
                         if (!firstFinished) {
                             firstFinished = true;
                             check(ok, QStringLiteral("第一个任务成功完成"));
                             runner.start(QStringLiteral("第二个任务"),
                                          [](TaskContext &) {});
                         } else {
                             secondFinished = true;
                             check(ok, QStringLiteral("第二个任务成功完成"));
                             app.quit();
                         }
                     });

    // 防止回归时测试永久等待。
    QTimer::singleShot(5000, &app, [&] {
        out << "FAIL 等待任务完成超时\n";
        ++failures;
        app.quit();
    });

    runner.start(QStringLiteral("第一个任务"), [](TaskContext &) {});
    app.exec();

    check(firstFinished, QStringLiteral("第一个任务的 finished 已触发"));
    check(secondFinished, QStringLiteral("第二个任务的 finished 已触发"));
    check(!runner.isRunning(), QStringLiteral("任务结束后 isRunning 为 false"));

    out << (failures == 0 ? "ALL PASS\n" : QStringLiteral("FAILURES=%1\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
