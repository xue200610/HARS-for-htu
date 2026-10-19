#include "route-helpers.h"
#include "../authutil.h"
#include "../dbutil.h"
#include "../routes.h"

#include <QDate>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>

/* ==================== 号源排班 ==================== */

namespace {

const char *kScheduleFields =
    "id, doctor_id, doctor, dept, work_date, period, start_time, end_time, room,"
    " source_type, price, total_quota, booked,"
    " (total_quota - booked) AS remaining, status";

} // namespace

void registerScheduleRoutes(QHttpServer &server)
{
    // 号源列表：dept / doctor_id / date / from / days / onlyAvailable
    server.route(QStringLiteral("/api/schedules"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        const QUrlQuery q(req.query());
        const QString dept = q.queryItemValue(QStringLiteral("dept"));
        const QString doctorId = q.queryItemValue(QStringLiteral("doctor_id"));
        const QString date = q.queryItemValue(QStringLiteral("date"));
        const QString from = q.queryItemValue(QStringLiteral("from"));
        int days = q.queryItemValue(QStringLiteral("days")).toInt();
        if (days <= 0 || days > 60) days = 14;
        const bool onlyAvailable = q.queryItemValue(QStringLiteral("onlyAvailable")) == QLatin1String("1")
                                || q.queryItemValue(QStringLiteral("onlyAvailable")) == QLatin1String("true");

        QString start = !date.isEmpty() ? date : (!from.isEmpty() ? from : QDate::currentDate().toString(Qt::ISODate));
        QString end = !date.isEmpty()
                          ? date
                          : QDate::fromString(start, Qt::ISODate).addDays(days - 1).toString(Qt::ISODate);

        QString sql = QStringLiteral("SELECT %1 FROM schedules WHERE work_date BETWEEN ? AND ?")
                          .arg(QString::fromLatin1(kScheduleFields));
        if (!dept.isEmpty())     sql += QStringLiteral(" AND dept = ?");
        if (!doctorId.isEmpty()) sql += QStringLiteral(" AND doctor_id = ?");
        if (onlyAvailable)       sql += QStringLiteral(" AND status = '正常' AND total_quota > booked");
        sql += QStringLiteral(" ORDER BY work_date, IF(period = '上午', 0, 1), doctor");

        QSqlQuery query(dbu::db());
        query.prepare(sql);
        query.addBindValue(start);
        query.addBindValue(end);
        if (!dept.isEmpty())     query.addBindValue(dept);
        if (!doctorId.isEmpty()) query.addBindValue(doctorId.toInt());

        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);
        return dbu::ok(rows);
    });

    // 有号源的日期列表
    server.route(QStringLiteral("/api/schedules/dates"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        const QUrlQuery q(req.query());
        const QString dept = q.queryItemValue(QStringLiteral("dept"));
        const QString doctorId = q.queryItemValue(QStringLiteral("doctor_id"));
        int days = q.queryItemValue(QStringLiteral("days")).toInt();
        if (days <= 0 || days > 60) days = 14;

        QString sql = QStringLiteral(
            "SELECT work_date, COUNT(*) AS schedule_count, SUM(total_quota - booked) AS remaining"
            "  FROM schedules WHERE status = '正常' AND work_date BETWEEN CURDATE() AND DATE_ADD(CURDATE(), INTERVAL %1 DAY)")
                          .arg(days - 1);
        if (!dept.isEmpty())     sql += QStringLiteral(" AND dept = ?");
        if (!doctorId.isEmpty()) sql += QStringLiteral(" AND doctor_id = ?");
        sql += QStringLiteral(" GROUP BY work_date ORDER BY work_date");

        QSqlQuery query(dbu::db());
        query.prepare(sql);
        if (!dept.isEmpty())     query.addBindValue(dept);
        if (!doctorId.isEmpty()) query.addBindValue(doctorId.toInt());

        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);
        return dbu::ok(rows);
    });

    // 批量生成号源（管理员）
    server.route(QStringLiteral("/api/schedules/generate"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);
        int days = dbu::jsonInt(authu::body(req).value(QStringLiteral("days")));
        if (days <= 0 || days > 60) days = 14;

        QString error;
        const QJsonObject result = routehelp::generateSchedules(days, &error);
        if (!error.isEmpty())
            return dbu::serverError(error);
        return dbu::ok(result);
    });

    // 新增单条号源（管理员）
    server.route(QStringLiteral("/api/schedules"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);
        const QJsonObject in = authu::body(req);
        const int doctorId = dbu::jsonInt(in.value(QStringLiteral("doctor_id")));
        const QString workDate = in.value(QStringLiteral("work_date")).toString();
        const QString period = in.value(QStringLiteral("period")).toString();

        QString error;
        QSqlQuery doc(dbu::db());
        doc.prepare(QStringLiteral("SELECT * FROM doctors WHERE id = ?"));
        doc.addBindValue(doctorId);
        QJsonObject doctor;
        if (!dbu::fetchOne(doc, doctor, &error))
            return dbu::serverError(error);
        if (doctor.isEmpty())
            return dbu::fail(QStringLiteral("请选择医生"));
        if (workDate.isEmpty() || period.isEmpty())
            return dbu::fail(QStringLiteral("请选择出诊日期和时段"));

        QSqlQuery dup(dbu::db());
        dup.prepare(QStringLiteral("SELECT id FROM schedules WHERE doctor_id = ? AND work_date = ? AND period = ?"));
        dup.addBindValue(doctorId);
        dup.addBindValue(workDate);
        dup.addBindValue(period);
        QJsonObject exists;
        if (!dbu::fetchOne(dup, exists, &error))
            return dbu::serverError(error);
        if (!exists.isEmpty())
            return dbu::fail(QStringLiteral("该医生在这个时段已有排班"), QHttpServerResponder::StatusCode::Conflict);

        const bool morning = period == QStringLiteral("上午");
        QSqlQuery ins(dbu::db());
        ins.prepare(QStringLiteral(
            "INSERT INTO schedules (doctor_id, doctor, dept, work_date, period, start_time, end_time,"
            " room, source_type, price, total_quota, booked, status) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?)"));
        ins.addBindValue(doctorId);
        ins.addBindValue(doctor.value(QStringLiteral("name")).toString());
        ins.addBindValue(doctor.value(QStringLiteral("dept")).toString());
        ins.addBindValue(workDate);
        ins.addBindValue(period);
        ins.addBindValue(in.value(QStringLiteral("start_time")).toString().isEmpty()
                             ? (morning ? QStringLiteral("08:30") : QStringLiteral("14:00"))
                             : in.value(QStringLiteral("start_time")).toString());
        ins.addBindValue(in.value(QStringLiteral("end_time")).toString().isEmpty()
                             ? (morning ? QStringLiteral("12:00") : QStringLiteral("17:30"))
                             : in.value(QStringLiteral("end_time")).toString());
        ins.addBindValue(in.value(QStringLiteral("room")).toString().isEmpty()
                             ? routehelp::roomOf(doctor.value(QStringLiteral("dept")).toString())
                             : in.value(QStringLiteral("room")).toString());
        ins.addBindValue(in.value(QStringLiteral("source_type")).toString().isEmpty()
                             ? QStringLiteral("普通号") : in.value(QStringLiteral("source_type")).toString());
        ins.addBindValue(in.contains(QStringLiteral("price"))
                             ? dbu::jsonDouble(in.value(QStringLiteral("price"))) : 10.00);
        const int quota = dbu::jsonInt(in.value(QStringLiteral("total_quota")));
        ins.addBindValue(quota > 0 ? quota : 20);
        ins.addBindValue(in.value(QStringLiteral("status")).toString().isEmpty()
                             ? QStringLiteral("正常") : in.value(QStringLiteral("status")).toString());
        if (!dbu::exec(ins, &error))
            return dbu::serverError(error);

        return dbu::created(QJsonObject{{QStringLiteral("id"), ins.lastInsertId().toInt()}});
    });

    // 调整号源（管理员）
    server.route(QStringLiteral("/api/schedules/<arg>"), QHttpServerRequest::Method::Put,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM schedules WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject s;
        if (!dbu::fetchOne(find, s, &error))
            return dbu::serverError(error);
        if (s.isEmpty())
            return dbu::fail(QStringLiteral("号源不存在"), QHttpServerResponder::StatusCode::NotFound);

        const QJsonObject in = authu::body(req);
        const int booked = s.value(QStringLiteral("booked")).toInt();
        const int quota = in.contains(QStringLiteral("total_quota"))
                              ? dbu::jsonInt(in.value(QStringLiteral("total_quota")))
                              : s.value(QStringLiteral("total_quota")).toInt();
        if (quota < booked)
            return dbu::fail(QStringLiteral("号源总量不能小于已挂号数 %1").arg(booked));

        QSqlQuery upd(dbu::db());
        upd.prepare(QStringLiteral(
            "UPDATE schedules SET period = ?, start_time = ?, end_time = ?, room = ?,"
            " source_type = ?, price = ?, total_quota = ?, status = ? WHERE id = ?"));
        upd.addBindValue(in.contains(QStringLiteral("period"))
                             ? in.value(QStringLiteral("period")).toString()
                             : s.value(QStringLiteral("period")).toString());
        upd.addBindValue(in.contains(QStringLiteral("start_time"))
                             ? in.value(QStringLiteral("start_time")).toString()
                             : s.value(QStringLiteral("start_time")).toString());
        upd.addBindValue(in.contains(QStringLiteral("end_time"))
                             ? in.value(QStringLiteral("end_time")).toString()
                             : s.value(QStringLiteral("end_time")).toString());
        upd.addBindValue(in.contains(QStringLiteral("room"))
                             ? in.value(QStringLiteral("room")).toString()
                             : s.value(QStringLiteral("room")).toString());
        upd.addBindValue(in.contains(QStringLiteral("source_type"))
                             ? in.value(QStringLiteral("source_type")).toString()
                             : s.value(QStringLiteral("source_type")).toString());
        upd.addBindValue(in.contains(QStringLiteral("price"))
                             ? dbu::jsonDouble(in.value(QStringLiteral("price")))
                             : s.value(QStringLiteral("price")).toDouble());
        upd.addBindValue(quota);
        upd.addBindValue(in.contains(QStringLiteral("status"))
                             ? in.value(QStringLiteral("status")).toString()
                             : s.value(QStringLiteral("status")).toString());
        upd.addBindValue(id);
        if (!dbu::exec(upd, &error))
            return dbu::serverError(error);

        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}});
    });

    // 删除号源（管理员，已有挂号不让删）
    server.route(QStringLiteral("/api/schedules/<arg>"), QHttpServerRequest::Method::Delete,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM schedules WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject s;
        if (!dbu::fetchOne(find, s, &error))
            return dbu::serverError(error);
        if (s.isEmpty())
            return dbu::fail(QStringLiteral("号源不存在"), QHttpServerResponder::StatusCode::NotFound);
        if (s.value(QStringLiteral("booked")).toInt() > 0)
            return dbu::fail(QStringLiteral("该号源已有挂号记录，不能删除，可改为\"停诊\""));

        QSqlQuery del(dbu::db());
        del.prepare(QStringLiteral("DELETE FROM schedules WHERE id = ?"));
        del.addBindValue(id);
        if (!dbu::exec(del, &error))
            return dbu::serverError(error);

        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}});
    });
}
