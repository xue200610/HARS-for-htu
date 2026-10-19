#include "../authutil.h"
#include "../dbutil.h"
#include "../routes.h"

#include <QDateTime>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

/* ==================== 统计与健康检查 ==================== */

namespace {

const char *kOverviewAllFields =
    " (SELECT COUNT(*) FROM appointments WHERE appt_time LIKE CONCAT(CURDATE(), '%')) AS today_appointments,"
    " (SELECT COUNT(*) FROM appointments WHERE status = '待就诊') AS waiting,"
    " (SELECT COUNT(*) FROM appointments WHERE status = '候诊中') AS queueing,"
    " (SELECT COUNT(*) FROM appointments WHERE status = '已就诊' AND appt_time LIKE CONCAT(CURDATE(), '%')) AS visited_today,"
    " (SELECT COUNT(*) FROM appointments WHERE status = '已取消') AS canceled,"
    " (SELECT COUNT(*) FROM schedules WHERE work_date = CURDATE() AND status = '正常') AS today_schedules,"
    " (SELECT IFNULL(SUM(total_quota - booked), 0) FROM schedules WHERE work_date >= CURDATE() AND status = '正常') AS remaining_quota,"
    " (SELECT COUNT(*) FROM patients) AS patients,"
    " (SELECT COUNT(*) FROM doctors) AS doctors,"
    " (SELECT COUNT(*) FROM departments) AS departments,"
    " (SELECT IFNULL(SUM(amount), 0) FROM fees WHERE status = '已结算') AS revenue,"
    " (SELECT COUNT(*) FROM fees WHERE status = '待结算') AS pending_fee,"
    " (SELECT COUNT(*) FROM inpatient WHERE status = '占用') AS beds_used,"
    " (SELECT COUNT(*) FROM inpatient WHERE status = '空床') AS beds_free";

const char *kOverviewDoctorFields =
    " (SELECT COUNT(*) FROM appointments WHERE doctor = ? AND appt_time LIKE CONCAT(CURDATE(), '%')) AS today_appointments,"
    " (SELECT COUNT(*) FROM appointments WHERE doctor = ? AND status = '待就诊') AS waiting,"
    " (SELECT COUNT(*) FROM appointments WHERE doctor = ? AND status = '候诊中') AS queueing,"
    " (SELECT COUNT(*) FROM appointments WHERE doctor = ? AND status = '已就诊' AND appt_time LIKE CONCAT(CURDATE(), '%')) AS visited_today,"
    " (SELECT COUNT(*) FROM appointments WHERE doctor = ? AND status = '已取消') AS canceled,"
    " (SELECT COUNT(*) FROM schedules WHERE doctor = ? AND work_date = CURDATE() AND status = '正常') AS today_schedules,"
    " (SELECT IFNULL(SUM(total_quota - booked), 0) FROM schedules WHERE doctor = ? AND work_date >= CURDATE() AND status = '正常') AS remaining_quota,"
    " (SELECT COUNT(*) FROM patients WHERE doctor = ?) AS patients,"
    " (SELECT COUNT(*) FROM doctors) AS doctors,"
    " (SELECT COUNT(*) FROM departments) AS departments";

} // namespace

void registerStatsRoutes(QHttpServer &server)
{
    /* ---------- 健康检查（含数据库探活） ---------- */
    server.route(QStringLiteral("/api/health"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &) -> QHttpServerResponse {
        static const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();

        QString database = QStringLiteral("ok");
        bool healthy = true;
        QSqlQuery probe(dbu::db());
        probe.prepare(QStringLiteral("SELECT 1"));
        if (!probe.exec() || !probe.next()) {
            healthy = false;
            database = probe.lastError().text();
        }

        return QHttpServerResponse(QJsonObject{
            {QStringLiteral("ok"), healthy},
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("service"), QStringLiteral("HARS")},
                {QStringLiteral("status"), healthy ? QStringLiteral("healthy") : QStringLiteral("degraded")},
                {QStringLiteral("database"), database},
                {QStringLiteral("uptime_seconds"), static_cast<double>((QDateTime::currentMSecsSinceEpoch() - startedAt) / 1000)},
                {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
                {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}
            }}
        }, healthy ? QHttpServerResponder::StatusCode::Ok
                   : QHttpServerResponder::StatusCode::ServiceUnavailable);
    });

    /* ---------- 首页统计卡片（管理员看全院，医生只看自己） ---------- */
    server.route(QStringLiteral("/api/stats/overview"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin"), QStringLiteral("doctor")})) return std::move(*denied);
        const QJsonObject user = authu::currentUser(req);
        const bool isDoctor = user.value(QStringLiteral("role")).toString() == QLatin1String("doctor");

        QSqlQuery query(dbu::db());
        if (isDoctor) {
            query.prepare(QStringLiteral("SELECT %1").arg(QString::fromLatin1(kOverviewDoctorFields)));
            for (int i = 0; i < 8; ++i)
                query.addBindValue(user.value(QStringLiteral("name")).toString());
        } else {
            query.prepare(QStringLiteral("SELECT %1").arg(QString::fromLatin1(kOverviewAllFields)));
        }

        QJsonObject row;
        QString error;
        if (!dbu::fetchOne(query, row, &error))
            return dbu::serverError(error);
        return dbu::ok(row);
    });

    /* ---------- 科室挂号分布（近 N 天） ---------- */
    server.route(QStringLiteral("/api/stats/dept-distribution"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin"), QStringLiteral("doctor")})) return std::move(*denied);
        int days = req.query().queryItemValue(QStringLiteral("days")).toInt();
        if (days <= 0 || days > 90) days = 7;

        QSqlQuery query(dbu::db());
        query.prepare(QStringLiteral(
            "SELECT IFNULL(dept, '未指定') AS dept, COUNT(*) AS count FROM appointments"
            " WHERE appt_time >= DATE_SUB(CURDATE(), INTERVAL %1 DAY)"
            " GROUP BY dept ORDER BY count DESC").arg(days - 1));
        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);
        return dbu::ok(rows);
    });

    /* ---------- 近 N 天挂号量趋势 ---------- */
    server.route(QStringLiteral("/api/stats/trend"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin"), QStringLiteral("doctor")})) return std::move(*denied);
        int days = req.query().queryItemValue(QStringLiteral("days")).toInt();
        if (days <= 0 || days > 90) days = 7;

        QSqlQuery query(dbu::db());
        query.prepare(QStringLiteral(
            "SELECT DATE(appt_time) AS day, COUNT(*) AS total,"
            "       SUM(status = '已就诊') AS visited, SUM(status = '已取消') AS canceled"
            "  FROM appointments WHERE appt_time >= DATE_SUB(CURDATE(), INTERVAL %1 DAY)"
            " GROUP BY DATE(appt_time) ORDER BY day").arg(days - 1));
        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);
        return dbu::ok(rows);
    });

    /* ---------- 医生工作量（近 N 天） ---------- */
    server.route(QStringLiteral("/api/stats/doctor-workload"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin"), QStringLiteral("doctor")})) return std::move(*denied);
        int days = req.query().queryItemValue(QStringLiteral("days")).toInt();
        if (days <= 0 || days > 365) days = 30;

        QSqlQuery query(dbu::db());
        query.prepare(QStringLiteral(
            "SELECT doctor, dept, COUNT(*) AS total, SUM(status = '已就诊') AS visited,"
            "       SUM(status IN ('待就诊', '候诊中')) AS pending"
            "  FROM appointments WHERE appt_time >= DATE_SUB(CURDATE(), INTERVAL %1 DAY)"
            " GROUP BY doctor, dept ORDER BY total DESC, doctor LIMIT 20").arg(days - 1));
        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);
        return dbu::ok(rows);
    });
}
