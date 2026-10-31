#include "MainWindow.h"

#include "SettingsDialog.h"
#include "core/BackupService.h"
#include "core/Deduper.h"
#include "core/FileScanner.h"
#include "core/GameImporter.h"
#include "core/GameLauncher.h"
#include "core/LogWatcher.h"
#include "core/PathUtils.h"
#include "core/SaveInstaller.h"
#include "core/TaskRunner.h"
#include "core/Version.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("七日杀多版本与存档管理"));
    resize(1280, 860);

    setupUi();

    m_taskRunner = new TaskRunner(this);
    connect(m_taskRunner, &TaskRunner::started, this, [this](const QString &title) {
        updateTaskUi(true);
        m_taskLabel->setText(title);
        statusBar()->showMessage(title);
    });
    connect(m_taskRunner, &TaskRunner::progress, this, &MainWindow::onTaskProgress);
    connect(m_taskRunner, &TaskRunner::finished, this, &MainWindow::onTaskFinished);

    m_logWatcher = new LogWatcher(this);
    connect(m_logWatcher, &LogWatcher::versionDetected, this, &MainWindow::handleVersionDetected);
    connect(m_logWatcher, &LogWatcher::userDataFolderDetected, this,
            &MainWindow::handleUserDataFolder);
    connect(m_logWatcher, &LogWatcher::autoLoadResult, this,
            [this](bool ok, const QString &detail) {
                if (ok) {
                    statusBar()->showMessage(QStringLiteral("游戏已自动加载选中的存档。"), 12000);
                } else {
                    statusBar()->showMessage(
                        QStringLiteral("自动加载未成功，请在游戏内选择存档：%1").arg(detail),
                        20000);
                }
            });
    connect(m_logWatcher, &LogWatcher::versionMismatchDetected, this,
            [this](const QString &) {
                statusBar()->showMessage(
                    QStringLiteral("游戏日志提示：该存档由其他版本创建/保存，请留意兼容性。"),
                    20000);
            });

    setupMenus();
    loadState();
    updateActionStates();

    QTimer::singleShot(0, this, [this] {
        if (!m_library.versions.isEmpty())
            scanStatistics();
    });
}

MainWindow::~MainWindow()
{
    if (m_logWatcher)
        m_logWatcher->stop();
    if (m_taskRunner)
        m_taskRunner->cancel();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_taskRunner && m_taskRunner->isRunning()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("退出"),
            QStringLiteral("后台任务正在运行，确定取消并退出？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_taskRunner->cancel();
    }
    if (m_logWatcher)
        m_logWatcher->stop();
    event->accept();
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    auto *splitter = new QSplitter(Qt::Vertical, central);

    // ---- 游戏版本区 ----
    auto *versionGroup = new QGroupBox(QStringLiteral("游戏版本管理"), splitter);
    auto *versionLayout = new QVBoxLayout(versionGroup);

    m_versionTable = new QTableWidget(0, 8, versionGroup);
    m_versionTable->setHorizontalHeaderLabels(
        {QStringLiteral("标签"), QStringLiteral("检测版本"), QStringLiteral("路径"),
         QStringLiteral("基准"), QStringLiteral("逻辑大小"), QStringLiteral("物理占用"),
         QStringLiteral("文件数"), QStringLiteral("状态")});
    m_versionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_versionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_versionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_versionTable->setAlternatingRowColors(true);
    m_versionTable->verticalHeader()->setVisible(false);
    m_versionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    versionLayout->addWidget(m_versionTable, 1);

    m_summaryLabel = new QLabel(versionGroup);
    m_summaryLabel->setWordWrap(true);
    versionLayout->addWidget(m_summaryLabel);

    m_importButton = new QPushButton(QStringLiteral("导入 Steam 版本"), versionGroup);
    m_addButton = new QPushButton(QStringLiteral("添加已有目录"), versionGroup);
    m_baseButton = new QPushButton(QStringLiteral("指定基准版本..."), versionGroup);
    m_setBaseButton = new QPushButton(QStringLiteral("设为基准"), versionGroup);
    m_dedupButton = new QPushButton(QStringLiteral("去重"), versionGroup);
    m_verifyButton = new QPushButton(QStringLiteral("校验基准"), versionGroup);
    m_breakButton = new QPushButton(QStringLiteral("解除链接"), versionGroup);
    m_removeButton = new QPushButton(QStringLiteral("删除版本"), versionGroup);
    m_launchVersionButton = new QPushButton(QStringLiteral("启动版本"), versionGroup);

    m_importButton->setToolTip(QStringLiteral(
        "从 Steam 安装目录复制一个完整游戏版本到版本汇集目录；"
        "不会移动或修改 Steam 原目录。"));
    m_addButton->setToolTip(QStringLiteral(
        "登记一个已存在的完整游戏目录；只添加记录，不复制文件。"));
    m_baseButton->setToolTip(QStringLiteral(
        "通过文件对话框选择任意完整游戏目录设为基准；"
        "会校验完整性、NTFS、同卷，且不能位于 Steam 管理目录下。"));
    m_setBaseButton->setToolTip(QStringLiteral(
        "将版本表中选中的版本设为基准；已建立的硬链接不会自动改变。"));
    m_dedupButton->setToolTip(QStringLiteral(
        "将选中版本与基准版本中相同的文件建立硬链接；"
        "执行前会显示可共享文件数和预计节省空间。"));
    m_verifyButton->setToolTip(QStringLiteral(
        "按基准清单校验基准目录是否被修改；发现漂移时会列出不一致的文件。"));
    m_breakButton->setToolTip(QStringLiteral(
        "将选中版本中的共享文件复制为独立副本；"
        "会恢复版本独立性，但会占用额外磁盘空间。"));
    m_removeButton->setToolTip(QStringLiteral(
        "从版本库移除选中版本；可选择只删除记录，或同时删除磁盘目录。"));
    m_launchVersionButton->setToolTip(QStringLiteral(
        "直接启动选中的游戏版本，不自动加载存档。"));

    // 创建带标题的按钮分组，供版本操作区按流程排列。
    auto makeActionGroup = [versionGroup](const QString &title,
                                          const QList<QPushButton *> &buttons) {
        auto *group = new QGroupBox(title, versionGroup);
        auto *layout = new QHBoxLayout(group);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);
        for (QPushButton *button : buttons) {
            button->setAutoDefault(false);
            layout->addWidget(button);
        }
        return group;
    };
    // 创建流程箭头，表示操作推荐顺序。
    auto makeArrow = [versionGroup]() {
        auto *arrow = new QLabel(QStringLiteral("→"), versionGroup);
        QFont font = arrow->font();
        font.setBold(true);
        font.setPointSize(font.pointSize() + 2);
        arrow->setFont(font);
        arrow->setAlignment(Qt::AlignCenter);
        return arrow;
    };

    auto *versionActions = new QHBoxLayout();
    versionActions->setSpacing(6);
    versionActions->addWidget(
        makeActionGroup(QStringLiteral("添加游戏版本"), {m_importButton, m_addButton}));
    versionActions->addWidget(makeArrow());
    versionActions->addWidget(
        makeActionGroup(QStringLiteral("指定基准版本"), {m_baseButton, m_setBaseButton}));
    versionActions->addWidget(makeArrow());
    versionActions->addWidget(
        makeActionGroup(QStringLiteral("去重与校验"), {m_dedupButton, m_verifyButton}));
    versionActions->addWidget(makeArrow());
    versionActions->addWidget(
        makeActionGroup(QStringLiteral("删除游戏版本"), {m_breakButton, m_removeButton}));
    versionActions->addStretch(1);
    versionLayout->addLayout(versionActions);

    // 启动版本是版本区的主操作，单独放大显示。
    QFont launchFont = m_launchVersionButton->font();
    launchFont.setBold(true);
    launchFont.setPointSize(launchFont.pointSize() + 2);
    m_launchVersionButton->setFont(launchFont);
    m_launchVersionButton->setMinimumHeight(44);
    m_launchVersionButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_launchVersionButton->setAutoDefault(false);
    versionLayout->addWidget(m_launchVersionButton);

    // ---- 存档区 ----
    auto *saveGroup = new QGroupBox(QStringLiteral("存档管理"), splitter);
    auto *saveLayout = new QVBoxLayout(saveGroup);

    m_saveTable = new QTableWidget(0, 8, saveGroup);
    m_saveTable->setHorizontalHeaderLabels(
        {QStringLiteral("世界"), QStringLiteral("存档"), QStringLiteral("原始版本"),
         QStringLiteral("最后保存版本"), QStringLiteral("最后修改"), QStringLiteral("对应游戏版本"),
         QStringLiteral("状态"), QStringLiteral("来源")});
    m_saveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_saveTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_saveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_saveTable->setAlternatingRowColors(true);
    m_saveTable->verticalHeader()->setVisible(false);
    m_saveTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    saveLayout->addWidget(m_saveTable, 1);

    m_startButton = new QPushButton(QStringLiteral("从版本开始游戏"), saveGroup);
    m_backupButton = new QPushButton(QStringLiteral("备份存档"), saveGroup);
    m_overrideButton = new QPushButton(QStringLiteral("指定版本"), saveGroup);
    m_chooseSaveLibraryButton = new QPushButton(QStringLiteral("添加存档目录"), saveGroup);
    m_openSaveButton = new QPushButton(QStringLiteral("打开存档目录"), saveGroup);
    m_refreshButton = new QPushButton(QStringLiteral("刷新"), saveGroup);

    m_startButton->setToolTip(QStringLiteral(
        "使用存档匹配（或当前选中）的游戏版本启动；"
        "选中存档库中的存档时，会先备份 C 盘旧存档，再复制该存档到游戏存档目录。"));
    m_backupButton->setToolTip(QStringLiteral(
        "按备份策略把选中存档复制到备份目录，并清理超过保留份数的旧备份。"));
    m_overrideButton->setToolTip(QStringLiteral(
        "为选中存档手动指定游戏版本；选择“自动匹配”可清除手动指定。"));
    m_chooseSaveLibraryButton->setToolTip(
        QStringLiteral("选择额外扫描的存档库目录；启动时会把它里面的存档复制到游戏存档目录。"));
    m_openSaveButton->setToolTip(QStringLiteral(
        "在资源管理器中打开选中存档所在目录。"));
    m_refreshButton->setToolTip(QStringLiteral(
        "重新扫描游戏版本与存档，并统计版本占用；文件较多时可能需要一些时间。"));

    // 从版本开始游戏是存档区的主操作，单独放大显示。
    QFont startFont = m_startButton->font();
    startFont.setBold(true);
    startFont.setPointSize(startFont.pointSize() + 2);
    m_startButton->setFont(startFont);
    m_startButton->setMinimumHeight(44);
    m_startButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_startButton->setAutoDefault(false);
    saveLayout->addWidget(m_startButton);

    auto *saveButtons = new QHBoxLayout();
    for (QPushButton *button :
         {m_backupButton, m_overrideButton, m_chooseSaveLibraryButton,
          m_openSaveButton, m_refreshButton}) {
        button->setAutoDefault(false);
        saveButtons->addWidget(button);
    }
    saveButtons->addStretch(1);
    saveLayout->addLayout(saveButtons);

    splitter->addWidget(versionGroup);
    splitter->addWidget(saveGroup);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({380, 420});

    mainLayout->addWidget(splitter);
    setCentralWidget(central);

    m_rootLabel = new QLabel(this);
    m_savesLabel = new QLabel(this);
    m_taskLabel = new QLabel(this);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setFixedWidth(220);
    m_progressBar->setVisible(false);
    m_cancelTaskButton = new QPushButton(QStringLiteral("取消任务"), this);
    m_cancelTaskButton->setAutoDefault(false);
    m_cancelTaskButton->setToolTip(
        QStringLiteral("取消当前正在执行的导入、去重、统计或存档复制任务。"));
    m_cancelTaskButton->setVisible(false);

    statusBar()->addWidget(m_rootLabel, 1);
    statusBar()->addWidget(m_savesLabel, 1);
    statusBar()->addPermanentWidget(m_taskLabel);
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_cancelTaskButton);

    connect(m_cancelTaskButton, &QPushButton::clicked, this, [this] {
        if (m_taskRunner)
            m_taskRunner->cancel();
        m_cancelTaskButton->setEnabled(false);
        m_cancelTaskButton->setText(QStringLiteral("正在取消..."));
    });

    connect(m_versionTable, &QTableWidget::itemSelectionChanged, this,
            &MainWindow::updateActionStates);
    connect(m_saveTable, &QTableWidget::itemSelectionChanged, this,
            &MainWindow::updateActionStates);
    connect(m_saveTable, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { startSelectedGame(); });
    connect(m_versionTable, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { launchSelectedVersion(); });

    connect(m_importButton, &QPushButton::clicked, this, &MainWindow::importSteamVersion);
    connect(m_addButton, &QPushButton::clicked, this, &MainWindow::addExistingVersion);
    connect(m_baseButton, &QPushButton::clicked, this, &MainWindow::chooseBaseVersionDirectory);
    connect(m_setBaseButton, &QPushButton::clicked, this, &MainWindow::setSelectedAsBase);
    connect(m_dedupButton, &QPushButton::clicked, this, &MainWindow::dedupSelectedVersion);
    connect(m_verifyButton, &QPushButton::clicked, this, &MainWindow::verifySelectedBase);
    connect(m_breakButton, &QPushButton::clicked, this, &MainWindow::breakLinksSelectedVersion);
    connect(m_removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedVersion);
    connect(m_launchVersionButton, &QPushButton::clicked, this,
            &MainWindow::launchSelectedVersion);

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startSelectedGame);
    connect(m_backupButton, &QPushButton::clicked, this, &MainWindow::backupSelectedSave);
    connect(m_overrideButton, &QPushButton::clicked, this, &MainWindow::overrideSaveVersion);
    connect(m_chooseSaveLibraryButton, &QPushButton::clicked, this,
            &MainWindow::chooseSaveLibrary);
    connect(m_openSaveButton, &QPushButton::clicked, this,
            &MainWindow::openSelectedSaveDirectory);
    connect(m_refreshButton, &QPushButton::clicked, this, [this] {
        refreshAll();
        scanStatistics();
    });
}

void MainWindow::setupMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    QAction *settingsAction = fileMenu->addAction(QStringLiteral("设置..."));
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(QStringLiteral("退出"));
    quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *toolsMenu = menuBar()->addMenu(QStringLiteral("操作"));
    QAction *refreshAction = toolsMenu->addAction(QStringLiteral("刷新"));
    refreshAction->setShortcut(QKeySequence::Refresh);
    connect(refreshAction, &QAction::triggered, this, [this] {
        refreshAll();
        scanStatistics();
    });
    QAction *openVersionAction = toolsMenu->addAction(QStringLiteral("打开版本根目录"));
    connect(openVersionAction, &QAction::triggered, this, &MainWindow::openVersionRoot);
    QAction *openSavesAction = toolsMenu->addAction(QStringLiteral("打开存档目录"));
    connect(openSavesAction, &QAction::triggered, this, &MainWindow::openSavesRoot);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    QAction *aboutAction = helpMenu->addAction(QStringLiteral("关于"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, QStringLiteral("关于"),
            QStringLiteral("七日杀多版本与存档管理\n\n版本：%1\n开发者：WK&AYU")
                .arg(QCoreApplication::applicationVersion()));
    });
}

void MainWindow::loadState()
{
    QString error;
    m_config = AppConfig::load(&error);
    if (!error.isEmpty())
        qWarning("%s", qUtf8Printable(error));

    m_library = Library::load(m_config.versionRoot, &error);
    if (!error.isEmpty())
        qWarning("%s", qUtf8Printable(error));

    refreshAll();
}

void MainWindow::refreshAll()
{
    m_library.refreshExistence();
    refreshVersionTable();
    refreshSaveTable();
    updateSummary();
    updateActionStates();

    m_rootLabel->setText(QStringLiteral("版本根：%1")
                             .arg(m_config.versionRoot.isEmpty() ? QStringLiteral("未设置")
                                                                : m_config.versionRoot));
    const QString savesText =
        QStringLiteral("存档库：%1（共 %2 个存档）")
            .arg(m_config.saveLibraryRoot.isEmpty() ? QStringLiteral("未设置")
                                                    : m_config.saveLibraryRoot)
            .arg(m_saves.size());
    m_savesLabel->setText(savesText);
}

void MainWindow::refreshVersionTable(const QString &selectId)
{
    const QString keepId = selectId.isEmpty() ? selectedVersionId() : selectId;

    QSignalBlocker blocker(m_versionTable);
    m_versionTable->setRowCount(0);

    for (const VersionInfo &version : m_library.versions) {
        const int row = m_versionTable->rowCount();
        m_versionTable->insertRow(row);

        auto setItem = [this, row](int column, const QString &text, const QString &tooltip = QString()) {
            auto *item = new QTableWidgetItem(text);
            if (!tooltip.isEmpty())
                item->setToolTip(tooltip);
            m_versionTable->setItem(row, column, item);
        };

        setItem(0, version.label);
        m_versionTable->item(row, 0)->setData(Qt::UserRole, version.id);

        setItem(1, version.detectedVersion.isEmpty() ? QStringLiteral("未检测")
                                                     : version.detectedVersion);
        setItem(2, version.path, version.path);
        setItem(3, version.id == m_library.baseVersionId ? QStringLiteral("基准") : QString());
        setItem(4, version.logicalBytes > 0 ? PathUtils::humanSize(version.logicalBytes)
                                            : QStringLiteral("-"));
        setItem(5, version.physicalBytes >= 0 ? PathUtils::humanSize(version.physicalBytes)
                                              : QStringLiteral("-"),
                QStringLiteral("该版本内按文件身份去重；与其他版本共享的文件仍会各自计入。"
                               "全局物理占用见汇总行。"));
        setItem(6, version.fileCount > 0 ? QString::number(version.fileCount)
                                         : QStringLiteral("-"));

        QStringList statuses;
        if (!version.exists)
            statuses << QStringLiteral("目录缺失");
        if (version.id == m_library.baseVersionId)
            statuses << QStringLiteral("基准");
        if (version.volatileSource)
            statuses << QStringLiteral("Steam 管理");
        if (version.exists && !version.volatileSource)
            statuses << QStringLiteral("就绪");
        if (statuses.isEmpty())
            statuses << QStringLiteral("未就绪");
        setItem(7, statuses.join(QStringLiteral("；")));
    }

    m_versionTable->resizeColumnsToContents();
    m_versionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    if (!keepId.isEmpty()) {
        for (int row = 0; row < m_versionTable->rowCount(); ++row) {
            if (m_versionTable->item(row, 0)->data(Qt::UserRole).toString() == keepId) {
                m_versionTable->selectRow(row);
                break;
            }
        }
    }
}

void MainWindow::refreshSaveTable(const QString &selectKey)
{
    maybeNormalizeLibraryNames();

    const int previousRow = selectedSaveRow();
    const QString keepKey = !selectKey.isEmpty()
                                ? selectKey
                                : (previousRow >= 0 ? rowKey(m_saves.at(previousRow)) : QString());

    QVector<SaveInfo> saves;
    QStringList scanErrors;
    QSet<QString> scannedRoots;
    auto scanRoot = [&saves, &scanErrors, &scannedRoots](const QString &root) {
        if (root.isEmpty())
            return;
        const QString key = QDir::cleanPath(root).toLower();
        if (scannedRoots.contains(key))
            return;
        scannedRoots.insert(key);
        if (!QFileInfo(root).isDir()) {
            scanErrors << QStringLiteral("存档目录不存在：%1").arg(root);
            return;
        }
        const SaveScanResult scan = SaveScanner::scan(root);
        if (!scan.error.isEmpty())
            scanErrors << scan.error;
        saves += scan.saves;
    };

    // 存档表只展示存档库；C 盘游戏存档目录仅作为启动时的复制/备份目标。
    const QString libraryRoot = QDir::cleanPath(m_config.saveLibraryRoot);
    const QString savesRoot = QDir::cleanPath(m_config.savesRoot);
    if (!m_config.saveLibraryRoot.isEmpty()
        && libraryRoot.compare(savesRoot, Qt::CaseInsensitive) != 0) {
        for (const QString &root : SaveScanner::discoverSavesRoots(m_config.saveLibraryRoot))
            scanRoot(root);
    }

    // 存档库可能包含或指向 C 盘游戏存档目录，剔除其中所有游戏运行时存档。
    if (!savesRoot.isEmpty()) {
        const auto isGameRuntimeSave = [&savesRoot](const SaveInfo &save) {
            const QString directory = QDir::cleanPath(save.directory);
            return directory.compare(savesRoot, Qt::CaseInsensitive) == 0
                   || directory.startsWith(savesRoot + QLatin1Char('/'), Qt::CaseInsensitive);
        };
        saves.erase(std::remove_if(saves.begin(), saves.end(), isGameRuntimeSave), saves.end());
    }

    std::stable_sort(saves.begin(), saves.end(), [](const SaveInfo &a, const SaveInfo &b) {
        return a.lastModified > b.lastModified;
    });
    m_saves = saves;

    QSignalBlocker blocker(m_saveTable);
    m_saveTable->setRowCount(0);

    for (const SaveInfo &save : m_saves) {
        const int row = m_saveTable->rowCount();
        m_saveTable->insertRow(row);

        auto setItem = [this, row](int column, const QString &text, const QString &tooltip = QString()) {
            auto *item = new QTableWidgetItem(text);
            if (!tooltip.isEmpty())
                item->setToolTip(tooltip);
            m_saveTable->setItem(row, column, item);
        };

        setItem(0, save.world);
        m_saveTable->item(row, 0)->setData(Qt::UserRole, rowKey(save));
        setItem(1, save.saveName);
        setItem(2, save.originalVersion.isEmpty() ? QStringLiteral("-") : save.originalVersion,
                save.originalVersion.isEmpty() ? QStringLiteral("没有 ext.bak，无法确定原始版本")
                                               : QString());
        setItem(3, save.lastSavedVersion.isEmpty() ? QStringLiteral("无法识别")
                                                   : save.lastSavedVersion);
        setItem(4, save.lastModified.toString(QStringLiteral("yyyy-MM-dd HH:mm")));

        bool matched = false;
        const VersionInfo *target = versionForSave(save, &matched);
        QString targetText = target ? versionDisplayName(*target) : QStringLiteral("无匹配");
        if (target && !m_library.resolveSaveVersion(save.world, save.saveName).isEmpty())
            targetText += QStringLiteral("（手动）");
        setItem(5, targetText);

        QStringList statuses;
        if (save.rewritten) {
            statuses << QStringLiteral("已升级：%1 → %2")
                            .arg(save.originalVersion, save.lastSavedVersion);
        }
        if (save.lastSavedVersion.isEmpty())
            statuses << QStringLiteral("版本无法识别");
        else if (!target)
            statuses << QStringLiteral("无匹配版本");
        else
            statuses << QStringLiteral("可启动");
        setItem(6, statuses.join(QStringLiteral("；")));
        setItem(7, saveSourceLabel(save), save.sourceRoot);
    }

    m_saveTable->resizeColumnsToContents();
    m_saveTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);

    if (!keepKey.isEmpty()) {
        for (int row = 0; row < m_saveTable->rowCount(); ++row) {
            if (m_saveTable->item(row, 0)->data(Qt::UserRole).toString() == keepKey) {
                m_saveTable->selectRow(row);
                break;
            }
        }
    }

    if (!scanErrors.isEmpty())
        statusBar()->showMessage(scanErrors.join(QStringLiteral("；")), 8000);
}

void MainWindow::maybeNormalizeLibraryNames()
{
    if (m_libraryRenameDeclined || m_config.saveLibraryRoot.isEmpty())
        return;
    if (QDir::cleanPath(m_config.saveLibraryRoot)
            .compare(QDir::cleanPath(m_config.savesRoot), Qt::CaseInsensitive)
        == 0) {
        return;
    }
    if (!QFileInfo(m_config.saveLibraryRoot).isDir())
        return;

    QVector<SaveRenamePlan> plans;
    for (const QString &root : SaveScanner::discoverSavesRoots(m_config.saveLibraryRoot))
        plans += SaveScanner::planTimestampRenames(root);

    // 存档库可能是 C 盘游戏存档目录的父目录，不能重命名其中的运行时存档。
    const QString savesRoot = QDir::cleanPath(m_config.savesRoot);
    if (!savesRoot.isEmpty()) {
        const auto isGameRuntimeSave = [&savesRoot](const SaveRenamePlan &plan) {
            const QString path = QDir::cleanPath(plan.fromPath);
            return path.compare(savesRoot, Qt::CaseInsensitive) == 0
                   || path.startsWith(savesRoot + QLatin1Char('/'), Qt::CaseInsensitive);
        };
        plans.erase(std::remove_if(plans.begin(), plans.end(), isGameRuntimeSave), plans.end());
    }
    if (plans.isEmpty())
        return;

    QStringList lines;
    const int shown = qMin(10, plans.size());
    for (int i = 0; i < shown; ++i) {
        lines << QStringLiteral("%1 → %2").arg(plans.at(i).fromName, plans.at(i).toName);
    }
    if (shown < plans.size())
        lines << QStringLiteral("... 其余 %1 项已省略").arg(plans.size() - shown);

    const auto answer = QMessageBox::question(
        this, QStringLiteral("存档库目录重命名"),
        QStringLiteral("检测到 %1 个存档目录未按标准格式命名。\n"
                       "将按 main.ttw 的版本和保存时间重命名为 "
                       "7DTDSaves_<版本>_<时间戳>：\n\n"
                       "%2\n\n是否执行？")
            .arg(plans.size())
            .arg(lines.join(QLatin1Char('\n'))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        m_libraryRenameDeclined = true;
        return;
    }

    QString error;
    if (!SaveScanner::applyRenames(plans, &error)) {
        showError(QStringLiteral("存档库重命名失败"), error);
        return;
    }

    // 目录改名后同步“手动指定版本”的键，避免旧覆盖失效。
    bool overridesChanged = false;
    for (const SaveRenamePlan &plan : plans) {
        const QString world = QFileInfo(plan.fromPath).dir().dirName();
        const QString oldKey = world + QLatin1Char('/') + plan.fromName;
        if (!m_library.saveVersionOverrides.contains(oldKey))
            continue;
        const QString versionId = m_library.saveVersionOverrides.take(oldKey);
        m_library.saveVersionOverrides.insert(
            world + QLatin1Char('/') + plan.toName, versionId);
        overridesChanged = true;
    }
    if (overridesChanged && !m_library.save(&error))
        qWarning("%s", qUtf8Printable(error));

    statusBar()->showMessage(
        QStringLiteral("已将 %1 个存档目录重命名为 7DTDSaves_<版本>_<时间戳>。")
            .arg(plans.size()),
        8000);
}

void MainWindow::chooseSaveLibrary()
{
    const QString initial = m_config.saveLibraryRoot.isEmpty() ? m_config.savesRoot
                                                               : m_config.saveLibraryRoot;
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择存档目录"), initial, QFileDialog::ShowDirsOnly);
    if (chosen.isEmpty())
        return;

    const QString directory = QDir::toNativeSeparators(chosen);
    if (QDir::cleanPath(directory)
            .compare(QDir::cleanPath(m_config.savesRoot), Qt::CaseInsensitive)
        == 0) {
        showInfo(QStringLiteral("添加存档目录"),
                 QStringLiteral("该目录就是游戏存档目录，无需重复添加。"));
        return;
    }

    m_config.saveLibraryRoot = directory;
    m_libraryRenameDeclined = false;

    QString error;
    if (!m_config.save(&error))
        showError(QStringLiteral("保存配置失败"), error);

    refreshAll();
}

void MainWindow::updateSummary()
{
    if (m_library.versions.isEmpty()) {
        m_summaryLabel->setText(
            QStringLiteral("尚未登记游戏版本。请先“导入 Steam 版本”或“添加已有目录”。"));
        return;
    }

    if (m_usageValid) {
        m_summaryLabel->setText(
            QStringLiteral("版本 %1 个；逻辑合计 %2；物理占用 %3；共享节省 %4；文件 %5 个")
                .arg(m_library.versions.size())
                .arg(PathUtils::humanSize(m_globalLogicalBytes),
                     PathUtils::humanSize(m_globalPhysicalBytes),
                     PathUtils::humanSize(qMax<qint64>(0, m_globalLogicalBytes
                                                             - m_globalPhysicalBytes)))
                .arg(m_globalFileCount));
        return;
    }

    qint64 logical = 0;
    int files = 0;
    for (const VersionInfo &version : m_library.versions) {
        logical += version.logicalBytes;
        files += version.fileCount;
    }
    m_summaryLabel->setText(
        QStringLiteral("版本 %1 个；逻辑合计 %2；文件 %3 个；物理占用尚未统计")
            .arg(m_library.versions.size())
            .arg(PathUtils::humanSize(logical))
            .arg(files));
}

void MainWindow::updateActionStates()
{
    const bool busy = m_taskRunner && m_taskRunner->isRunning();
    const QString versionId = selectedVersionId();
    const VersionInfo *selected = versionId.isEmpty() ? nullptr : m_library.findVersion(versionId);
    const VersionInfo *base = m_library.baseVersion();
    const bool hasSave = selectedSaveRow() >= 0;
    const bool baseUsable = base && base->exists;

    const bool canDedup = !busy && selected && selected->exists && !selected->volatileSource
                          && baseUsable && selected->id != base->id;
    const bool canSetBase = !busy && selected && selected->exists && !selected->volatileSource
                            && selected->id != m_library.baseVersionId;

    m_importButton->setEnabled(!busy);
    m_addButton->setEnabled(!busy);
    m_baseButton->setEnabled(!busy);
    m_setBaseButton->setEnabled(canSetBase);
    m_dedupButton->setEnabled(canDedup);
    m_verifyButton->setEnabled(!busy && baseUsable);
    m_breakButton->setEnabled(!busy && selected && selected->exists);
    m_removeButton->setEnabled(!busy && selected != nullptr);
    m_launchVersionButton->setEnabled(!busy && selected && selected->exists);

    m_startButton->setEnabled(!busy && (hasSave || (selected && selected->exists)));
    m_backupButton->setEnabled(!busy && hasSave);
    m_overrideButton->setEnabled(!busy && hasSave && !m_library.versions.isEmpty());
    m_chooseSaveLibraryButton->setEnabled(!busy);
    m_openSaveButton->setEnabled(hasSave);
    m_refreshButton->setEnabled(!busy);
}

QString MainWindow::selectedVersionId() const
{
    if (!m_versionTable)
        return QString();
    const int row = m_versionTable->currentRow();
    if (row < 0 || !m_versionTable->item(row, 0))
        return QString();
    return m_versionTable->item(row, 0)->data(Qt::UserRole).toString();
}

int MainWindow::selectedSaveRow() const
{
    if (!m_saveTable)
        return -1;
    const int row = m_saveTable->currentRow();
    if (row < 0 || row >= m_saves.size())
        return -1;
    return row;
}

const VersionInfo *MainWindow::versionForSave(const SaveInfo &save, bool *matched) const
{
    if (matched)
        *matched = false;

    const QString overrideId = m_library.resolveSaveVersion(save.world, save.saveName);
    if (!overrideId.isEmpty()) {
        const VersionInfo *version = m_library.findVersion(overrideId);
        if (version) {
            if (matched)
                *matched = true;
            return version;
        }
    }

    if (save.lastSavedVersion.isEmpty())
        return nullptr;

    const VersionInfo *fallback = nullptr;
    for (const VersionInfo &version : m_library.versions) {
        const QString candidate =
            version.detectedVersion.isEmpty() ? version.label : version.detectedVersion;
        if (!sameMajorMinor(save.lastSavedVersion, candidate))
            continue;
        if (version.id == m_library.baseVersionId) {
            if (matched)
                *matched = true;
            return &version;
        }
        if (!fallback)
            fallback = &version;
    }

    if (fallback && matched)
        *matched = true;
    return fallback;
}

QString MainWindow::saveKey(const SaveInfo &save)
{
    return save.world + QLatin1Char('/') + save.saveName;
}

QString MainWindow::rowKey(const SaveInfo &save)
{
    return QDir::cleanPath(save.sourceRoot) + QLatin1Char('\n') + saveKey(save);
}

bool MainWindow::isLibrarySave(const SaveInfo &save) const
{
    if (save.sourceRoot.isEmpty())
        return false;
    return QDir::cleanPath(save.sourceRoot)
               .compare(QDir::cleanPath(m_config.savesRoot), Qt::CaseInsensitive)
           != 0;
}

QString MainWindow::saveSourceLabel(const SaveInfo &save) const
{
    return isLibrarySave(save) ? QStringLiteral("存档库") : QStringLiteral("游戏存档");
}

QString MainWindow::versionDisplayName(const VersionInfo &version)
{
    if (version.detectedVersion.isEmpty())
        return version.label;
    return QStringLiteral("%1（%2）").arg(version.label, version.detectedVersion);
}

void MainWindow::runTask(const QString &title, TaskRunner::Task task,
                         std::function<void(bool, const QString &)> onFinished)
{
    if (!m_taskRunner)
        return;
    if (m_taskRunner->isRunning()) {
        showInfo(QStringLiteral("后台任务"), QStringLiteral("已有后台任务在运行，请稍候。"));
        return;
    }
    m_taskCallback = std::move(onFinished);
    m_taskRunner->start(title, std::move(task));
}

void MainWindow::onTaskProgress(int percent, const QString &message)
{
    m_progressBar->setValue(qBound(0, percent, 100));
    m_taskLabel->setText(message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onTaskFinished(bool ok, const QString &message)
{
    updateTaskUi(false);

    auto callback = std::move(m_taskCallback);
    m_taskCallback = nullptr;
    if (callback)
        callback(ok, message);
    else if (!ok && message != QStringLiteral("已取消"))
        showError(QStringLiteral("任务失败"), message);

    updateActionStates();
}

void MainWindow::updateTaskUi(bool running)
{
    m_progressBar->setVisible(running);
    m_cancelTaskButton->setVisible(running);
    m_cancelTaskButton->setEnabled(running);
    m_cancelTaskButton->setText(QStringLiteral("取消任务"));
    if (running) {
        m_progressBar->setValue(0);
    } else {
        m_taskLabel->clear();
    }
    updateActionStates();
}

void MainWindow::scanStatistics()
{
    if (m_library.versions.isEmpty())
        return;

    QStringList roots;
    for (const VersionInfo &version : m_library.versions) {
        if (version.exists)
            roots << version.path;
    }
    if (roots.isEmpty())
        return;

    auto stats = std::make_shared<UsageStats>();
    runTask(
        QStringLiteral("统计版本占用"),
        [stats, roots](TaskContext &context) {
            *stats = Deduper::calculateUsage(
                roots, context.cancelFlag(),
                [&context](int percent, const QString &message) {
                    context.report(percent, message);
                });
        },
        [this, stats](bool ok, const QString &message) {
            if (!ok) {
                if (message != QStringLiteral("已取消"))
                    statusBar()->showMessage(QStringLiteral("统计失败：%1").arg(message), 8000);
                return;
            }

            m_usageValid = true;
            m_globalLogicalBytes = stats->logicalBytes;
            m_globalPhysicalBytes = stats->physicalBytes;
            m_globalFileCount = stats->fileCount;

            for (VersionInfo &version : m_library.versions) {
                if (!version.exists)
                    continue;
                version.logicalBytes = stats->logicalByRoot.value(version.path,
                                                                  version.logicalBytes);
                version.fileCount = stats->countByRoot.value(version.path, version.fileCount);
                version.physicalBytes = stats->physicalByRoot.value(version.path, -1);
            }

            QString error;
            if (!m_library.save(&error))
                qWarning("%s", qUtf8Printable(error));

            refreshVersionTable();
            updateSummary();
            updateActionStates();
        });
}

void MainWindow::importSteamVersion()
{
    QString source = m_config.steamRoot;
    QString error;
    if (!GameImporter::validateSource(source, &error)) {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择 Steam 游戏安装目录"),
            source.isEmpty() ? m_config.versionRoot : source, QFileDialog::ShowDirsOnly);
        if (chosen.isEmpty())
            return;
        source = QDir::toNativeSeparators(chosen);
        if (!GameImporter::validateSource(source, &error)) {
            showError(QStringLiteral("导入版本"), error);
            return;
        }
        m_config.steamRoot = source;
        QString saveError;
        if (!m_config.save(&saveError))
            qWarning("%s", qUtf8Printable(saveError));
    }

    const SteamMetadata metadata = GameImporter::readSteamMetadata(source);
    QString initial = QStringLiteral("V1.4");
    if (!metadata.betaKey.isEmpty())
        initial = normalizeLabel(metadata.betaKey);

    bool ok = false;
    QString label = QInputDialog::getText(this, QStringLiteral("导入 Steam 版本"),
                                          QStringLiteral("版本标签（例如 V1.4）："),
                                          QLineEdit::Normal, initial, &ok);
    if (!ok)
        return;
    label = normalizeLabel(label);
    if (!parseGameVersion(label).isValid()) {
        showError(QStringLiteral("导入版本"),
                  QStringLiteral("版本标签必须包含主次版本号，例如 V1.4。"));
        return;
    }

    const QString targetPath =
        QDir(m_config.versionRoot).filePath(versionFolderName(label));
    if (QFileInfo::exists(targetPath)) {
        showError(QStringLiteral("导入版本"),
                  QStringLiteral("目标目录已存在：\n%1\n\n请换一个标签，或先处理已有目录。")
                      .arg(targetPath));
        return;
    }
    if (m_library.findByPath(targetPath)) {
        showError(QStringLiteral("导入版本"),
                  QStringLiteral("该目录已在版本库中登记。"));
        return;
    }

    if (!PathUtils::isNtfs(m_config.versionRoot, &error)) {
        showError(QStringLiteral("导入版本"), error);
        return;
    }
    const VersionInfo *base = m_library.baseVersion();
    if (base && !PathUtils::sameVolume(base->path, m_config.versionRoot, &error)) {
        showError(QStringLiteral("导入版本"),
                  QStringLiteral("版本汇集目录与基准版本不在同一卷，无法进行硬链接去重：\n%1")
                      .arg(error));
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("确认导入"),
        QStringLiteral("将把以下完整游戏目录复制为独立版本快照：\n\n源：%1\n目标：%2\n\n"
                       "约 20GB，可能需要数分钟。是否继续？")
            .arg(source, targetPath),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    auto result = std::make_shared<ImportResult>();
    runTask(
        QStringLiteral("导入 %1").arg(label),
        [result, source, targetPath](TaskContext &context) {
            *result = GameImporter::importDirectory(
                source, targetPath, context.cancelFlag(),
                [&context](int percent, const QString &message) {
                    context.report(percent, message);
                });
            if (!result->ok && !result->canceled)
                context.fail(result->error);
        },
        [this, result, label, targetPath](bool ok, const QString &message) {
            if (!ok) {
                if (message != QStringLiteral("已取消"))
                    showError(QStringLiteral("导入失败"), message);
                return;
            }

            VersionInfo info;
            info.id = m_library.makeId(label);
            info.label = label;
            info.path = QDir::toNativeSeparators(targetPath);
            info.logicalBytes = result->totalBytes;
            info.fileCount = result->fileCount;
            info.steamBuildId = result->steam.buildId;
            info.steamBetaKey = result->steam.betaKey;
            info.manifest = QStringLiteral(".meta/%1.manifest.json").arg(info.id);
            info.exists = true;

            QString error;
            if (!m_library.addVersion(info, &error)) {
                showError(QStringLiteral("导入完成但登记失败"), error);
                return;
            }
            if (m_library.baseVersionId.isEmpty())
                m_library.setBaseVersion(info.id);
            if (!m_library.save(&error))
                showError(QStringLiteral("保存版本库失败"), error);

            refreshVersionTable(info.id);
            updateSummary();
            updateActionStates();

            showInfo(QStringLiteral("导入完成"),
                     QStringLiteral("已导入 %1\n\n路径：%2\n文件：%3 个\n大小：%4")
                         .arg(label, info.path)
                         .arg(result->fileCount)
                         .arg(PathUtils::humanSize(result->totalBytes)));

            scanStatistics();
        });
}

void MainWindow::addExistingVersion()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QStringLiteral("添加已有游戏目录"), m_config.versionRoot,
        QFileDialog::ShowDirsOnly);
    if (chosen.isEmpty())
        return;

    const QString directory = QDir::toNativeSeparators(chosen);
    QString error;
    if (!PathUtils::isCompleteGameDir(directory, &error)) {
        showError(QStringLiteral("添加版本"),
                  QStringLiteral("所选目录不是完整的七日杀目录，缺少：%1").arg(error));
        return;
    }

    if (const VersionInfo *existing = m_library.findByPath(directory)) {
        refreshVersionTable(existing->id);
        showInfo(QStringLiteral("添加版本"),
                 QStringLiteral("该目录已登记为版本 %1。").arg(existing->label));
        return;
    }

    if (!PathUtils::isNtfs(directory, &error)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("添加版本"),
            QStringLiteral("%1\n\n该目录无法参与硬链接去重，是否仍然登记？").arg(error),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    const VersionInfo *base = m_library.baseVersion();
    if (base && !PathUtils::sameVolume(base->path, directory, &error)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("添加版本"),
            QStringLiteral("%1\n\n与基准版本不在同一卷，无法去重，是否仍然登记？").arg(error),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    bool ok = false;
    QString label = QInputDialog::getText(
        this, QStringLiteral("添加版本"), QStringLiteral("版本标签（例如 V1.4）："),
        QLineEdit::Normal, labelFromVersionFolder(QFileInfo(directory).fileName()), &ok);
    if (!ok)
        return;
    label = normalizeLabel(label);
    if (!parseGameVersion(label).isValid()) {
        showError(QStringLiteral("添加版本"),
                  QStringLiteral("版本标签必须包含主次版本号，例如 V1.4。"));
        return;
    }

    VersionInfo info;
    info.id = m_library.makeId(label);
    info.label = label;
    info.path = directory;
    info.volatileSource = PathUtils::isUnderSteamCommon(directory);
    info.manifest = QStringLiteral(".meta/%1.manifest.json").arg(info.id);
    info.exists = true;

    if (!m_library.addVersion(info, &error)) {
        showError(QStringLiteral("添加版本"), error);
        return;
    }
    if (!m_library.save(&error))
        showError(QStringLiteral("保存版本库失败"), error);

    refreshVersionTable(info.id);
    updateSummary();
    updateActionStates();

    if (info.volatileSource) {
        showInfo(QStringLiteral("添加版本"),
                 QStringLiteral("该目录位于 Steam 管理范围（steamapps\\common），"
                                "已登记为只读展示版本，不能设为基准或参与去重。"));
    }

    scanStatistics();
}

void MainWindow::chooseBaseVersionDirectory()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QStringLiteral("指定基准版本目录"), m_config.versionRoot,
        QFileDialog::ShowDirsOnly);
    if (chosen.isEmpty())
        return;

    const QString directory = QDir::toNativeSeparators(chosen);
    QString error;
    if (!PathUtils::isCompleteGameDir(directory, &error)) {
        showError(QStringLiteral("指定基准版本"),
                  QStringLiteral("所选目录不是完整的七日杀目录，缺少：%1").arg(error));
        return;
    }
    if (PathUtils::isUnderSteamCommon(directory)) {
        showError(QStringLiteral("指定基准版本"),
                  QStringLiteral("不能把 Steam 正在管理的目录（steamapps\\common）设为基准。\n"
                                 "请先把该目录复制为冻结快照。"));
        return;
    }
    if (!PathUtils::isNtfs(directory, &error)) {
        showError(QStringLiteral("指定基准版本"), error);
        return;
    }
    for (const VersionInfo &version : m_library.versions) {
        if (!version.exists || version.volatileSource)
            continue;
        if (!PathUtils::sameVolume(version.path, directory, &error)) {
            showError(QStringLiteral("指定基准版本"),
                      QStringLiteral("基准目录必须与所有参与去重的版本位于同一卷：\n%1")
                          .arg(error));
            return;
        }
    }

    VersionInfo *existing = m_library.findByPath(directory);
    QString versionId;
    if (existing) {
        if (existing->volatileSource) {
            showError(QStringLiteral("指定基准版本"),
                      QStringLiteral("该版本被标记为 Steam 管理目录，不能作为基准。"));
            return;
        }
        versionId = existing->id;
    } else {
        VersionInfo info;
        info.id = m_library.makeId(labelFromVersionFolder(QFileInfo(directory).fileName()));
        info.label = labelFromVersionFolder(QFileInfo(directory).fileName());
        info.path = directory;
        info.manifest = QStringLiteral(".meta/%1.manifest.json").arg(info.id);
        info.exists = true;
        if (!m_library.addVersion(info, &error)) {
            showError(QStringLiteral("指定基准版本"), error);
            return;
        }
        versionId = info.id;
    }

    if (!m_library.setBaseVersion(versionId, &error)) {
        showError(QStringLiteral("指定基准版本"), error);
        return;
    }
    if (!m_library.save(&error))
        showError(QStringLiteral("保存版本库失败"), error);

    refreshVersionTable(versionId);
    updateActionStates();
    updateSummary();

    const VersionInfo *base = m_library.findVersion(versionId);
    QString text = QStringLiteral("已将 %1 设为基准版本。")
                       .arg(base ? base->label : QStringLiteral("所选目录"));
    if (m_library.versions.size() > 1) {
        text += QStringLiteral("\n\n现有硬链接不会自动改变。如要让所有版本改为以新基准共享，"
                               "请选择其他版本并执行“去重”。");
    }
    showInfo(QStringLiteral("基准版本"), text);
}

void MainWindow::setSelectedAsBase()
{
    const QString id = selectedVersionId();
    VersionInfo *version = id.isEmpty() ? nullptr : m_library.findVersion(id);
    if (!version)
        return;
    if (version->volatileSource) {
        showError(QStringLiteral("设为基准"),
                  QStringLiteral("Steam 管理目录不能作为基准。"));
        return;
    }

    QString error;
    if (!PathUtils::isCompleteGameDir(version->path, &error)) {
        showError(QStringLiteral("设为基准"),
                  QStringLiteral("目录不完整，缺少：%1").arg(error));
        return;
    }
    if (!PathUtils::isNtfs(version->path, &error)) {
        showError(QStringLiteral("设为基准"), error);
        return;
    }
    for (const VersionInfo &other : m_library.versions) {
        if (other.id == version->id || !other.exists || other.volatileSource)
            continue;
        if (!PathUtils::sameVolume(other.path, version->path, &error)) {
            showError(QStringLiteral("设为基准"),
                      QStringLiteral("基准目录必须与其他版本位于同一卷：\n%1").arg(error));
            return;
        }
    }

    if (!m_library.setBaseVersion(version->id, &error)) {
        showError(QStringLiteral("设为基准"), error);
        return;
    }
    if (!m_library.save(&error))
        showError(QStringLiteral("保存版本库失败"), error);

    refreshVersionTable(version->id);
    updateActionStates();
    updateSummary();
    showInfo(QStringLiteral("设为基准"),
             QStringLiteral("已将 %1 设为基准版本。\n\n已有硬链接不会自动改变，"
                            "如需重新共享请执行“去重”。")
                 .arg(version->label));
}

void MainWindow::removeSelectedVersion()
{
    const QString id = selectedVersionId();
    VersionInfo *version = id.isEmpty() ? nullptr : m_library.findVersion(id);
    if (!version)
        return;

    const QString label = version->label;
    const QString path = version->path;
    const bool wasBase = version->id == m_library.baseVersionId;

    QMessageBox box(QMessageBox::Warning, QStringLiteral("删除版本"),
                    QStringLiteral("确定删除版本 %1？\n\n"
                                   "“仅移除记录”不会删除磁盘文件。\n"
                                   "“同时删除目录”会永久删除：\n%2")
                        .arg(label, path),
                    QMessageBox::NoButton, this);
    QPushButton *deleteBoth = box.addButton(QStringLiteral("同时删除目录"),
                                            QMessageBox::DestructiveRole);
    QPushButton *removeOnly = box.addButton(QStringLiteral("仅移除记录"),
                                            QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() != deleteBoth && box.clickedButton() != removeOnly)
        return;

    const bool deleteFiles = (box.clickedButton() == deleteBoth);
    if (deleteFiles) {
        const auto confirm = QMessageBox::question(
            this, QStringLiteral("再次确认"),
            QStringLiteral("将永久删除整个目录及其中的全部文件：\n%1\n\n此操作不可恢复，确定继续？")
                .arg(path),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm != QMessageBox::Yes)
            return;
    }

    m_library.removeVersion(id);
    QString error;
    if (!m_library.save(&error))
        showError(QStringLiteral("保存版本库失败"), error);

    refreshVersionTable();
    updateActionStates();
    updateSummary();

    if (wasBase) {
        showInfo(QStringLiteral("删除版本"),
                 QStringLiteral("基准版本记录已删除。请重新使用“指定基准版本...”设置基准。"));
    }

    if (deleteFiles) {
        runTask(
            QStringLiteral("删除版本目录"),
            [path](TaskContext &context) {
                context.report(0, QStringLiteral("正在删除：%1").arg(path));
                QDir directory(path);
                if (directory.exists() && !directory.removeRecursively())
                    context.fail(QStringLiteral("目录删除失败：%1").arg(path));
            },
            [this, path](bool ok, const QString &message) {
                if (!ok && message != QStringLiteral("已取消"))
                    showError(QStringLiteral("删除目录失败"), message);
                else
                    statusBar()->showMessage(QStringLiteral("已删除目录：%1").arg(path), 8000);
            });
    }
}

void MainWindow::dedupSelectedVersion()
{
    VersionInfo *base = m_library.baseVersion();
    if (!base || !base->exists) {
        showError(QStringLiteral("去重"),
                  QStringLiteral("请先指定一个存在且完整的基准版本。"));
        return;
    }

    const QString id = selectedVersionId();
    VersionInfo *source = id.isEmpty() ? nullptr : m_library.findVersion(id);
    if (!source) {
        showError(QStringLiteral("去重"), QStringLiteral("请先在版本表中选择一个非基准版本。"));
        return;
    }
    if (source->id == base->id) {
        showError(QStringLiteral("去重"),
                  QStringLiteral("不能对基准版本自身执行去重，请选择其他版本。"));
        return;
    }
    if (source->volatileSource) {
        showError(QStringLiteral("去重"),
                  QStringLiteral("该版本位于 Steam 管理范围，不能参与去重。"));
        return;
    }
    if (!source->exists) {
        showError(QStringLiteral("去重"), QStringLiteral("版本目录不存在：%1").arg(source->path));
        return;
    }

    QString error;
    if (!PathUtils::isCompleteGameDir(source->path, &error)
        || !PathUtils::isCompleteGameDir(base->path, &error)) {
        showError(QStringLiteral("去重"), QStringLiteral("目录不完整：%1").arg(error));
        return;
    }
    if (!PathUtils::isNtfs(source->path, &error)
        || !PathUtils::sameVolume(source->path, base->path, &error)) {
        showError(QStringLiteral("去重"), error);
        return;
    }

    const VersionInfo baseCopy = *base;
    const VersionInfo sourceCopy = *source;
    const QStringList excludes = m_config.excludes;
    const QString baseManifest = QDir(m_config.versionRoot)
                                     .filePath(QStringLiteral(".meta/%1.manifest.json")
                                                   .arg(baseCopy.id));
    const QString sourceManifest = QDir(m_config.versionRoot)
                                       .filePath(QStringLiteral(".meta/%1.manifest.json")
                                                     .arg(sourceCopy.id));

    auto plan = std::make_shared<DedupPlan>();
    runTask(
        QStringLiteral("分析去重"),
        [plan, baseCopy, sourceCopy, excludes, baseManifest, sourceManifest](
            TaskContext &context) {
            DedupOptions options;
            options.excludes = excludes;
            options.baseManifestPath = baseManifest;
            options.sourceManifestPath = sourceManifest;
            options.cancel = context.cancelFlag();
            options.progress = [&context](int percent, const QString &message) {
                context.report(percent, message);
            };
            *plan = Deduper::plan(sourceCopy.path, baseCopy.path, options);
            if (!plan->error.isEmpty() && !plan->canceled)
                context.fail(plan->error);
        },
        [this, plan, baseCopy, sourceCopy, excludes, baseManifest, sourceManifest](
            bool ok, const QString &message) {
            if (!ok) {
                if (message != QStringLiteral("已取消"))
                    showError(QStringLiteral("去重分析失败"), message);
                return;
            }
            if (plan->canceled)
                return;

            if (plan->entries.isEmpty()) {
                showInfo(QStringLiteral("去重分析"),
                         QStringLiteral("没有可共享的文件。\n\n相同文件：0\n已共享：%1\n"
                                        "内容不同：%2\n基准中缺失：%3\n哈希失败：%4")
                             .arg(plan->alreadyLinkedCount)
                             .arg(plan->differentCount)
                             .arg(plan->missingInBaseCount)
                             .arg(plan->hashFailedCount));
                return;
            }

            const auto answer = QMessageBox::question(
                this, QStringLiteral("确认去重"),
                QStringLiteral("基准：%1\n目标：%2\n\n"
                               "可共享文件：%3 个\n预计节省：%4\n"
                               "已共享：%5，内容不同：%6，基准缺失：%7\n\n"
                               "执行后目标版本中这些文件将与基准共用同一份数据。是否继续？")
                    .arg(baseCopy.label, sourceCopy.label)
                    .arg(plan->sameCount)
                    .arg(PathUtils::humanSize(plan->savingsBytes))
                    .arg(plan->alreadyLinkedCount)
                    .arg(plan->differentCount)
                    .arg(plan->missingInBaseCount),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;

            auto result = std::make_shared<DedupApplyResult>();
            runTask(
                QStringLiteral("执行去重"),
                [result, plan, excludes, baseManifest, sourceManifest](TaskContext &context) {
                    DedupOptions options;
                    options.excludes = excludes;
                    options.baseManifestPath = baseManifest;
                    options.sourceManifestPath = sourceManifest;
                    options.cancel = context.cancelFlag();
                    options.progress = [&context](int percent, const QString &message) {
                        context.report(percent, message);
                    };
                    *result = Deduper::apply(*plan, options);
                    if (!result->ok && !result->canceled)
                        context.fail(QStringLiteral("%1 个文件处理失败")
                                         .arg(result->failedCount));
                },
                [this, result](bool, const QString &) {
                    QStringList details = result->errors.mid(0, 10);
                    if (details.size() < result->errors.size())
                        details << QStringLiteral("... 其余 %1 条已省略")
                                       .arg(result->errors.size() - details.size());

                    if (result->ok) {
                        showInfo(QStringLiteral("去重完成"),
                                 QStringLiteral("已共享 %1 个文件，节省 %2。")
                                     .arg(result->linkedCount)
                                     .arg(PathUtils::humanSize(result->savedBytes)));
                    } else {
                        showError(QStringLiteral("去重完成但有错误"),
                                  QStringLiteral("成功：%1\n失败：%2\n节省：%3\n\n%4")
                                      .arg(result->linkedCount)
                                      .arg(result->failedCount)
                                      .arg(PathUtils::humanSize(result->savedBytes))
                                      .arg(details.join(QLatin1Char('\n'))));
                    }
                    scanStatistics();
                });
        });
}

void MainWindow::verifySelectedBase()
{
    VersionInfo *base = m_library.baseVersion();
    if (!base) {
        showError(QStringLiteral("校验基准"), QStringLiteral("尚未设置基准版本。"));
        return;
    }

    const QString manifestPath = QDir(m_config.versionRoot)
                                     .filePath(QStringLiteral(".meta/%1.manifest.json")
                                                   .arg(base->id));
    if (!QFileInfo::exists(manifestPath)) {
        showInfo(QStringLiteral("校验基准"),
                 QStringLiteral("尚未生成基准文件清单。\n请先对另一个版本执行一次“去重”分析。"));
        return;
    }

    const VersionInfo baseCopy = *base;
    auto drifts = std::make_shared<QVector<ManifestDrift>>();
    auto entryCount = std::make_shared<int>(0);

    runTask(
        QStringLiteral("校验基准"),
        [drifts, entryCount, baseCopy, manifestPath](TaskContext &context) {
            *drifts = Deduper::verifyManifest(baseCopy.path, manifestPath, entryCount.get());
            context.report(100, QStringLiteral("校验完成"));
        },
        [this, drifts, entryCount](bool ok, const QString &message) {
            if (!ok) {
                if (message != QStringLiteral("已取消"))
                    showError(QStringLiteral("校验基准失败"), message);
                return;
            }

            if (drifts->isEmpty()) {
                showInfo(QStringLiteral("校验基准"),
                         QStringLiteral("清单共 %1 项，未发现漂移。").arg(*entryCount));
                return;
            }

            QStringList lines;
            const int shown = qMin(20, drifts->size());
            for (int i = 0; i < shown; ++i)
                lines << QStringLiteral("%1：%2").arg(drifts->at(i).relativePath,
                                                      drifts->at(i).reason);
            if (shown < drifts->size())
                lines << QStringLiteral("... 其余 %1 项已省略").arg(drifts->size() - shown);

            showError(QStringLiteral("基准漂移"),
                      QStringLiteral("检测到 %1 个文件与清单不一致。\n"
                                     "基准被修改会影响所有共享这些文件的版本。\n\n%2")
                          .arg(drifts->size())
                          .arg(lines.join(QLatin1Char('\n'))));
        });
}

void MainWindow::breakLinksSelectedVersion()
{
    const QString id = selectedVersionId();
    VersionInfo *version = id.isEmpty() ? nullptr : m_library.findVersion(id);
    if (!version || !version->exists) {
        showError(QStringLiteral("解除链接"), QStringLiteral("请选择一个存在的版本。"));
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("解除链接"),
        QStringLiteral("将把版本 %1 中所有链接数大于 1 的文件复制为独立副本。\n\n"
                       "这会恢复该版本的独立性，但会占用额外磁盘空间，并可能需要较长时间。\n"
                       "是否继续？")
            .arg(version->label),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    const VersionInfo versionCopy = *version;
    auto broken = std::make_shared<int>(0);
    auto error = std::make_shared<QString>();

    runTask(
        QStringLiteral("解除链接"),
        [versionCopy, broken, error](TaskContext &context) {
            if (!Deduper::breakAllLinks(
                    versionCopy.path, context.cancelFlag(),
                    [&context](int percent, const QString &message) {
                        context.report(percent, message);
                    },
                    broken.get(), error.get())) {
                if (!error->isEmpty())
                    context.fail(*error);
            }
        },
        [this, broken, error](bool ok, const QString &message) {
            if (!ok) {
                if (message != QStringLiteral("已取消"))
                    showError(QStringLiteral("解除链接失败"),
                              error->isEmpty() ? message : *error);
                return;
            }
            showInfo(QStringLiteral("解除链接"),
                     QStringLiteral("已解除 %1 个文件的链接。").arg(*broken));
            scanStatistics();
        });
}

void MainWindow::launchSelectedVersion()
{
    const QString id = selectedVersionId();
    VersionInfo *version = id.isEmpty() ? nullptr : m_library.findVersion(id);
    if (!version) {
        showError(QStringLiteral("启动版本"), QStringLiteral("请先在版本表中选择一个版本。"));
        return;
    }
    if (!version->exists) {
        showError(QStringLiteral("启动版本"),
                  QStringLiteral("版本目录不存在：%1").arg(version->path));
        return;
    }

    QString error;
    if (!PathUtils::isCompleteGameDir(version->path, &error)) {
        showError(QStringLiteral("启动版本"),
                  QStringLiteral("版本目录不完整，缺少：%1").arg(error));
        return;
    }

    const VersionInfo versionCopy = *version;
    launchVerified(versionCopy, QString());
}

void MainWindow::startSelectedGame()
{
    const int saveRow = selectedSaveRow();
    const QString versionId = selectedVersionId();
    const VersionInfo *explicitVersion =
        versionId.isEmpty() ? nullptr : m_library.findVersion(versionId);

    if (saveRow < 0 && !explicitVersion) {
        showError(QStringLiteral("开始游戏"),
                  QStringLiteral("请先选择一个存档，或选择一个游戏版本。"));
        return;
    }

    const SaveInfo save = saveRow >= 0 ? m_saves.at(saveRow) : SaveInfo{};
    const bool hasSave = saveRow >= 0;

    const VersionInfo *target = nullptr;
    bool matched = false;

    if (hasSave) {
        target = versionForSave(save, &matched);
        if (!target && explicitVersion) {
            target = explicitVersion;
            matched = false;
        }
        if (target && matched && explicitVersion && explicitVersion->id != target->id) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("选择版本"),
                QStringLiteral("存档匹配的版本是 %1，当前选中的版本是 %2。\n\n"
                               "是否使用匹配版本启动？")
                    .arg(versionDisplayName(*target), versionDisplayName(*explicitVersion)),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
            if (answer == QMessageBox::Cancel)
                return;
            if (answer == QMessageBox::No) {
                target = explicitVersion;
                matched = false;
            }
        }
    } else {
        target = explicitVersion;
        matched = true;
    }

    if (!target) {
        showError(QStringLiteral("开始游戏"),
                  QStringLiteral("没有与存档版本 %1 匹配的游戏版本。\n\n"
                                 "请添加对应版本，或在版本表中选择一个版本后重试。")
                      .arg(save.lastSavedVersion.isEmpty() ? QStringLiteral("未知")
                                                           : save.lastSavedVersion));
        return;
    }

    if (!target->exists) {
        showError(QStringLiteral("开始游戏"),
                  QStringLiteral("版本目录不存在：%1").arg(target->path));
        return;
    }

    QString completeError;
    if (!PathUtils::isCompleteGameDir(target->path, &completeError)) {
        showError(QStringLiteral("开始游戏"),
                  QStringLiteral("版本目录不完整，缺少：%1").arg(completeError));
        return;
    }

    const bool librarySave = hasSave && isLibrarySave(save);

    bool mismatch = false;
    if (hasSave) {
        if (save.lastSavedVersion.isEmpty()) {
            mismatch = true;
        } else {
            const QString targetVersion = target->detectedVersion.isEmpty()
                                              ? target->label
                                              : target->detectedVersion;
            mismatch = !sameMajorMinor(save.lastSavedVersion, targetVersion);
        }
    }

    if (mismatch) {
        QString text = QStringLiteral("存档：%1\n存档最后保存版本：%2\n所选游戏版本：%3\n\n"
                                      "版本不一致可能导致存档无法加载或损坏。")
                           .arg(save.displayName(),
                                save.lastSavedVersion.isEmpty() ? QStringLiteral("无法识别")
                                                                : save.lastSavedVersion,
                                versionDisplayName(*target));
        if (save.rewritten) {
            text += QStringLiteral("\n\n注意：该存档已从 %1 升级为 %2。")
                        .arg(save.originalVersion, save.lastSavedVersion);
        }

        QMessageBox box(QMessageBox::Warning, QStringLiteral("版本不匹配"), text,
                        QMessageBox::NoButton, this);
        QPushButton *force = box.addButton(QStringLiteral("备份并强制启动"),
                                           QMessageBox::DestructiveRole);
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != force)
            return;

        if (!librarySave && m_config.backupPolicy == QStringLiteral("never")) {
            showError(QStringLiteral("无法强制启动"),
                      QStringLiteral("当前备份策略为“从不备份”。强制跨版本启动必须先备份，"
                                     "请先在设置中修改备份策略。"));
            return;
        }
    }

    const VersionInfo targetCopy = *target;
    const QString baseSaveName =
        librarySave ? SaveScanner::stripTimestampSuffix(save.saveName) : save.saveName;
    const QString saveRelative =
        hasSave ? save.world + QLatin1Char('/') + baseSaveName : QString();

    // 存档库中的存档需要先复制到游戏存档目录，再启动。
    if (librarySave) {
        launchLibrarySave(save, saveRelative, targetCopy);
        return;
    }

    bool needBackup = false;
    if (hasSave) {
        if (m_config.backupPolicy == QStringLiteral("always"))
            needBackup = true;
        else if (m_config.backupPolicy == QStringLiteral("mismatch") && mismatch)
            needBackup = true;
    }

    if (!needBackup) {
        launchVerified(targetCopy, saveRelative);
        return;
    }

    const QString backupRoot = m_config.backupRoot;
    const int keep = m_config.backupKeep;
    auto result = std::make_shared<BackupResult>();

    runTask(
        QStringLiteral("启动前备份存档"),
        [result, save, backupRoot, targetCopy](TaskContext &context) {
            *result = BackupService::backupSave(
                save.directory, backupRoot, save.world, save.saveName, targetCopy.label,
                context.cancelFlag(),
                [&context](int percent, const QString &message) {
                    context.report(percent, message);
                });
            if (!result->ok && !result->canceled)
                context.fail(result->error);
        },
        [this, result, save, backupRoot, keep, targetCopy, saveRelative](bool ok,
                                                                        const QString &message) {
            if (!ok) {
                if (message != QStringLiteral("已取消"))
                    showError(QStringLiteral("备份失败"),
                              QStringLiteral("%1\n\n已取消启动。").arg(message));
                return;
            }

            BackupService::pruneBackups(backupRoot, save.world, save.saveName, keep);
            statusBar()->showMessage(
                QStringLiteral("已备份到：%1").arg(result->path), 8000);
            launchVerified(targetCopy, saveRelative);
        });
}

void MainWindow::launchLibrarySave(const SaveInfo &save, const QString &saveRelative,
                                   const VersionInfo &target)
{
    const QString baseSaveName = SaveScanner::stripTimestampSuffix(save.saveName);
    const QString destination =
        QDir(m_config.savesRoot).filePath(save.world + QLatin1Char('/') + baseSaveName);
    // 存档可能来自 <存档库>\<版本>\Saves，备份也写回该实际 Saves 目录。
    const QString libraryRoot =
        save.sourceRoot.isEmpty() ? m_config.saveLibraryRoot : save.sourceRoot;
    const QString libraryWorldDir = QDir(libraryRoot).filePath(save.world);

    auto result = std::make_shared<SaveInstallResult>();
    auto backupPath = std::make_shared<QString>();

    runTask(
        QStringLiteral("准备存档并启动"),
        [result, backupPath, save, destination, libraryWorldDir](TaskContext &context) {
            const bool targetExists = PathUtils::isDirectory(destination);

            // C 盘已有同名存档时，先把它保存回存档库，避免覆盖后丢失上次游玩记录。
            if (targetExists) {
                QString error;
                if (!PathUtils::ensureDir(libraryWorldDir, &error)) {
                    context.fail(error);
                    return;
                }
                const QStringList existing = QDir(libraryWorldDir)
                                                 .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                const QString baseName = SaveScanner::stripTimestampSuffix(save.saveName);
                const QString backupName = SaveScanner::timestampedName(
                    baseName, QDateTime::currentDateTime(), existing);
                *backupPath = QDir(libraryWorldDir).filePath(backupName);

                const SaveInstallResult backup = SaveInstaller::install(
                    destination, *backupPath, context.cancelFlag(),
                    [&context](int percent, const QString &message) {
                        context.report(percent * 40 / 100,
                                       QStringLiteral("备份 C 盘存档：%1").arg(message));
                    });
                if (!backup.ok) {
                    if (!backup.canceled)
                        context.fail(backup.error);
                    return;
                }
            }

            *result = SaveInstaller::install(
                save.directory, destination, context.cancelFlag(),
                [&context, targetExists](int percent, const QString &message) {
                    const int mapped = targetExists ? 40 + percent * 60 / 100 : percent;
                    context.report(mapped,
                                   QStringLiteral("复制到游戏存档目录：%1").arg(message));
                });
            if (!result->ok && !result->canceled)
                context.fail(result->error);
        },
        [this, target, saveRelative, backupPath](bool ok, const QString &message) {
            if (!ok) {
                if (message != QStringLiteral("已取消"))
                    showError(QStringLiteral("准备存档失败"), message);
                return;
            }

            if (!backupPath->isEmpty()) {
                statusBar()->showMessage(
                    QStringLiteral("C 盘原存档已保存到存档库：%1").arg(*backupPath), 10000);
            }
            refreshSaveTable();
            launchVerified(target, saveRelative);
        });
}

void MainWindow::launchVerified(const VersionInfo &version, const QString &saveRelative)
{
    LaunchRequest request;
    request.versionId = version.id;
    request.versionDir = version.path;
    request.logDir = QDir(PathUtils::appDataDir()).filePath(QStringLiteral("logs"));
    const int slash = saveRelative.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        request.worldName = saveRelative.left(slash);
        request.saveName = saveRelative.mid(slash + 1);
    }
    request.noEac = m_config.useNoEac;
    request.skipIntro = m_config.skipIntro;
    request.autoLoad = m_config.autoLoadSave && !request.worldName.isEmpty()
                      && !request.saveName.isEmpty();
    request.extraArgs = m_config.extraArgs;

    const LaunchResult result = GameLauncher::launch(request);
    if (!result.ok) {
        showError(QStringLiteral("启动失败"), result.error);
        return;
    }

    m_launchedVersionId = version.id;
    m_userDataWarned = false;
    m_logWatcher->start(result.logPath);

    statusBar()->showMessage(
        QStringLiteral("已启动 %1；日志：%2").arg(version.label, result.logPath), 12000);
}

void MainWindow::handleVersionDetected(const QString &version)
{
    VersionInfo *info = m_library.findVersion(m_launchedVersionId);
    if (!info)
        return;

    statusBar()->showMessage(QStringLiteral("游戏版本：%1").arg(version), 8000);
    if (info->detectedVersion == version)
        return;

    info->detectedVersion = version;
    QString error;
    if (!m_library.save(&error))
        qWarning("%s", qUtf8Printable(error));

    refreshVersionTable(info->id);
    refreshSaveTable();
    updateActionStates();
}

void MainWindow::handleUserDataFolder(const QString &path)
{
    const QString actualSaves = QDir::cleanPath(QDir(path).filePath(QStringLiteral("Saves")));
    const QString configured = QDir::cleanPath(m_config.savesRoot);

    if (actualSaves.compare(configured, Qt::CaseInsensitive) == 0 || m_userDataWarned)
        return;

    m_userDataWarned = true;
    QMessageBox::warning(
        this, QStringLiteral("存档目录不一致"),
        QStringLiteral("游戏实际使用的存档目录：\n%1\n\n工具配置的存档目录：\n%2\n\n"
                       "工具不会自动复制存档。请把设置中的存档目录改为游戏实际目录，"
                       "否则启动的存档与工具显示的存档可能不是同一份。")
            .arg(actualSaves, configured));
}

void MainWindow::backupSelectedSave()
{
    const int row = selectedSaveRow();
    if (row < 0)
        return;

    const SaveInfo save = m_saves.at(row);
    const VersionInfo *version = versionForSave(save, nullptr);
    const QString versionText = version ? versionDisplayName(*version)
                                        : save.lastSavedVersion;

    const QString backupRoot = m_config.backupRoot;
    const int keep = m_config.backupKeep;
    auto result = std::make_shared<BackupResult>();

    runTask(
        QStringLiteral("备份存档"),
        [result, save, backupRoot, versionText](TaskContext &context) {
            *result = BackupService::backupSave(
                save.directory, backupRoot, save.world, save.saveName, versionText,
                context.cancelFlag(),
                [&context](int percent, const QString &message) {
                    context.report(percent, message);
                });
            if (!result->ok && !result->canceled)
                context.fail(result->error);
        },
        [this, result, save, backupRoot, keep](bool ok, const QString &message) {
            if (!ok) {
                if (message != QStringLiteral("已取消"))
                    showError(QStringLiteral("备份失败"), message);
                return;
            }
            const int pruned = BackupService::pruneBackups(backupRoot, save.world,
                                                           save.saveName, keep);
            showInfo(QStringLiteral("备份完成"),
                     QStringLiteral("已备份到：\n%1\n\n清理旧备份：%2 份")
                         .arg(result->path)
                         .arg(pruned));
        });
}

void MainWindow::overrideSaveVersion()
{
    const int row = selectedSaveRow();
    if (row < 0)
        return;

    const SaveInfo save = m_saves.at(row);
    const QString currentId = m_library.resolveSaveVersion(save.world, save.saveName);

    QStringList labels;
    QVector<QString> ids;
    labels << QStringLiteral("自动匹配（清除手动指定）");
    ids << QString();

    int currentIndex = 0;
    for (const VersionInfo &version : m_library.versions) {
        labels << QStringLiteral("%1  [%2]").arg(versionDisplayName(version), version.id);
        ids << version.id;
        if (!currentId.isEmpty() && version.id == currentId)
            currentIndex = labels.size() - 1;
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, QStringLiteral("指定存档版本"),
        QStringLiteral("选择用于存档 %1 的游戏版本：").arg(save.displayName()), labels,
        currentIndex, false, &ok);
    if (!ok)
        return;

    const int index = labels.indexOf(chosen);
    if (index < 0 || index >= ids.size())
        return;

    const QString key = saveKey(save);
    if (ids.at(index).isEmpty())
        m_library.saveVersionOverrides.remove(key);
    else
        m_library.saveVersionOverrides.insert(key, ids.at(index));

    QString error;
    if (!m_library.save(&error))
        showError(QStringLiteral("保存版本库失败"), error);

    refreshSaveTable(rowKey(save));
    updateActionStates();
}

void MainWindow::openSelectedSaveDirectory()
{
    const int row = selectedSaveRow();
    if (row < 0)
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_saves.at(row).directory));
}

void MainWindow::openVersionRoot()
{
    QString error;
    if (!PathUtils::ensureDir(m_config.versionRoot, &error)) {
        showError(QStringLiteral("打开版本根目录"), error);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_config.versionRoot));
}

void MainWindow::openSavesRoot()
{
    if (!QFileInfo(m_config.savesRoot).isDir()) {
        showError(QStringLiteral("打开存档目录"),
                  QStringLiteral("存档目录不存在：%1").arg(m_config.savesRoot));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_config.savesRoot));
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_config, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_config = dialog.config();
    m_libraryRenameDeclined = false;

    QString error;
    m_library = Library::load(m_config.versionRoot, &error);
    if (!error.isEmpty())
        showError(QStringLiteral("加载版本库失败"), error);

    m_usageValid = false;
    refreshAll();
    scanStatistics();
}

void MainWindow::showError(const QString &title, const QString &text)
{
    QMessageBox::warning(this, title, text);
}

void MainWindow::showInfo(const QString &title, const QString &text)
{
    QMessageBox::information(this, title, text);
}
