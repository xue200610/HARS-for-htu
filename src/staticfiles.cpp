#include "staticfiles.h"
#include "appconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

QByteArray mimeOf(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == QLatin1String("html") || s == QLatin1String("htm")) return QByteArrayLiteral("text/html; charset=utf-8");
    if (s == QLatin1String("css"))  return QByteArrayLiteral("text/css; charset=utf-8");
    if (s == QLatin1String("js"))   return QByteArrayLiteral("application/javascript; charset=utf-8");
    if (s == QLatin1String("json")) return QByteArrayLiteral("application/json; charset=utf-8");
    if (s == QLatin1String("svg"))  return QByteArrayLiteral("image/svg+xml");
    if (s == QLatin1String("png"))  return QByteArrayLiteral("image/png");
    if (s == QLatin1String("jpg") || s == QLatin1String("jpeg")) return QByteArrayLiteral("image/jpeg");
    if (s == QLatin1String("ico"))  return QByteArrayLiteral("image/x-icon");
    if (s == QLatin1String("woff2")) return QByteArrayLiteral("font/woff2");
    if (s == QLatin1String("map"))  return QByteArrayLiteral("application/json");
    return QByteArrayLiteral("application/octet-stream");
}

} // namespace

namespace staticfiles {

std::optional<QHttpServerResponse> serve(const QString &urlPath)
{
    QString rel = urlPath;
    if (rel.isEmpty() || rel == QLatin1String("/"))
        rel = QStringLiteral("index.html");
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);

    // 防目录穿越
    if (rel.contains(QLatin1String("..")) || rel.contains(QLatin1Char('\\')))
        return std::nullopt;

    const QString root = cfg::publicDir();
    QString full = root + QLatin1Char('/') + rel;

    // 没有扩展名时补 .html（/login -> login.html）
    if (!QFileInfo::exists(full) && !QFileInfo(rel).suffix().size())
        full += QStringLiteral(".html");

    const QFileInfo info(full);
    if (!info.exists() || !info.isFile())
        return std::nullopt;

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;

    const QByteArray data = file.readAll();
    return QHttpServerResponse(mimeOf(info.suffix()), data);
}

} // namespace staticfiles
