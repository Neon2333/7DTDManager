#include "HashService.h"

#include <QCryptographicHash>
#include <QFile>

QByteArray HashService::sha256File(const QString &path, QString *error, const std::atomic_bool *cancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取文件 %1：%2").arg(path, file.errorString());
        return {};
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    constexpr qint64 chunkSize = 1024 * 1024;

    while (!file.atEnd()) {
        if (cancel && cancel->load()) {
            if (error)
                *error = QStringLiteral("已取消");
            return {};
        }
        const QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error)
                *error = QStringLiteral("读取文件失败：%1").arg(file.errorString());
            return {};
        }
        hasher.addData(chunk);
    }

    return hasher.result().toHex();
}

QByteArray HashService::toHex(const QByteArray &digest)
{
    return digest.toHex();
}
