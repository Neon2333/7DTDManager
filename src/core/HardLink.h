#pragma once

#include <QByteArray>
#include <QString>

// 文件身份信息：用于识别硬链接（同一文件的不同路径具有相同 fileId）。
struct FileIdentity
{
    quint32 volumeSerial = 0;
    quint64 fileIndex = 0;
    quint32 linkCount = 0;
    bool valid = false;

    // 组合卷序列号与文件索引，作为同一文件的唯一键。
    QString key() const;
};

// 创建硬链接 linkPath -> targetPath（同一 NTFS 卷）。
bool createHardLink(const QString &linkPath, const QString &targetPath, QString *error = nullptr);

// 查询文件的卷序列号、文件索引与链接数。
bool queryFileIdentity(const QString &path, FileIdentity *identity, QString *error = nullptr);

// 把文件内容复制到新文件并替换原路径，从而解除硬链接（copy-on-write）。
bool breakHardLink(const QString &path, QString *error = nullptr);
