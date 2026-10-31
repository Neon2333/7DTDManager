#pragma once

#include <QString>

// 从游戏版本串（如 "V 1.4 (b8)"）解析出的版本信息。
struct ParsedVersion
{
    int major = -1;
    int minor = -1;
    int build = -1;

    bool isValid() const { return major >= 0 && minor >= 0; }
    QString majorMinor() const;
    QString display() const;
};

// 解析版本串；支持 "V 1.4 (b8)"、"V1.4"、"1.4" 等写法。
ParsedVersion parseGameVersion(const QString &text);

// 返回 "主.次" 版本号（如 "1.4"）；无法解析时返回空串。
QString majorMinorVersion(const QString &text);

// 只比较主次版本号，build 号忽略。
bool sameMajorMinor(const QString &a, const QString &b);

// 从版本目录名解析标签："7 Days To DieV1.4" -> "V1.4"。
QString labelFromVersionFolder(const QString &folderName);

// 规范化用户输入的标签："1.4" / "V1.4" -> "V1.4"。
QString normalizeLabel(const QString &label);

// 生成版本目录名："V1.4" -> "7 Days To DieV1.4"。
QString versionFolderName(const QString &label);
