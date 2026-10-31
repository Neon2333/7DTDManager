#include "Version.h"

#include <QRegularExpression>

namespace {

// 匹配可选的 V 前缀、主版本、次版本，以及可选的 (bN) 构建号。
const QRegularExpression &versionRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"((?:^|[^0-9])V?\s*(\d+)\.(\d+)(?:\s*\(\s*b(\d+)\s*\))?)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

QString ParsedVersion::majorMinor() const
{
    if (!isValid())
        return QString();
    return QStringLiteral("%1.%2").arg(major).arg(minor);
}

QString ParsedVersion::display() const
{
    if (!isValid())
        return QString();
    if (build >= 0)
        return QStringLiteral("V %1.%2 (b%3)").arg(major).arg(minor).arg(build);
    return QStringLiteral("V %1.%2").arg(major).arg(minor);
}

ParsedVersion parseGameVersion(const QString &text)
{
    ParsedVersion result;
    const auto match = versionRegex().match(text);
    if (!match.hasMatch())
        return result;

    bool ok = false;
    result.major = match.captured(1).toInt(&ok);
    if (!ok)
        return ParsedVersion{};
    result.minor = match.captured(2).toInt(&ok);
    if (!ok)
        return ParsedVersion{};

    const QString buildText = match.captured(3);
    if (!buildText.isEmpty())
        result.build = buildText.toInt(&ok);
    return result;
}

QString majorMinorVersion(const QString &text)
{
    return parseGameVersion(text).majorMinor();
}

bool sameMajorMinor(const QString &a, const QString &b)
{
    const ParsedVersion va = parseGameVersion(a);
    const ParsedVersion vb = parseGameVersion(b);
    if (!va.isValid() || !vb.isValid())
        return false;
    return va.major == vb.major && va.minor == vb.minor;
}

QString labelFromVersionFolder(const QString &folderName)
{
    static const QRegularExpression gameFolderRe(
        QStringLiteral(R"(7\s*Days\s*To\s*Die\s*V?\s*(\d+\.\d+))"),
        QRegularExpression::CaseInsensitiveOption);

    auto match = gameFolderRe.match(folderName);
    if (match.hasMatch())
        return QStringLiteral("V%1").arg(match.captured(1));

    static const QRegularExpression anyVersionRe(
        QStringLiteral(R"(^V?\s*(\d+\.\d+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    match = anyVersionRe.match(folderName.trimmed());
    if (match.hasMatch())
        return QStringLiteral("V%1").arg(match.captured(1));

    return folderName.trimmed();
}

QString normalizeLabel(const QString &label)
{
    const QString trimmed = label.trimmed();
    static const QRegularExpression versionOnlyRe(
        QStringLiteral(R"(^V?\s*(\d+)\.(\d+)$)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = versionOnlyRe.match(trimmed);
    if (match.hasMatch())
        return QStringLiteral("V%1.%2").arg(match.captured(1), match.captured(2));

    return trimmed;
}

QString versionFolderName(const QString &label)
{
    const QString normalized = normalizeLabel(label);
    static const QRegularExpression vRe(QStringLiteral(R"(^V(.*)$)"),
                                        QRegularExpression::CaseInsensitiveOption);
    const auto match = vRe.match(normalized);
    const QString suffix = match.hasMatch() ? match.captured(1) : normalized;
    return QStringLiteral("7 Days To DieV%1").arg(suffix);
}
