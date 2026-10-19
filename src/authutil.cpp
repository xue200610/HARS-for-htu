#include "authutil.h"
#include "appconfig.h"
#include "dbutil.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QUrlQuery>

namespace {

QByteArray base64Url(const QByteArray &raw)
{
    QByteArray b = raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return b;
}

QByteArray base64UrlDecode(const QByteArray &text)
{
    return QByteArray::fromBase64(text, QByteArray::Base64UrlEncoding);
}

/** 定长比较，避免时序侧信道 */
bool constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

} // namespace

namespace authu {

QString md5(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString signToken(const QJsonObject &user)
{
    QJsonObject payload = user;
    payload.insert(QStringLiteral("exp"),
                   static_cast<double>(QDateTime::currentMSecsSinceEpoch()) + 8.0 * 3600 * 1000);
    const QByteArray bodyText = base64Url(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const QByteArray sig = base64Url(
        QMessageAuthenticationCode::hash(bodyText, cfg::sessionSecret(), QCryptographicHash::Sha256));
    return QString::fromLatin1(bodyText + '.' + sig);
}

QJsonObject verifyToken(const QString &token)
{
    const int dot = token.indexOf(QLatin1Char('.'));
    if (dot <= 0)
        return {};

    const QByteArray bodyText = token.left(dot).toLatin1();
    const QByteArray givenSig = token.mid(dot + 1).toLatin1();
    const QByteArray expectSig = base64Url(
        QMessageAuthenticationCode::hash(bodyText, cfg::sessionSecret(), QCryptographicHash::Sha256));

    if (!constantTimeEquals(givenSig, expectSig))
        return {};

    const QJsonObject payload = QJsonDocument::fromJson(base64UrlDecode(bodyText)).object();
    const double exp = payload.value(QStringLiteral("exp")).toDouble();
    if (exp <= 0 || exp < static_cast<double>(QDateTime::currentMSecsSinceEpoch()))
        return {};
    return payload;
}

QString bearerToken(const QHttpServerRequest &req)
{
    const QString header = req.headers().value(QByteArrayLiteral("authorization")).toByteArray();
    if (header.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        return header.mid(7).trimmed();
    return req.query().queryItemValue(QStringLiteral("token")).trimmed();
}

QJsonObject currentUser(const QHttpServerRequest &req)
{
    return verifyToken(bearerToken(req));
}

std::optional<QHttpServerResponse> check(const QHttpServerRequest &req, const QStringList &roles)
{
    const QJsonObject user = currentUser(req);
    if (user.isEmpty())
        return dbu::fail(QStringLiteral("请先登录"), QHttpServerResponder::StatusCode::Unauthorized);

    if (!roles.isEmpty()) {
        const QString role = user.value(QStringLiteral("role")).toString();
        if (!roles.contains(role)) {
            return dbu::fail(QStringLiteral("当前角色无权访问该功能"),
                             QHttpServerResponder::StatusCode::Forbidden);
        }
    }
    return std::nullopt;
}

QJsonObject body(const QHttpServerRequest &req)
{
    return QJsonDocument::fromJson(req.body()).object();
}

QString roleName(const QString &role)
{
    if (role == QLatin1String("admin"))    return QStringLiteral("管理员");
    if (role == QLatin1String("doctor"))   return QStringLiteral("医生");
    if (role == QLatin1String("patient"))  return QStringLiteral("患者");
    return role;
}

} // namespace authu
