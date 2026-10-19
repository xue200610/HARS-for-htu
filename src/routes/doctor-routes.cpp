#include "route-helpers.h"
#include "../authutil.h"
#include "../dbutil.h"
#include "../routes.h"

#include <QHash>
#include <QJsonObject>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>

/* ==================== 医生管理 ==================== */

void registerDoctorRoutes(QHttpServer &server)
{
    // 列表：可按科室/关键字筛选，并附带未来 7 天余号
    server.route(QStringLiteral("/api/doctors"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        const QString dept = req.query().queryItemValue(QStringLiteral("dept"));
        const QString keyword = req.query().queryItemValue(QStringLiteral("keyword"));

        QString sql = QStringLiteral("SELECT id, name, dept, title, phone FROM doctors");
        QStringList where;
        if (!dept.isEmpty())    where << QStringLiteral("dept = ?");
        if (!keyword.isEmpty()) where << QStringLiteral("(name LIKE ? OR title LIKE ?)");
        if (!where.isEmpty())   sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
        sql += QStringLiteral(" ORDER BY dept, id");

        QSqlQuery query(dbu::db());
        query.prepare(sql);
        if (!dept.isEmpty())
            query.addBindValue(dept);
        if (!keyword.isEmpty()) {
            query.addBindValue(QStringLiteral("%1%").arg(keyword));
            query.addBindValue(QStringLiteral("%1%").arg(keyword));
        }
        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);

        // 每位医生未来 7 天的余号与排班数
        QSqlQuery stats(dbu::db());
        stats.prepare(QStringLiteral(
            "SELECT doctor_id, SUM(total_quota - booked) AS remaining, COUNT(*) AS schedule_count"
            "  FROM schedules WHERE status = '正常'"
            "   AND work_date BETWEEN CURDATE() AND DATE_ADD(CURDATE(), INTERVAL 6 DAY)"
            " GROUP BY doctor_id"));
        QJsonArray statRows;
        if (!dbu::fetchAll(stats, statRows, &error))
            return dbu::serverError(error);

        QHash<int, QJsonObject> byDoctor;
        for (const QJsonValue &v : statRows) {
            const QJsonObject o = v.toObject();
            byDoctor.insert(o.value(QStringLiteral("doctor_id")).toInt(), o);
        }

        QJsonArray out;
        for (const QJsonValue &v : rows) {
            QJsonObject o = v.toObject();
            const QJsonObject s = byDoctor.value(o.value(QStringLiteral("id")).toInt());
            o.insert(QStringLiteral("remaining"), s.value(QStringLiteral("remaining")).toInt());
            o.insert(QStringLiteral("schedule_count"), s.value(QStringLiteral("schedule_count")).toInt());
            out.append(o);
        }
        return dbu::ok(out);
    });

    // 新增医生
    server.route(QStringLiteral("/api/doctors"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);
        const QJsonObject in = authu::body(req);
        const QString name = in.value(QStringLiteral("name")).toString().trimmed();
        const QString dept = in.value(QStringLiteral("dept")).toString();
        if (name.isEmpty() || dept.isEmpty())
            return dbu::fail(QStringLiteral("请填写医生姓名和所属科室"));

        QString error;
        QSqlQuery ins(dbu::db());
        ins.prepare(QStringLiteral("INSERT INTO doctors (name, dept, title, phone) VALUES (?, ?, ?, ?)"));
        ins.addBindValue(name);
        ins.addBindValue(dept);
        ins.addBindValue(in.value(QStringLiteral("title")).toString().isEmpty()
                             ? QStringLiteral("主治医师") : in.value(QStringLiteral("title")).toString());
        ins.addBindValue(in.value(QStringLiteral("phone")).toString());
        if (!dbu::exec(ins, &error))
            return dbu::serverError(error);

        routehelp::syncDoctorCount(dept);
        return dbu::created(QJsonObject{{QStringLiteral("id"), ins.lastInsertId().toInt()}});
    });

    // 修改医生（姓名/科室变化时，同步他的号源与挂号记录）
    server.route(QStringLiteral("/api/doctors/<arg>"), QHttpServerRequest::Method::Put,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM doctors WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject old;
        if (!dbu::fetchOne(find, old, &error))
            return dbu::serverError(error);
        if (old.isEmpty())
            return dbu::fail(QStringLiteral("医生不存在"), QHttpServerResponder::StatusCode::NotFound);

        const QJsonObject in = authu::body(req);
        const QString newName = in.contains(QStringLiteral("name"))
                                    ? in.value(QStringLiteral("name")).toString().trimmed()
                                    : old.value(QStringLiteral("name")).toString();
        const QString newDept = in.contains(QStringLiteral("dept"))
                                    ? in.value(QStringLiteral("dept")).toString()
                                    : old.value(QStringLiteral("dept")).toString();
        const QString oldDept = old.value(QStringLiteral("dept")).toString();

        QSqlQuery upd(dbu::db());
        upd.prepare(QStringLiteral("UPDATE doctors SET name = ?, dept = ?, title = ?, phone = ? WHERE id = ?"));
        upd.addBindValue(newName);
        upd.addBindValue(newDept);
        upd.addBindValue(in.contains(QStringLiteral("title"))
                             ? in.value(QStringLiteral("title")).toString()
                             : old.value(QStringLiteral("title")).toString());
        upd.addBindValue(in.contains(QStringLiteral("phone"))
                             ? in.value(QStringLiteral("phone")).toString()
                             : old.value(QStringLiteral("phone")).toString());
        upd.addBindValue(id);
        if (!dbu::exec(upd, &error))
            return dbu::serverError(error);

        QSqlQuery s1(dbu::db());
        s1.prepare(QStringLiteral("UPDATE schedules SET doctor = ?, dept = ? WHERE doctor_id = ?"));
        s1.addBindValue(newName);
        s1.addBindValue(newDept);
        s1.addBindValue(id);
        if (!dbu::exec(s1, &error))
            return dbu::serverError(error);

        QSqlQuery s2(dbu::db());
        s2.prepare(QStringLiteral(
            "UPDATE appointments a JOIN schedules s ON a.schedule_id = s.id "
            "   SET a.doctor = s.doctor, a.dept = s.dept WHERE s.doctor_id = ?"));
        s2.addBindValue(id);
        if (!dbu::exec(s2, &error))
            return dbu::serverError(error);

        // —— 医生姓名在本系统里同时是「业务标识」：号源、挂号、病历、住院床位、患者档案，
        //    以及医生登录账号（users.name）里都冗余存了姓名；医生端筛「我的挂号」用的就是
        //    登录账号里的姓名。改名必须一起同步，否则这位医生一登录就看不到自己的任何记录。
        //    这些表只存姓名、没有 doctor_id，所以只在「旧姓名唯一」时按姓名同步，
        //    避免把同名的另一位医生的记录一起改掉。
        const QString oldName = old.value(QStringLiteral("name")).toString();
        if (!newName.isEmpty() && newName != oldName) {
            QString err2;
            QSqlQuery cnt(dbu::db());
            cnt.prepare(QStringLiteral("SELECT COUNT(*) AS n FROM doctors WHERE name = ?"));
            cnt.addBindValue(oldName);
            const QJsonValue sameV = dbu::fetchScalar(cnt, QStringLiteral("n"), &err2);
            if (!err2.isEmpty())
                return dbu::serverError(err2);

            if (sameV.toInt() > 1) {
                qWarning().noquote()
                    << QStringLiteral("[改名] 有 %1 位医生都叫「%2」，无法区分，已跳过按姓名的关联同步")
                           .arg(sameV.toInt()).arg(oldName);
            } else {
                // 1) 桌面端早期建的挂号记录（没有 schedule_id），按旧姓名兜底
                QSqlQuery q1(dbu::db());
                q1.prepare(QStringLiteral("UPDATE appointments SET doctor = ?, dept = ? "
                                          "WHERE doctor = ? AND schedule_id IS NULL"));
                q1.addBindValue(newName);
                q1.addBindValue(newDept);
                q1.addBindValue(oldName);
                if (!dbu::exec(q1, &error)) return dbu::serverError(error);

                // 2) 患者档案里的「责任医生」
                QSqlQuery q2(dbu::db());
                q2.prepare(QStringLiteral("UPDATE patients SET doctor = ? WHERE doctor = ?"));
                q2.addBindValue(newName);
                q2.addBindValue(oldName);
                if (!dbu::exec(q2, &error)) return dbu::serverError(error);

                // 3) 病历里的接诊医生（只同步姓名；dept 是就诊当时的科室，不跟着医生走）
                QSqlQuery q3(dbu::db());
                q3.prepare(QStringLiteral("UPDATE medical_records SET doctor = ? WHERE doctor = ?"));
                q3.addBindValue(newName);
                q3.addBindValue(oldName);
                if (!dbu::exec(q3, &error)) return dbu::serverError(error);

                // 4) 住院床位的主治医生（inpatient.dept 是床位所在科室，也不跟着医生走）
                QSqlQuery q4(dbu::db());
                q4.prepare(QStringLiteral("UPDATE inpatient SET doctor = ? WHERE doctor = ?"));
                q4.addBindValue(newName);
                q4.addBindValue(oldName);
                if (!dbu::exec(q4, &error)) return dbu::serverError(error);

                // 5) 医生登录账号：不同步的话，医生端按姓名筛选会查不到自己的挂号
                QSqlQuery q5(dbu::db());
                q5.prepare(QStringLiteral(
                    "UPDATE users SET name = ?, dept = ? WHERE role = 'doctor' AND name = ?"));
                q5.addBindValue(newName);
                q5.addBindValue(newDept);
                q5.addBindValue(oldName);
                if (!dbu::exec(q5, &error)) return dbu::serverError(error);
            }
        }

        routehelp::syncDoctorCount(oldDept);
        if (newDept != oldDept)
            routehelp::syncDoctorCount(newDept);

        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}});
    });

    // 删除医生（还有未完成挂号就不让删）
    server.route(QStringLiteral("/api/doctors/<arg>"), QHttpServerRequest::Method::Delete,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM doctors WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject doc;
        if (!dbu::fetchOne(find, doc, &error))
            return dbu::serverError(error);
        if (doc.isEmpty())
            return dbu::fail(QStringLiteral("医生不存在"), QHttpServerResponder::StatusCode::NotFound);

        QSqlQuery pending(dbu::db());
        pending.prepare(QStringLiteral(
            "SELECT COUNT(*) AS n FROM appointments a JOIN schedules s ON a.schedule_id = s.id"
            " WHERE s.doctor_id = ? AND a.status IN ('待就诊','候诊中')"));
        pending.addBindValue(id);
        QJsonObject pendingRow;
        if (!dbu::fetchOne(pending, pendingRow, &error))
            return dbu::serverError(error);
        const int n = pendingRow.value(QStringLiteral("n")).toInt();
        if (n > 0)
            return dbu::fail(QStringLiteral("该医生还有 %1 条未完成挂号，不能删除").arg(n));

        QSqlQuery d1(dbu::db());
        d1.prepare(QStringLiteral("DELETE FROM schedules WHERE doctor_id = ?"));
        d1.addBindValue(id);
        if (!dbu::exec(d1, &error))
            return dbu::serverError(error);

        QSqlQuery d2(dbu::db());
        d2.prepare(QStringLiteral("DELETE FROM doctors WHERE id = ?"));
        d2.addBindValue(id);
        if (!dbu::exec(d2, &error))
            return dbu::serverError(error);

        routehelp::syncDoctorCount(doc.value(QStringLiteral("dept")).toString());
        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}});
    });
}
