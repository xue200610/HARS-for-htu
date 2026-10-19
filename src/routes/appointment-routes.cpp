#include "route-helpers.h"
#include "../authutil.h"
#include "../dbutil.h"
#include "../routes.h"

#include <QDate>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

/* ==================== 挂号业务（含事务与防超卖） ==================== */

namespace {

const char *kApptFields =
    "id, patient_id, patient_name, dept, doctor, source_type, appt_time, status,"
    " schedule_id, room, price, queue_no, phone, created_at";

/** 把 null 的 patient_id 当成 -1，避免和"没有绑定患者档案"的 0 混淆 */
int patientIdOf(const QJsonObject &appt)
{
    const QJsonValue v = appt.value(QStringLiteral("patient_id"));
    return (v.isNull() || v.isUndefined()) ? -1 : v.toInt();
}

bool isActiveStatus(const QString &status)
{
    return status == QStringLiteral("待就诊") || status == QStringLiteral("候诊中");
}

/** 这条挂号记录该不该由当前用户操作 */
bool ownsAppointment(const QJsonObject &user, const QJsonObject &appt)
{
    const QString role = user.value(QStringLiteral("role")).toString();
    if (role == QLatin1String("admin"))
        return true;
    if (role == QLatin1String("doctor"))
        return appt.value(QStringLiteral("doctor")).toString() == user.value(QStringLiteral("name")).toString();
    if (role == QLatin1String("patient")) {
        return patientIdOf(appt) == user.value(QStringLiteral("patient_id")).toInt()
            || appt.value(QStringLiteral("patient_name")).toString() == user.value(QStringLiteral("name")).toString();
    }
    return false;
}

} // namespace

void registerAppointmentRoutes(QHttpServer &server)
{
    /* ---------- 挂号记录列表（按角色自动限定范围） ---------- */
    server.route(QStringLiteral("/api/appointments"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req)) return std::move(*denied);
        const QJsonObject user = authu::currentUser(req);

        const QUrlQuery q(req.query());
        const QString status  = q.queryItemValue(QStringLiteral("status"));
        const QString date    = q.queryItemValue(QStringLiteral("date"));
        const QString dept    = q.queryItemValue(QStringLiteral("dept"));
        const QString keyword = q.queryItemValue(QStringLiteral("keyword"));
        const QString scope   = q.queryItemValue(QStringLiteral("scope"));
        const QString role    = user.value(QStringLiteral("role")).toString();

        QStringList where;
        QVariantList binds;
        where << QStringLiteral("1 = 1");   // 占位，避免拼出空 WHERE

        const bool wantAll = scope == QLatin1String("all")
                          && (role == QLatin1String("admin") || role == QLatin1String("doctor"));
        if (wantAll) {
            if (role == QLatin1String("doctor")) {
                where << QStringLiteral("doctor = ?");
                binds << user.value(QStringLiteral("name")).toString();
            }
        } else if (role == QLatin1String("admin")) {
            // 全部
        } else if (role == QLatin1String("doctor")) {
            where << QStringLiteral("doctor = ?");
            binds << user.value(QStringLiteral("name")).toString();
        } else {
            const int pid = user.value(QStringLiteral("patient_id")).toInt();
            if (pid > 0) {
                where << QStringLiteral("patient_id = ?");
                binds << pid;
            } else {
                where << QStringLiteral("(patient_id IS NULL OR patient_id = 0) AND patient_name = ?");
                binds << user.value(QStringLiteral("name")).toString();
            }
        }

        if (!status.isEmpty())  { where << QStringLiteral("status = ?");        binds << status; }
        if (!date.isEmpty())    { where << QStringLiteral("appt_time LIKE ?");  binds << (date + QStringLiteral("%")); }
        if (!dept.isEmpty())    { where << QStringLiteral("dept = ?");          binds << dept; }
        if (!keyword.isEmpty()) {
            where << QStringLiteral("(patient_name LIKE ? OR doctor LIKE ? OR IFNULL(phone, \"\") LIKE ?)");
            binds << QStringLiteral("%1%").arg(keyword) << QStringLiteral("%1%").arg(keyword)
                  << QStringLiteral("%1%").arg(keyword);
        }

        QSqlQuery query(dbu::db());
        query.prepare(QStringLiteral("SELECT %1 FROM appointments WHERE %2 "
                                     "ORDER BY (status = '候诊中') DESC, appt_time, id LIMIT 200")
                          .arg(QString::fromLatin1(kApptFields), where.join(QStringLiteral(" AND "))));
        for (const QVariant &b : binds)
            query.addBindValue(b);

        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);
        return dbu::ok(rows);
    });

    /* ---------- 挂号：患者给自己挂；医生/管理员可代患者挂 ---------- */
    server.route(QStringLiteral("/api/appointments"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req)) return std::move(*denied);
        const QJsonObject user = authu::currentUser(req);
        const QJsonObject in = authu::body(req);
        const int scheduleId = dbu::jsonInt(in.value(QStringLiteral("schedule_id")));
        if (scheduleId <= 0)
            return dbu::fail(QStringLiteral("请先选择号源"));

        QString error;
        if (!dbu::db().transaction())
            return dbu::serverError(QStringLiteral("无法开启事务"));

        auto abort = [&](QHttpServerResponse resp) {
            dbu::db().rollback();
            return resp;
        };

        // 锁住这一行号源，避免多人抢最后一个号
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM schedules WHERE id = ? FOR UPDATE"));
        find.addBindValue(scheduleId);
        QJsonObject s;
        if (!dbu::fetchOne(find, s, &error))
            return abort(dbu::serverError(error));
        if (s.isEmpty())
            return abort(dbu::fail(QStringLiteral("号源不存在"), QHttpServerResponder::StatusCode::NotFound));
        if (s.value(QStringLiteral("status")).toString() != QStringLiteral("正常"))
            return abort(dbu::fail(QStringLiteral("该号源已停诊")));
        if (s.value(QStringLiteral("work_date")).toString() < QDate::currentDate().toString(Qt::ISODate))
            return abort(dbu::fail(QStringLiteral("该号源日期已过期")));
        if (s.value(QStringLiteral("booked")).toInt() >= s.value(QStringLiteral("total_quota")).toInt())
            return abort(dbu::fail(QStringLiteral("该号源已约满")));

        int patientId = 0;
        QString patientName;
        QString phone;
        if (user.value(QStringLiteral("role")).toString() == QLatin1String("patient")) {
            // 姓名/电话取数据库里的最新值，避免令牌里缓存的姓名过期
            QSqlQuery me(dbu::db());
            me.prepare(QStringLiteral("SELECT name, phone FROM users WHERE id = ?"));
            me.addBindValue(user.value(QStringLiteral("id")).toInt());
            QJsonObject meRow;
            if (!dbu::fetchOne(me, meRow, &error))
                return abort(dbu::serverError(error));
            patientId = user.value(QStringLiteral("patient_id")).toInt();
            patientName = meRow.value(QStringLiteral("name")).toString();
            phone = in.value(QStringLiteral("phone")).toString();
            if (phone.isEmpty())
                phone = meRow.value(QStringLiteral("phone")).toString();
        } else {
            patientId = dbu::jsonInt(in.value(QStringLiteral("patient_id")));
            patientName = in.value(QStringLiteral("patient_name")).toString().trimmed();
            phone = in.value(QStringLiteral("phone")).toString();
            if (patientName.isEmpty())
                return abort(dbu::fail(QStringLiteral("请填写就诊人姓名")));
        }

        if (patientId > 0) {
            QSqlQuery dup(dbu::db());
            dup.prepare(QStringLiteral("SELECT id FROM appointments WHERE patient_id = ? AND schedule_id = ? "
                                       "AND status <> '已取消' LIMIT 1"));
            dup.addBindValue(patientId);
            dup.addBindValue(scheduleId);
            QJsonObject dupRow;
            if (!dbu::fetchOne(dup, dupRow, &error))
                return abort(dbu::serverError(error));
            if (!dupRow.isEmpty())
                return abort(dbu::fail(QStringLiteral("该就诊人已经挂过这个号源，请勿重复挂号"),
                                       QHttpServerResponder::StatusCode::Conflict));
        }

        QSqlQuery take(dbu::db());
        take.prepare(QStringLiteral("UPDATE schedules SET booked = booked + 1 "
                                    "WHERE id = ? AND booked < total_quota"));
        take.addBindValue(scheduleId);
        if (!dbu::exec(take, &error))
            return abort(dbu::serverError(error));
        if (take.numRowsAffected() <= 0)
            return abort(dbu::fail(QStringLiteral("该号源已约满")));

        const int queueNo = s.value(QStringLiteral("booked")).toInt() + 1;
        const QString apptTime = QStringLiteral("%1 %2").arg(s.value(QStringLiteral("work_date")).toString(),
                                                             s.value(QStringLiteral("start_time")).toString()).trimmed();

        QSqlQuery ins(dbu::db());
        ins.prepare(QStringLiteral(
            "INSERT INTO appointments (patient_id, patient_name, dept, doctor, source_type, appt_time, status,"
            " schedule_id, room, price, queue_no, phone) VALUES (?, ?, ?, ?, ?, ?, '待就诊', ?, ?, ?, ?, ?)"));
        if (patientId > 0) ins.addBindValue(patientId);
        else               ins.addBindValue(QVariant(QMetaType(QMetaType::Int)));
        ins.addBindValue(patientName);
        ins.addBindValue(s.value(QStringLiteral("dept")).toString());
        ins.addBindValue(s.value(QStringLiteral("doctor")).toString());
        ins.addBindValue(s.value(QStringLiteral("source_type")).toString());
        ins.addBindValue(apptTime);
        ins.addBindValue(scheduleId);
        ins.addBindValue(s.value(QStringLiteral("room")).toString());
        ins.addBindValue(s.value(QStringLiteral("price")).toDouble());
        ins.addBindValue(queueNo);
        ins.addBindValue(phone);
        if (!dbu::exec(ins, &error))
            return abort(dbu::serverError(error));

        const int newId = ins.lastInsertId().toInt();
        if (!dbu::db().commit())
            return dbu::serverError(QStringLiteral("提交事务失败"));

        return dbu::created(QJsonObject{
            {QStringLiteral("id"), newId},
            {QStringLiteral("queue_no"), queueNo},
            {QStringLiteral("appt_time"), apptTime},
            {QStringLiteral("dept"), s.value(QStringLiteral("dept"))},
            {QStringLiteral("doctor"), s.value(QStringLiteral("doctor"))},
            {QStringLiteral("room"), s.value(QStringLiteral("room"))},
            {QStringLiteral("price"), s.value(QStringLiteral("price"))},
            {QStringLiteral("source_type"), s.value(QStringLiteral("source_type"))},
            {QStringLiteral("patient_name"), patientName},
            {QStringLiteral("status"), QStringLiteral("待就诊")}
        });
    });

    /* ---------- 取号：待就诊 -> 候诊中 ---------- */
    server.route(QStringLiteral("/api/appointments/<arg>/queue"), QHttpServerRequest::Method::Post,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req)) return std::move(*denied);
        const QJsonObject user = authu::currentUser(req);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM appointments WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject appt;
        if (!dbu::fetchOne(find, appt, &error))
            return dbu::serverError(error);
        if (appt.isEmpty())
            return dbu::fail(QStringLiteral("挂号记录不存在"), QHttpServerResponder::StatusCode::NotFound);
        if (!ownsAppointment(user, appt))
            return dbu::fail(QStringLiteral("无权操作该挂号记录"), QHttpServerResponder::StatusCode::Forbidden);
        if (appt.value(QStringLiteral("status")).toString() != QStringLiteral("待就诊"))
            return dbu::fail(QStringLiteral("当前状态为「%1」，不能取号")
                                 .arg(appt.value(QStringLiteral("status")).toString()));

        QSqlQuery upd(dbu::db());
        upd.prepare(QStringLiteral("UPDATE appointments SET status = '候诊中' WHERE id = ?"));
        upd.addBindValue(id);
        if (!dbu::exec(upd, &error))
            return dbu::serverError(error);

        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("status"), QStringLiteral("候诊中")}});
    });

    /* ---------- 接诊完成：待就诊/候诊中 -> 已就诊 ---------- */
    server.route(QStringLiteral("/api/appointments/<arg>/visit"), QHttpServerRequest::Method::Post,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req)) return std::move(*denied);
        const QJsonObject user = authu::currentUser(req);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM appointments WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject appt;
        if (!dbu::fetchOne(find, appt, &error))
            return dbu::serverError(error);
        if (appt.isEmpty())
            return dbu::fail(QStringLiteral("挂号记录不存在"), QHttpServerResponder::StatusCode::NotFound);
        if (!ownsAppointment(user, appt))
            return dbu::fail(QStringLiteral("无权操作该挂号记录"), QHttpServerResponder::StatusCode::Forbidden);
        if (!isActiveStatus(appt.value(QStringLiteral("status")).toString()))
            return dbu::fail(QStringLiteral("当前状态为「%1」，不能接诊")
                                 .arg(appt.value(QStringLiteral("status")).toString()));

        QSqlQuery upd(dbu::db());
        upd.prepare(QStringLiteral("UPDATE appointments SET status = '已就诊' WHERE id = ?"));
        upd.addBindValue(id);
        if (!dbu::exec(upd, &error))
            return dbu::serverError(error);

        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("status"), QStringLiteral("已就诊")}});
    });

    /* ---------- 取消挂号：释放号源 ---------- */
    server.route(QStringLiteral("/api/appointments/<arg>/cancel"), QHttpServerRequest::Method::Post,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req)) return std::move(*denied);
        const QJsonObject user = authu::currentUser(req);

        QString error;
        if (!dbu::db().transaction())
            return dbu::serverError(QStringLiteral("无法开启事务"));
        auto abort = [&](QHttpServerResponse resp) {
            dbu::db().rollback();
            return resp;
        };

        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM appointments WHERE id = ? FOR UPDATE"));
        find.addBindValue(id);
        QJsonObject appt;
        if (!dbu::fetchOne(find, appt, &error))
            return abort(dbu::serverError(error));
        if (appt.isEmpty())
            return abort(dbu::fail(QStringLiteral("挂号记录不存在"), QHttpServerResponder::StatusCode::NotFound));
        if (!ownsAppointment(user, appt))
            return abort(dbu::fail(QStringLiteral("无权操作该挂号记录"), QHttpServerResponder::StatusCode::Forbidden));
        if (!isActiveStatus(appt.value(QStringLiteral("status")).toString()))
            return abort(dbu::fail(QStringLiteral("当前状态为「%1」，不能取消")
                                       .arg(appt.value(QStringLiteral("status")).toString())));

        QSqlQuery upd(dbu::db());
        upd.prepare(QStringLiteral("UPDATE appointments SET status = '已取消' WHERE id = ?"));
        upd.addBindValue(id);
        if (!dbu::exec(upd, &error))
            return abort(dbu::serverError(error));

        const int scheduleId = appt.value(QStringLiteral("schedule_id")).toInt();
        if (scheduleId > 0) {
            QSqlQuery release(dbu::db());
            release.prepare(QStringLiteral("UPDATE schedules SET booked = GREATEST(booked - 1, 0) WHERE id = ?"));
            release.addBindValue(scheduleId);
            if (!dbu::exec(release, &error))
                return abort(dbu::serverError(error));
        }

        if (!dbu::db().commit())
            return dbu::serverError(QStringLiteral("提交事务失败"));
        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("status"), QStringLiteral("已取消")}});
    });

    /* ---------- 单条详情 ---------- */
    server.route(QStringLiteral("/api/appointments/<arg>"), QHttpServerRequest::Method::Get, [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req)) return std::move(*denied);
        const QJsonObject user = authu::currentUser(req);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM appointments WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject appt;
        if (!dbu::fetchOne(find, appt, &error))
            return dbu::serverError(error);
        if (appt.isEmpty())
            return dbu::fail(QStringLiteral("挂号记录不存在"), QHttpServerResponder::StatusCode::NotFound);
        if (!ownsAppointment(user, appt))
            return dbu::fail(QStringLiteral("无权操作该挂号记录"), QHttpServerResponder::StatusCode::Forbidden);
        return dbu::ok(appt);
    });
}
