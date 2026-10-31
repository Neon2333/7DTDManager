#include "SettingsDialog.h"

#include "core/GameImporter.h"
#include "core/PathUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const AppConfig &config, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
{
    setWindowTitle(QStringLiteral("设置"));
    resize(760, 620);

    auto *mainLayout = new QVBoxLayout(this);

    auto *pathGroup = new QGroupBox(QStringLiteral("目录"), this);
    auto *pathForm = new QFormLayout(pathGroup);
    pathForm->addRow(QStringLiteral("版本汇集目录："),
                     createPathRow(&m_versionRootEdit, QStringLiteral("选择版本汇集目录"),
                                   config.versionRoot));
    pathForm->addRow(QStringLiteral("游戏存档目录："),
                     createPathRow(&m_savesRootEdit, QStringLiteral("选择游戏存档目录"),
                                   config.savesRoot));
    pathForm->addRow(QStringLiteral("存档库目录（可为空）："),
                     createPathRow(&m_saveLibraryRootEdit,
                                   QStringLiteral("选择存档库目录"),
                                   config.saveLibraryRoot));
    pathForm->addRow(QStringLiteral("Steam 安装目录："),
                     createPathRow(&m_steamRootEdit,
                                   QStringLiteral("选择 Steam 游戏安装目录（仅用于导入）"),
                                   config.steamRoot));
    pathForm->addRow(QStringLiteral("备份目录："),
                     createPathRow(&m_backupRootEdit, QStringLiteral("选择备份目录"),
                                   config.backupRoot));
    mainLayout->addWidget(pathGroup);

    auto *launchGroup = new QGroupBox(QStringLiteral("启动"), this);
    auto *launchLayout = new QVBoxLayout(launchGroup);
    m_noEacCheck = new QCheckBox(QStringLiteral("使用 -noeac 启动（单机推荐）"), launchGroup);
    m_noEacCheck->setChecked(config.useNoEac);
    m_noEacCheck->setToolTip(QStringLiteral(
        "启动时追加 -noeac；单机推荐，联机是否可用需自行验证。"));
    m_skipIntroCheck = new QCheckBox(QStringLiteral("使用 -skipintro 跳过开场动画"), launchGroup);
    m_skipIntroCheck->setChecked(config.skipIntro);
    m_skipIntroCheck->setToolTip(QStringLiteral(
        "启动时追加 -skipintro，跳过开场动画。"));
    m_autoLoadCheck = new QCheckBox(QStringLiteral("自动加载选中的存档（-LoadSaveGame）"), launchGroup);
    m_autoLoadCheck->setChecked(config.autoLoadSave);
    m_autoLoadCheck->setToolTip(QStringLiteral(
        "启动时追加 -LoadSaveGame=true -GameWorld=<世界> -GameName=<存档>，"
        "游戏会自动进入继续游戏并选中该存档。"));

    launchLayout->addWidget(m_noEacCheck);
    launchLayout->addWidget(m_skipIntroCheck);
    launchLayout->addWidget(m_autoLoadCheck);

    auto *argsLayout = new QHBoxLayout();
    argsLayout->addWidget(new QLabel(QStringLiteral("附加启动参数："), launchGroup));
    m_extraArgsEdit = new QLineEdit(config.extraArgs, launchGroup);
    argsLayout->addWidget(m_extraArgsEdit, 1);
    launchLayout->addLayout(argsLayout);
    mainLayout->addWidget(launchGroup);

    auto *backupGroup = new QGroupBox(QStringLiteral("备份策略"), this);
    auto *backupForm = new QFormLayout(backupGroup);
    m_backupPolicyCombo = new QComboBox(backupGroup);
    m_backupPolicyCombo->addItem(QStringLiteral("仅存档版本与游戏版本不匹配时"),
                                 QStringLiteral("mismatch"));
    m_backupPolicyCombo->addItem(QStringLiteral("每次启动前"), QStringLiteral("always"));
    m_backupPolicyCombo->addItem(QStringLiteral("从不备份"), QStringLiteral("never"));
    const int policyIndex = m_backupPolicyCombo->findData(config.backupPolicy);
    m_backupPolicyCombo->setCurrentIndex(policyIndex >= 0 ? policyIndex : 0);
    backupForm->addRow(QStringLiteral("启动前备份："), m_backupPolicyCombo);

    m_backupKeepSpin = new QSpinBox(backupGroup);
    m_backupKeepSpin->setRange(1, 50);
    m_backupKeepSpin->setValue(qBound(1, config.backupKeep, 50));
    backupForm->addRow(QStringLiteral("每个存档保留份数："), m_backupKeepSpin);
    mainLayout->addWidget(backupGroup);

    auto *excludeGroup = new QGroupBox(QStringLiteral("去重排除规则（每行一条）"), this);
    auto *excludeLayout = new QVBoxLayout(excludeGroup);
    m_excludesEdit = new QPlainTextEdit(excludeGroup);
    m_excludesEdit->setPlainText(config.excludes.join(QLatin1Char('\n')));
    excludeLayout->addWidget(m_excludesEdit);
    mainLayout->addWidget(excludeGroup, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok))
        okButton->setToolTip(QStringLiteral("保存设置并关闭对话框。"));
    if (QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel))
        cancelButton->setToolTip(QStringLiteral("放弃本次修改并关闭对话框。"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

QWidget *SettingsDialog::createPathRow(QLineEdit **editOut, const QString &dialogTitle,
                                       const QString &initialPath)
{
    auto *container = new QWidget(this);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *edit = new QLineEdit(initialPath, container);
    auto *browse = new QPushButton(QStringLiteral("浏览..."), container);
    browse->setAutoDefault(false);
    browse->setToolTip(dialogTitle);

    connect(browse, &QPushButton::clicked, this, [this, edit, dialogTitle] {
        const QString initial = edit->text().isEmpty() ? m_config.versionRoot : edit->text();
        const QString chosen = QFileDialog::getExistingDirectory(
            this, dialogTitle, initial, QFileDialog::ShowDirsOnly);
        if (!chosen.isEmpty())
            edit->setText(QDir::toNativeSeparators(chosen));
    });

    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    *editOut = edit;
    return container;
}

void SettingsDialog::accept()
{
    const QString versionRoot = QDir::toNativeSeparators(m_versionRootEdit->text().trimmed());
    const QString savesRoot = QDir::toNativeSeparators(m_savesRootEdit->text().trimmed());
    const QString saveLibraryRoot =
        QDir::toNativeSeparators(m_saveLibraryRootEdit->text().trimmed());
    const QString steamRoot = QDir::toNativeSeparators(m_steamRootEdit->text().trimmed());
    QString backupRoot = QDir::toNativeSeparators(m_backupRootEdit->text().trimmed());

    if (versionRoot.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("设置"), QStringLiteral("版本汇集目录不能为空。"));
        return;
    }

    if (backupRoot.isEmpty())
        backupRoot = QDir(versionRoot).filePath(QStringLiteral("backups"));

    if (savesRoot.isEmpty() || !QFileInfo(savesRoot).isDir()) {
        QMessageBox::warning(this, QStringLiteral("设置"),
                             QStringLiteral("游戏存档目录不存在，请选择一个已存在的目录。"));
        return;
    }

    if (!saveLibraryRoot.isEmpty() && !QFileInfo(saveLibraryRoot).isDir()) {
        QMessageBox::warning(this, QStringLiteral("设置"),
                             QStringLiteral("存档库目录不存在，请选择一个已存在的目录，"
                                            "或留空。"));
        return;
    }

    if (!steamRoot.isEmpty() && !GameImporter::validateSource(steamRoot)) {
        QMessageBox::warning(this, QStringLiteral("设置"),
                             QStringLiteral("Steam 安装目录不是完整的七日杀目录（需要 "
                                            "7DaysToDie.exe、7DaysToDie_Data、Data）。"));
        return;
    }

    QString error;
    if (!PathUtils::ensureDir(versionRoot, &error)) {
        QMessageBox::warning(this, QStringLiteral("设置"), error);
        return;
    }
    if (!PathUtils::ensureDir(backupRoot, &error)) {
        QMessageBox::warning(this, QStringLiteral("设置"), error);
        return;
    }

    if (!PathUtils::isNtfs(versionRoot, &error)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("设置"),
            QStringLiteral("%1\n\n非 NTFS 卷无法使用硬链接去重，是否仍然保存？").arg(error),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    m_config.versionRoot = versionRoot;
    m_config.savesRoot = savesRoot;
    m_config.saveLibraryRoot = saveLibraryRoot;
    m_config.steamRoot = steamRoot;
    m_config.backupRoot = backupRoot;
    m_config.useNoEac = m_noEacCheck->isChecked();
    m_config.skipIntro = m_skipIntroCheck->isChecked();
    m_config.autoLoadSave = m_autoLoadCheck->isChecked();
    m_config.backupPolicy = m_backupPolicyCombo->currentData().toString();
    m_config.backupKeep = m_backupKeepSpin->value();
    m_config.extraArgs = m_extraArgsEdit->text().trimmed();

    QStringList excludes;
    const QStringList lines = m_excludesEdit->toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            excludes << trimmed;
    }
    if (!excludes.isEmpty())
        m_config.excludes = excludes;

    if (!m_config.save(&error)) {
        QMessageBox::warning(this, QStringLiteral("设置"), error);
        return;
    }

    QDialog::accept();
}
