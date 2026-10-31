#include "core/SaveInstaller.h"
#include "core/SaveScanner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <atomic>

namespace {

// 写入测试文件，自动创建父目录。
bool writeFile(const QString &path, const QByteArray &content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

// 创建最小存档目录：带可解析版本头的 main.ttw + 子目录文件。
bool createSave(const QString &directory,
                const QString &version = QStringLiteral("V 1.4 (b8)"))
{
    const QByteArray versionBytes = version.toLatin1();
    QByteArray ttw("ttw\0", 4);
    ttw.append(QByteArray(4, '\0'));
    ttw.append(static_cast<char>(versionBytes.size()));
    ttw.append(versionBytes);

    return writeFile(QDir(directory).filePath(QStringLiteral("main.ttw")), ttw)
           && writeFile(QDir(directory).filePath(QStringLiteral("sub/data.bin")),
                        QByteArray("payload", 7));
}

} // namespace

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

    check(SaveScanner::hasTimestampSuffix(QStringLiteral("MyGame_20261031_143012")),
          QStringLiteral("识别时间戳后缀"));
    check(SaveScanner::hasTimestampSuffix(QStringLiteral("MyGame_20261031_143012_2")),
          QStringLiteral("识别带序号的时间戳后缀"));
    check(!SaveScanner::hasTimestampSuffix(QStringLiteral("MyGame")),
          QStringLiteral("普通名称无时间戳后缀"));
    check(SaveScanner::stripTimestampSuffix(QStringLiteral("MyGame_20261031_143012"))
              == QStringLiteral("MyGame"),
          QStringLiteral("去掉时间戳后缀"));
    check(SaveScanner::timestampedName(
              QStringLiteral("MyGame"),
              QDateTime(QDate(2026, 10, 31), QTime(14, 30, 12)),
              {QStringLiteral("MyGame_20261031_143012")})
              == QStringLiteral("MyGame_20261031_143012_2"),
          QStringLiteral("重名时追加序号"));

    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("创建临时目录"));
    if (!temp.isValid()) {
        out << "FAILURES=1\n";
        return 1;
    }

    const QString root = temp.path();
    const QString source = QDir(root).filePath(QStringLiteral("library/World/MyGame"));
    check(createSave(source), QStringLiteral("创建源存档"));

    // 复制到空目标目录。
    const QString dest = QDir(root).filePath(QStringLiteral("game/World/MyGame"));
    SaveInstallResult result = SaveInstaller::install(source, dest);
    check(result.ok, QStringLiteral("复制到空目标成功"));
    check(QFileInfo::exists(QDir(dest).filePath(QStringLiteral("main.ttw"))),
          QStringLiteral("目标包含 main.ttw"));
    check(QFileInfo::exists(QDir(dest).filePath(QStringLiteral("sub/data.bin"))),
          QStringLiteral("目标包含子目录文件"));

    // 覆盖已有目标目录，旧文件应被替换。
    check(writeFile(QDir(dest).filePath(QStringLiteral("stale.txt")),
                    QByteArray("old")),
          QStringLiteral("写入旧目标文件"));
    result = SaveInstaller::install(source, dest);
    check(result.ok, QStringLiteral("覆盖已有目标成功"));
    check(!QFileInfo::exists(QDir(dest).filePath(QStringLiteral("stale.txt"))),
          QStringLiteral("覆盖后旧文件已删除"));
    check(QFileInfo::exists(QDir(dest).filePath(QStringLiteral("main.ttw"))),
          QStringLiteral("覆盖后新文件存在"));

    const QStringList leftovers =
        QDir(QFileInfo(dest).absolutePath())
            .entryList({QStringLiteral(".*installing*"), QStringLiteral(".*replaced*")},
                       QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    check(leftovers.isEmpty(), QStringLiteral("未残留临时目录"));

    // 取消时目标目录保持原样。
    const QString cancelDest = QDir(root).filePath(QStringLiteral("game/World/Cancel"));
    check(writeFile(QDir(cancelDest).filePath(QStringLiteral("keep.txt")),
                    QByteArray("keep")),
          QStringLiteral("创建取消测试目标"));
    std::atomic_bool cancel{true};
    result = SaveInstaller::install(source, cancelDest, &cancel);
    check(result.canceled && !result.ok, QStringLiteral("取消标记生效"));
    check(QFileInfo::exists(QDir(cancelDest).filePath(QStringLiteral("keep.txt"))),
          QStringLiteral("取消后目标未被修改"));

    // 扫描存档库并生成时间戳重命名计划。
    check(createSave(QDir(root).filePath(QStringLiteral("library/World/Another"))),
          QStringLiteral("创建第二个源存档"));
    const QVector<SaveRenamePlan> plans =
        SaveScanner::planTimestampRenames(QDir(root).filePath(QStringLiteral("library")));
    check(plans.size() == 2, QStringLiteral("生成两个重命名计划"));
    bool namesOk = true;
    for (const SaveRenamePlan &plan : plans) {
        if (!SaveScanner::hasTimestampSuffix(plan.toName)
            || !plan.toName.startsWith(QStringLiteral("7DTDSaves_V1.4_"))) {
            namesOk = false;
        }
    }
    check(namesOk, QStringLiteral("重命名目标为 7DTDSaves_版本_时间戳"));

    QString renameError;
    check(SaveScanner::applyRenames(plans, &renameError),
          QStringLiteral("执行重命名成功：%1").arg(renameError));
    for (const SaveRenamePlan &plan : plans) {
        check(!QFileInfo::exists(plan.fromPath) && QFileInfo::exists(plan.toPath),
              QStringLiteral("目录已重命名：%1").arg(plan.toName));
    }

    // 存档库父目录中包含多个 <版本目录>/Saves 时，应发现两个实际存档根。
    const QString discoverRoot = QDir(root).filePath(QStringLiteral("discover"));
    const QString versionA = QDir(discoverRoot).filePath(QStringLiteral("7DaysToDie"));
    const QString versionB = QDir(discoverRoot).filePath(QStringLiteral("7DaysToDieV2.6"));
    const QString savesA = QDir(versionA).filePath(QStringLiteral("Saves"));
    const QString savesB = QDir(versionB).filePath(QStringLiteral("Saves"));
    check(createSave(QDir(savesA).filePath(QStringLiteral("WorldA/SaveA"))),
          QStringLiteral("创建版本 A 存档"));
    check(createSave(QDir(savesB).filePath(QStringLiteral("WorldB/SaveB"))),
          QStringLiteral("创建版本 B 存档"));
    check(writeFile(QDir(versionA).filePath(QStringLiteral("GeneratedWorlds/GenWorld/main.ttw")),
                    QByteArray("ttw\0gen", 7)),
          QStringLiteral("创建生成世界数据"));

    const QStringList discovered = SaveScanner::discoverSavesRoots(discoverRoot);
    QStringList discoveredClean;
    for (const QString &path : discovered)
        discoveredClean << QDir::cleanPath(path);
    check(discoveredClean.size() == 2,
          QStringLiteral("发现两个版本存档根"));
    check(discoveredClean.contains(QDir::cleanPath(savesA))
              && discoveredClean.contains(QDir::cleanPath(savesB)),
          QStringLiteral("存档根指向各版本的 Saves"));
    check(!discoveredClean.contains(QDir::cleanPath(versionA)),
          QStringLiteral("未把版本根目录当成存档根"));

    const QStringList fromVersion = SaveScanner::discoverSavesRoots(versionA);
    check(fromVersion.size() == 1
              && QDir::cleanPath(fromVersion.first()) == QDir::cleanPath(savesA),
          QStringLiteral("从用户数据根识别 Saves"));

    const QStringList fromSavesRoot = SaveScanner::discoverSavesRoots(savesA);
    check(fromSavesRoot.size() == 1
              && QDir::cleanPath(fromSavesRoot.first()) == QDir::cleanPath(savesA),
          QStringLiteral("普通存档根返回自身"));

    const SaveScanResult scanA = SaveScanner::scan(savesA);
    check(scanA.saves.size() == 1 && scanA.saves.first().world == QStringLiteral("WorldA"),
          QStringLiteral("扫描版本 A 的 Saves"));

    QVector<SaveRenamePlan> nestedPlans;
    for (const QString &path : discovered)
        nestedPlans += SaveScanner::planTimestampRenames(path);
    check(nestedPlans.size() == 2,
          QStringLiteral("汇总多个版本存档根的重命名计划"));

    // 扁平版本目录：<库根>/SavesV1.4/<世界>/<存档>/main.ttw。
    const QString flatRoot = QDir(root).filePath(QStringLiteral("flat"));
    const QString flatA = QDir(flatRoot).filePath(QStringLiteral("SavesV1.4"));
    const QString flatB = QDir(flatRoot).filePath(QStringLiteral("SavesV2.6"));
    check(createSave(QDir(flatA).filePath(QStringLiteral("Pregen10k1/MyGame")),
                     QStringLiteral("V 1.4 (b8)")),
          QStringLiteral("创建扁平版本 A 存档"));
    check(createSave(QDir(flatB).filePath(QStringLiteral("Navezgane_UserCopy/wonderland")),
                     QStringLiteral("V 2.6 (b8)")),
          QStringLiteral("创建扁平版本 B 存档"));
    check(createSave(QDir(flatB).filePath(QStringLiteral("Pregen08k01/WorldEditor")),
                     QStringLiteral("V 2.6 (b8)")),
          QStringLiteral("创建扁平版本 B 第二个存档"));

    const QStringList flatRoots = SaveScanner::discoverSavesRoots(flatRoot);
    QStringList flatRootsClean;
    for (const QString &path : flatRoots)
        flatRootsClean << QDir::cleanPath(path);
    check(flatRootsClean.size() == 2, QStringLiteral("扁平版本目录发现两个存档根"));
    check(flatRootsClean.contains(QDir::cleanPath(flatA))
              && flatRootsClean.contains(QDir::cleanPath(flatB)),
          QStringLiteral("扁平版本存档根指向 SavesV1.4/SavesV2.6"));

    const SaveScanResult flatScan = SaveScanner::scan(flatRoot);
    check(flatScan.saves.size() == 3,
          QStringLiteral("递归扫描扁平版本目录得到三个存档"));

    QVector<SaveRenamePlan> flatPlans;
    for (const QString &path : flatRoots)
        flatPlans += SaveScanner::planTimestampRenames(path);
    int v14Plans = 0;
    int v26Plans = 0;
    bool flatNamesOk = true;
    for (const SaveRenamePlan &plan : flatPlans) {
        if (!SaveScanner::hasTimestampSuffix(plan.toName))
            flatNamesOk = false;
        if (plan.toName.startsWith(QStringLiteral("7DTDSaves_V1.4_")))
            ++v14Plans;
        else if (plan.toName.startsWith(QStringLiteral("7DTDSaves_V2.6_")))
            ++v26Plans;
    }
    check(flatPlans.size() == 3 && flatNamesOk && v14Plans == 1 && v26Plans == 2,
          QStringLiteral("按存档版本生成重命名计划"));

    // 同一根目录混合两种结构：
    // 1) <根>/<目录1>/<世界>/<存档>/main.ttw
    // 2) <根>/<版本目录>/Saves/<世界>/<存档>/main.ttw
    const QString mixedRoot = QDir(root).filePath(QStringLiteral("mixed"));
    const QString mixedFlatRoot = QDir(mixedRoot).filePath(QStringLiteral("SavesV1.4"));
    const QString mixedNestedRoot =
        QDir(mixedRoot).filePath(QStringLiteral("7DaysToDieV2.6/Saves"));
    check(createSave(QDir(mixedFlatRoot).filePath(QStringLiteral("Pregen10k1/MyGame")),
                     QStringLiteral("V 1.4 (b8)")),
          QStringLiteral("创建混合结构一存档"));
    check(createSave(QDir(mixedNestedRoot).filePath(QStringLiteral("Pregen08k01/WorldEditor")),
                     QStringLiteral("V 2.6 (b8)")),
          QStringLiteral("创建混合结构二存档"));

    const QStringList mixedRoots = SaveScanner::discoverSavesRoots(mixedRoot);
    QStringList mixedRootsClean;
    for (const QString &path : mixedRoots)
        mixedRootsClean << QDir::cleanPath(path);
    check(mixedRootsClean.size() == 2, QStringLiteral("混合结构发现两个存档根"));
    check(mixedRootsClean.contains(QDir::cleanPath(mixedFlatRoot))
              && mixedRootsClean.contains(QDir::cleanPath(mixedNestedRoot)),
          QStringLiteral("混合结构同时识别目录1和版本目录/Saves"));

    const SaveScanResult mixedScan = SaveScanner::scan(mixedRoot);
    check(mixedScan.saves.size() == 2,
          QStringLiteral("混合结构递归扫描得到两个存档"));

    out << (failures == 0 ? "ALL PASS\n" : QStringLiteral("FAILURES=%1\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
