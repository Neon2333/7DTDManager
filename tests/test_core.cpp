#include "core/PathUtils.h"
#include "core/SaveScanner.h"
#include "core/Version.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTextStream>

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

    const ParsedVersion version = parseGameVersion(QStringLiteral("V 1.4 (b8)"));
    check(version.isValid() && version.major == 1 && version.minor == 4 && version.build == 8,
          QStringLiteral("解析 V 1.4 (b8)"));
    check(majorMinorVersion(QStringLiteral("V 1.0 (b336)")) == QStringLiteral("1.0"),
          QStringLiteral("主次版本 1.0"));
    check(sameMajorMinor(QStringLiteral("V 1.4 (b7)"), QStringLiteral("V1.4")),
          QStringLiteral("同主次版本匹配"));
    check(!sameMajorMinor(QStringLiteral("V 1.0 (b336)"), QStringLiteral("V 1.4 (b8)")),
          QStringLiteral("不同主次版本不匹配"));
    check(labelFromVersionFolder(QStringLiteral("7 Days To DieV1.4")) == QStringLiteral("V1.4"),
          QStringLiteral("目录名解析标签"));
    check(normalizeLabel(QStringLiteral("1.4")) == QStringLiteral("V1.4"),
          QStringLiteral("标签规范化"));
    check(versionFolderName(QStringLiteral("1.4")) == QStringLiteral("7 Days To DieV1.4"),
          QStringLiteral("版本目录名生成"));

    QTemporaryDir userData;
    QDir(userData.path()).mkpath(QStringLiteral("Saves"));
    check(PathUtils::resolveSavesRoot(userData.path())
              == QDir::cleanPath(QDir(userData.path()).filePath(QStringLiteral("Saves"))),
          QStringLiteral("用户数据目录归一化为 Saves 子目录"));
    const QString plainDir = QDir(userData.path()).filePath(QStringLiteral("plain"));
    QDir().mkpath(plainDir);
    check(PathUtils::resolveSavesRoot(plainDir) == QDir::cleanPath(plainDir),
          QStringLiteral("普通目录保持原样"));

    const QString savesRoot = SaveScanner::defaultSavesRoot();
    out << "savesRoot=" << savesRoot << "\n";
    const SaveScanResult result = SaveScanner::scan(savesRoot);
    if (!result.error.isEmpty())
        out << "scanError=" << result.error << "\n";

    out << "saveCount=" << result.saves.size() << "\n";
    for (const SaveInfo &save : result.saves) {
        out << save.displayName() << " | original=" << save.originalVersion
            << " | last=" << save.lastSavedVersion
            << " | rewritten=" << (save.rewritten ? "true" : "false") << "\n";
    }

    check(!result.saves.isEmpty(), QStringLiteral("扫描到至少一个存档"));

    bool foundRealSave = false;
    for (const SaveInfo &save : result.saves) {
        if (save.world == QStringLiteral("Pregen10k1")
            && save.saveName == QStringLiteral("MyGame")) {
            foundRealSave = true;
            check(save.lastSavedVersion == QStringLiteral("V 1.4 (b8)"),
                  QStringLiteral("Pregen10k1/MyGame 最后保存版本为 V 1.4 (b8)"));
            // .ext.bak 会随游戏正常存档而轮换，因此只校验“已升级”标记与版本是否一致。
            check(save.rewritten
                      == (!save.originalVersion.isEmpty()
                          && save.originalVersion != save.lastSavedVersion),
                  QStringLiteral("Pregen10k1/MyGame 的已升级标记与版本一致（原始 %1）")
                      .arg(save.originalVersion));
        }
    }
    check(foundRealSave, QStringLiteral("存在 Pregen10k1/MyGame 存档"));

    out << (failures == 0 ? "ALL PASS\n" : QStringLiteral("FAILURES=%1\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
