#pragma once

#include "app/AppConfig.h"
#include "app/Library.h"
#include "core/SaveScanner.h"
#include "core/TaskRunner.h"

#include <QMainWindow>
#include <QVector>

#include <functional>

class QCloseEvent;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class LogWatcher;
class TaskRunner;

// 主窗口：版本表 + 存档表 + 导入/基准/去重/启动/备份操作。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUi();
    void setupMenus();
    void loadState();
    void refreshAll();
    void refreshVersionTable(const QString &selectId = QString());
    void refreshSaveTable(const QString &selectKey = QString());
    void updateActionStates();
    void updateSummary();

    QString selectedVersionId() const;
    int selectedSaveRow() const;
    const VersionInfo *versionForSave(const SaveInfo &save, bool *matched) const;
    static QString saveKey(const SaveInfo &save);
    static QString versionDisplayName(const VersionInfo &version);

    void runTask(const QString &title, TaskRunner::Task task,
                 std::function<void(bool, const QString &)> onFinished);
    void scanStatistics();
    void importSteamVersion();
    void addExistingVersion();
    void chooseBaseVersionDirectory();
    void setSelectedAsBase();
    void removeSelectedVersion();
    void dedupSelectedVersion();
    void verifySelectedBase();
    void breakLinksSelectedVersion();
    void startSelectedGame();
    void launchSelectedVersion();
    void backupSelectedSave();
    void overrideSaveVersion();
    void openSelectedSaveDirectory();
    void openVersionRoot();
    void openSavesRoot();
    void openSettings();

    void launchVerified(const VersionInfo &version, const QString &saveRelative);
    void handleVersionDetected(const QString &version);
    void handleUserDataFolder(const QString &path);
    void onTaskProgress(int percent, const QString &message);
    void onTaskFinished(bool ok, const QString &message);
    void updateTaskUi(bool running);

    void showError(const QString &title, const QString &text);
    void showInfo(const QString &title, const QString &text);

    AppConfig m_config;
    Library m_library;
    QVector<SaveInfo> m_saves;

    QTableWidget *m_versionTable = nullptr;
    QTableWidget *m_saveTable = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_rootLabel = nullptr;
    QLabel *m_savesLabel = nullptr;
    QLabel *m_taskLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_cancelTaskButton = nullptr;

    QPushButton *m_importButton = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_baseButton = nullptr;
    QPushButton *m_setBaseButton = nullptr;
    QPushButton *m_dedupButton = nullptr;
    QPushButton *m_verifyButton = nullptr;
    QPushButton *m_breakButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_launchVersionButton = nullptr;

    QPushButton *m_startButton = nullptr;
    QPushButton *m_backupButton = nullptr;
    QPushButton *m_overrideButton = nullptr;
    QPushButton *m_openSaveButton = nullptr;
    QPushButton *m_refreshButton = nullptr;

    TaskRunner *m_taskRunner = nullptr;
    LogWatcher *m_logWatcher = nullptr;
    std::function<void(bool, const QString &)> m_taskCallback;
    QString m_launchedVersionId;
    bool m_userDataWarned = false;
    bool m_usageValid = false;
    qint64 m_globalLogicalBytes = 0;
    qint64 m_globalPhysicalBytes = 0;
    int m_globalFileCount = 0;
};
