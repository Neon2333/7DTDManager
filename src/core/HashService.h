#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>

// 文件哈希计算。
class HashService
{
public:
    // 流式计算文件的 SHA-256，返回十六进制摘要；失败或取消返回空数组。
    static QByteArray sha256File(const QString &path, QString *error = nullptr,
                                 const std::atomic_bool *cancel = nullptr);

    // 将二进制摘要转为小写十六进制字符串。
    static QByteArray toHex(const QByteArray &digest);
};
