#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {

QString envStr(const char *name, const QString &fallback)
{
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? fallback : QString::fromUtf8(v);
}

const QByteArray kDefaultSecret = "HARS-dev-secret-please-change";

} // namespace

namespace cfg {

QString dbHost()     { static const QString v = envStr("DB_HOST", QStringLiteral("127.0.0.1")); return v; }
int     dbPort()     { static const int v = envStr("DB_PORT", QStringLiteral("3306")).toInt(); return v; }
QString dbUser()     { static const QString v = envStr("DB_USER", QStringLiteral("root")); return v; }
// 密码只从环境变量 DB_PASSWORD 读（HARS.bat 会从 db.env 里设置它），代码里不留默认密码
QString dbPassword() { static const QString v = envStr("DB_PASSWORD", QStringLiteral("")); return v; }
QString dbName()     { static const QString v = envStr("DB_NAME", QStringLiteral("hospital_db")); return v; }

quint16 httpPort()
{
    static const quint16 v = static_cast<quint16>(envStr("PORT", QStringLiteral("8080")).toUShort());
    return v;
}

QString publicDir()
{
    static const QString v = [] {
        // 1) 环境变量优先
        const QString fromEnv = envStr("PUBLIC_DIR", QString());
        if (!fromEnv.isEmpty())
            return fromEnv;

        // 2) 可执行文件旁边（Qt Creator 直接运行时，工作目录不固定，所以两种都找）
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            appDir + QStringLiteral("/public"),
            appDir + QStringLiteral("/../public"),
            QDir::currentPath() + QStringLiteral("/public")
        };
        for (const QString &c : candidates) {
            if (QFileInfo::exists(c + QStringLiteral("/index.html")))
                return QDir(c).absolutePath();
        }
        return QDir::currentPath() + QStringLiteral("/public");
    }();
    return v;
}

QByteArray sessionSecret()
{
    static const QByteArray v = [] {
        const QByteArray fromEnv = qgetenv("SESSION_SECRET");
        return fromEnv.isEmpty() ? kDefaultSecret : fromEnv;
    }();
    return v;
}

bool usingDefaultSecret() { return sessionSecret() == kDefaultSecret; }

} // namespace cfg
