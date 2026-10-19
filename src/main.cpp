// ============================================================================
//  医院挂号管理系统 —— C++ 服务端（Qt QHttpServer + MySQL）
//
//  所以前端那 5 个页面（门户/登录/患者端/医生端/管理端）不用改一行就能跑。
//
//  分层：
//    main.cpp            启动：连库、注册路由、托管静态文件、监听端口
//    appconfig.*         配置（环境变量 + 默认值）
//    dbutil.*            数据库连接、查询封装、统一响应 { ok, data / message }
//    authutil.*          MD5 密码、HMAC-SHA256 令牌、角色守卫
//    staticfiles.*       public/ 下的前端页面与资源
//    routes/*.cpp        各业务模块的路由（auth / departments / doctors /
//                        schedules / appointments / patients / stats）
// ============================================================================

#include "appconfig.h"
#include "dbutil.h"
#include "routes.h"
#include "staticfiles.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTime>
#include <QDebug>

namespace {

/** 本机可对外访问的 IPv4（用于打印"发给别人用"的地址） */
QStringList lanAddresses()
{
    QStringList out;
    for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
            out << addr.toString();
    }
    out.sort();
    return out;
}

void printBanner(quint16 port)
{
    const QString pub = cfg::publicDir();
    const bool hasFrontend = QFileInfo::exists(pub + QStringLiteral("/index.html"));

    qInfo().noquote() << "";
    qInfo().noquote() << "  ┌──────────────────────────────────────────────────────────┐";
    qInfo().noquote() << "  │  医院挂号管理系统 · C++ 服务端（Qt QHttpServer）已启动    │";
    qInfo().noquote() << "  └──────────────────────────────────────────────────────────┘";
    qInfo().noquote() << QStringLiteral("  本机访问   : http://127.0.0.1:%1").arg(port);
    const QStringList lans = lanAddresses();
    if (!lans.isEmpty()) {
        qInfo().noquote() << "  发给别人用 :";
        for (const QString &ip : lans)
            qInfo().noquote() << QStringLiteral("               http://%1:%2").arg(ip).arg(port);
    }
    qInfo().noquote() << QStringLiteral("  数据库     : %1@%2:%3/%4")
                             .arg(cfg::dbUser(), cfg::dbHost()).arg(cfg::dbPort()).arg(cfg::dbName());
    qInfo().noquote() << QStringLiteral("  前端页面   : %1").arg(pub);
    if (!hasFrontend) {
        qInfo().noquote() << "";
        qInfo().noquote() << "  ⚠ 没有找到 public/index.html，浏览器打开会是 404。";
        qInfo().noquote() << "    请先执行： .\\sync-frontend.ps1   （把前端页面同步过来）";
    }
    qInfo().noquote() << "";
    qInfo().noquote() << "  演示账号（密码均为 123456）: admin / doctor1 / doctor2 / patient1";
    if (cfg::usingDefaultSecret()) {
        qInfo().noquote() << "";
        qInfo().noquote() << "  ⚠ 安全提醒：正在使用默认 SESSION_SECRET，公网部署前请改掉。";
    }
    qInfo().noquote() << "";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    /* ---------- 数据库 ---------- */
    QString dbError;
    if (!dbu::open(&dbError)) {
        qWarning().noquote() << "数据库连接失败：" << dbError;
        qWarning().noquote() << "提示：QMYSQL 驱动需要能加载 libmysql.dll。用 MinGW 套件时";
        qWarning().noquote() << "      Qt 的 sqldrivers 目录里已经带了，一般不用手动复制。";
        return 1;
    }
    qInfo().noquote() << QStringLiteral("已连接数据库 %1@%2:%3/%4")
                             .arg(cfg::dbUser(), cfg::dbHost()).arg(cfg::dbPort()).arg(cfg::dbName());

    QHttpServer server;

    /* ---------- 访问日志 ----------
     * 每条请求都打印：来源 IP / 方法 / 路径 / 状态码。
     * 排查"别人连不上"时这条最有用：
     *   · 别人试了，这里【没有】日志 → 请求根本没到（不在同一网络 / 防火墙 / 他地址写错了）
     *   · 别人试了，这里【有】日志   → 请求到了，问题在返回内容或他的浏览器
     */
    server.addAfterRequestHandler(&server, [](const QHttpServerRequest &req, QHttpServerResponse &resp) {
        auto methodName = [](QHttpServerRequest::Method m) -> QString {
            if (m == QHttpServerRequest::Method::Get)    return QStringLiteral("GET");
            if (m == QHttpServerRequest::Method::Post)   return QStringLiteral("POST");
            if (m == QHttpServerRequest::Method::Put)    return QStringLiteral("PUT");
            if (m == QHttpServerRequest::Method::Delete) return QStringLiteral("DELETE");
            return QString::number(static_cast<int>(m));
        };
        // 双栈监听时来源会显示成 ::ffff:192.168.0.198，去掉前缀更好认
        QString clientIp = req.remoteAddress().toString();
        if (clientIp.startsWith(QLatin1String("::ffff:")))
            clientIp = clientIp.mid(7);

        qInfo().noquote() << QStringLiteral("[%1] %2 %3 %4 -> %5")
                                 .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                                 .arg(clientIp, -15)
                                 .arg(methodName(req.method()), -6)
                                 .arg(req.url().path(), -34)
                                 .arg(static_cast<int>(resp.statusCode()));
    });

    /* ---------- 业务路由 ---------- */
    registerAuthRoutes(server);
    registerDepartmentRoutes(server);
    registerDoctorRoutes(server);
    registerScheduleRoutes(server);
    registerAppointmentRoutes(server);
    registerPatientRoutes(server);
    registerStatsRoutes(server);

    /* ---------- 没匹配上的请求：/api 返回 JSON 404，其余当静态文件 ---------- */
    // 注意 setMissingHandler 的签名要求 QHttpServerResponder&（左值引用）
    server.setMissingHandler(&server, [](const QHttpServerRequest &req, QHttpServerResponder &responder) {
        const QString path = req.url().path();
        if (path.startsWith(QLatin1String("/api/"))) {
            responder.sendResponse(dbu::fail(QStringLiteral("接口不存在：%1").arg(path),
                                             QHttpServerResponder::StatusCode::NotFound));
            return;
        }
        if (auto file = staticfiles::serve(path)) {
            responder.sendResponse(*file);
            return;
        }
        responder.sendResponse(dbu::fail(QStringLiteral("页面不存在：%1").arg(path),
                                         QHttpServerResponder::StatusCode::NotFound));
    });

    /* ---------- 监听 ---------- */
    // Qt 6.12 的 QHttpServer 没有 listen()，要自己建 QTcpServer 再 bind
    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::Any, cfg::httpPort())) {
        qWarning().noquote() << QStringLiteral("监听失败：端口 %1 可能已被占用（%2）")
                                    .arg(cfg::httpPort()).arg(tcpServer.errorString());
        return 1;
    }
    if (!server.bind(&tcpServer)) {
        qWarning().noquote() << "QHttpServer 绑定失败";
        return 1;
    }

    const QList<quint16> ports = server.serverPorts();
    printBanner(ports.isEmpty() ? cfg::httpPort() : ports.first());

    return app.exec();
}
