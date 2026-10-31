#pragma once

#include <QString>

// 路径、卷、文件系统与显示相关的通用工具。
namespace PathUtils {

// %APPDATA%\7DTDManager
QString appDataDir();

// 默认游戏存档目录：%APPDATA%\7DaysToDie\Saves
QString defaultSavesDir();

// 为 Win32 API 添加 \\?\ 长路径前缀（不改变 Qt 使用的路径）。
QString toWin32Path(const QString &path);

// 去掉 \\?\ 前缀，返回普通 Windows 路径。
QString fromWin32Path(const QString &path);

// 返回给定路径所在的卷根，例如 "G:\"。
QString volumeRoot(const QString &path);

// 返回卷序列号；失败返回 0。
quint32 volumeSerial(const QString &path);

// 判断路径所在卷是否为 NTFS。
bool isNtfs(const QString &path, QString *error = nullptr);

// 判断两个路径是否位于同一卷。
bool sameVolume(const QString &a, const QString &b, QString *error = nullptr);

// 判断路径是否位于 Steam 的 steamapps\common 下。
bool isUnderSteamCommon(const QString &path);

// 判断目录是否为完整游戏目录（exe + 7DaysToDie_Data + Data）。
bool isCompleteGameDir(const QString &path, QString *missing = nullptr);

// 判断路径是否存在且为目录。
bool isDirectory(const QString &path);

// 创建目录（含多级），失败时返回 false 并填充 error。
bool ensureDir(const QString &path, QString *error = nullptr);

// 以可读形式格式化字节数（如 "20.86 GB"）。
QString humanSize(qint64 bytes);

// 当前时间戳，形如 20261031_005041。
QString timestamp();

// 将相对路径统一为 '/' 分隔。
QString normalizedRelative(const QString &root, const QString &absolutePath);

} // namespace PathUtils
