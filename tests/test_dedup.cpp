#include "core/BackupService.h"
#include "core/Deduper.h"
#include "core/FileScanner.h"
#include "core/HardLink.h"
#include "core/PathUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

// 写入指定内容的文件，必要时创建父目录。
bool writeFile(const QString &path, const QByteArray &content)
{
    if (!PathUtils::ensureDir(QFileInfo(path).absolutePath(), nullptr))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(content) == content.size();
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

    QTemporaryDir temp;
    if (!temp.isValid()) {
        out << "FAIL 无法创建临时目录\n";
        return 1;
    }

    const QString root = temp.path();
    const QString baseDir = QDir(root).filePath(QStringLiteral("base"));
    const QString sourceDir = QDir(root).filePath(QStringLiteral("source"));
    const QString metaDir = QDir(root).filePath(QStringLiteral(".meta"));

    const QByteArray big1(1024 * 1024, 'A');
    const QByteArray big2(1024 * 1024, 'B');
    const QByteArray big3(1024 * 1024, 'C');
    const QByteArray big4(1024 * 1024, 'D');

    check(writeFile(QDir(baseDir).filePath(QStringLiteral("same.dat")), big1), "写入 base same.dat");
    check(writeFile(QDir(baseDir).filePath(QStringLiteral("same2.dat")), big2), "写入 base same2.dat");
    check(writeFile(QDir(baseDir).filePath(QStringLiteral("diff.dat")), big3), "写入 base diff.dat");
    check(writeFile(QDir(baseDir).filePath(QStringLiteral("Mods/mod.dat")), big4),
          "写入 base Mods/mod.dat");

    check(writeFile(QDir(sourceDir).filePath(QStringLiteral("same.dat")), big1),
          "写入 source same.dat");
    check(writeFile(QDir(sourceDir).filePath(QStringLiteral("same2.dat")), big2),
          "写入 source same2.dat");
    check(writeFile(QDir(sourceDir).filePath(QStringLiteral("diff.dat")), big4),
          "写入 source diff.dat");
    check(writeFile(QDir(sourceDir).filePath(QStringLiteral("Mods/mod.dat")), big4),
          "写入 source Mods/mod.dat");

    QString error;
    check(PathUtils::isNtfs(root, &error), QStringLiteral("临时目录为 NTFS（%1）").arg(error));
    check(PathUtils::sameVolume(baseDir, sourceDir, &error), "base 与 source 同卷");

    DedupOptions options;
    options.excludes = FileScanner::defaultExcludes();
    options.baseManifestPath = QDir(metaDir).filePath(QStringLiteral("base.manifest.json"));
    options.sourceManifestPath = QDir(metaDir).filePath(QStringLiteral("source.manifest.json"));

    const DedupPlan plan = Deduper::plan(sourceDir, baseDir, options);
    check(plan.error.isEmpty(), QStringLiteral("去重分析无错误：%1").arg(plan.error));
    check(plan.sameCount == 2, QStringLiteral("可共享文件数为 2（实际 %1）").arg(plan.sameCount));
    check(plan.differentCount == 1,
          QStringLiteral("内容不同文件数为 1（实际 %1）").arg(plan.differentCount));
    check(plan.savingsBytes == 2 * 1024 * 1024,
          QStringLiteral("预计节省 2MB（实际 %1）").arg(plan.savingsBytes));
    check(QFileInfo::exists(options.baseManifestPath), "已生成基准清单");

    const DedupApplyResult applyResult = Deduper::apply(plan, options);
    check(applyResult.ok, QStringLiteral("去重执行成功：%1").arg(applyResult.errors.join("; ")));
    check(applyResult.linkedCount == 2, "成功建立 2 个硬链接");

    FileIdentity sourceSameId;
    FileIdentity baseSameId;
    check(queryFileIdentity(QDir(sourceDir).filePath(QStringLiteral("same.dat")), &sourceSameId)
              && queryFileIdentity(QDir(baseDir).filePath(QStringLiteral("same.dat")), &baseSameId),
          "读取相同文件身份");
    check(sourceSameId.key() == baseSameId.key(), "去重后 same.dat 与基准为同一文件");
    check(sourceSameId.linkCount == 2,
          QStringLiteral("same.dat 链接数为 2（实际 %1）").arg(sourceSameId.linkCount));

    FileIdentity sourceDiffId;
    check(queryFileIdentity(QDir(sourceDir).filePath(QStringLiteral("diff.dat")), &sourceDiffId),
          "读取 diff.dat 身份");
    check(sourceDiffId.key() != baseSameId.key(), "diff.dat 未被误链接");

    FileIdentity sourceModId;
    FileIdentity baseModId;
    queryFileIdentity(QDir(sourceDir).filePath(QStringLiteral("Mods/mod.dat")), &sourceModId);
    queryFileIdentity(QDir(baseDir).filePath(QStringLiteral("Mods/mod.dat")), &baseModId);
    check(sourceModId.key() != baseModId.key(), "Mods 被排除，未参与去重");

    int entryCount = 0;
    const QVector<ManifestDrift> drifts =
        Deduper::verifyManifest(baseDir, options.baseManifestPath, &entryCount);
    check(entryCount >= 3, QStringLiteral("基准清单条目数 %1").arg(entryCount));
    check(drifts.isEmpty(), "基准清单无漂移");

    const UsageStats usage =
        Deduper::calculateUsage({baseDir, sourceDir}, nullptr, {});
    check(usage.logicalBytes == 8 * 1024 * 1024,
          QStringLiteral("逻辑合计 8MB（实际 %1）").arg(usage.logicalBytes));
    check(usage.physicalBytes < usage.logicalBytes,
          QStringLiteral("物理占用 %1 小于逻辑 %2")
              .arg(usage.physicalBytes)
              .arg(usage.logicalBytes));
    check(usage.physicalBytes == 6 * 1024 * 1024,
          QStringLiteral("物理占用 6MB（实际 %1）").arg(usage.physicalBytes));

    int broken = 0;
    check(Deduper::breakAllLinks(sourceDir, nullptr, {}, &broken, &error),
          QStringLiteral("解除链接成功：%1").arg(error));
    check(broken == 2, QStringLiteral("解除 2 个链接（实际 %1）").arg(broken));

    FileIdentity afterBreakId;
    queryFileIdentity(QDir(sourceDir).filePath(QStringLiteral("same.dat")), &afterBreakId);
    check(afterBreakId.linkCount == 1,
          QStringLiteral("解除后链接数为 1（实际 %1）").arg(afterBreakId.linkCount));

    const UsageStats usageAfter =
        Deduper::calculateUsage({baseDir, sourceDir}, nullptr, {});
    check(usageAfter.physicalBytes == usageAfter.logicalBytes, "解除链接后物理占用等于逻辑大小");

    const QString backupRoot = QDir(root).filePath(QStringLiteral("backups"));
    const BackupResult backup = BackupService::backupSave(sourceDir, backupRoot,
                                                          QStringLiteral("World"),
                                                          QStringLiteral("Save"),
                                                          QStringLiteral("V1.4"), nullptr, {});
    check(backup.ok, QStringLiteral("备份成功：%1").arg(backup.error));
    check(QFileInfo::exists(QDir(backup.path).filePath(QStringLiteral("backup.json"))),
          "备份元数据存在");
    check(QFileInfo::exists(QDir(backup.path).filePath(QStringLiteral("same.dat"))),
          "备份包含存档文件");

    out << (failures == 0 ? "ALL PASS\n" : QStringLiteral("FAILURES=%1\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
