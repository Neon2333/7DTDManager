#pragma once

#include "app/AppConfig.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

// 设置对话框：路径、启动参数与去重排除规则。
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const AppConfig &config, QWidget *parent = nullptr);

    // 读取用户确认后的配置。
    AppConfig config() const { return m_config; }

protected:
    void accept() override;

private:
    // 创建“路径输入框 + 浏览按钮”一行，并把输入框指针写回 editOut。
    QWidget *createPathRow(QLineEdit **editOut, const QString &dialogTitle,
                           const QString &initialPath);

    AppConfig m_config;

    QLineEdit *m_versionRootEdit = nullptr;
    QLineEdit *m_savesRootEdit = nullptr;
    QLineEdit *m_saveLibraryRootEdit = nullptr;
    QLineEdit *m_steamRootEdit = nullptr;
    QCheckBox *m_noEacCheck = nullptr;
    QCheckBox *m_skipIntroCheck = nullptr;
    QCheckBox *m_autoLoadCheck = nullptr;
    QLineEdit *m_extraArgsEdit = nullptr;
    QPlainTextEdit *m_excludesEdit = nullptr;
};
