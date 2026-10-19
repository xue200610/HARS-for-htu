#include "route-helpers.h"
#include "../authutil.h"
#include "../dbutil.h"
#include "../routes.h"

#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>

/* ==================== 科室管理 ==================== */

void registerDepartmentRoutes(QHttpServer &server)
{
    // 列表：带上实际医生数与未来号源时段数
    server.route(QStringLiteral("/api/departments"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &) -> QHttpServerResponse {
        QSqlQuery query(dbu::db());
        query.prepare(QStringLiteral(
            "SELECT d.id, d.name, d.director, d.nurse_count, d.bed_count,"
            "       d.doctor_count AS stored_doctor_count,"
            "       (SELECT COUNT(*) FROM doctors doc WHERE doc.dept = d.name) AS doctor_count,"
            "       (SELECT COUNT(*) FROM schedules s WHERE s.dept = d.name"
            "         AND s.work_date >= CURDATE() AND s.status = '正常') AS schedule_count"
            "  FROM departments d ORDER BY d.id"));
        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);
        return dbu::ok(rows);
    });

    // 新增
    server.route(QStringLiteral("/api/departments"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);
        const QJsonObject in = authu::body(req);
        const QString name = in.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            return dbu::fail(QStringLiteral("请输入科室名称"));

        QString error;
        QSqlQuery dup(dbu::db());
        dup.prepare(QStringLiteral("SELECT id FROM departments WHERE name = ?"));
        dup.addBindValue(name);
        QJsonObject exists;
        if (!dbu::fetchOne(dup, exists, &error))
            return dbu::serverError(error);
        if (!exists.isEmpty())
            return dbu::fail(QStringLiteral("该科室已存在"), QHttpServerResponder::StatusCode::Conflict);

        QSqlQuery ins(dbu::db());
        ins.prepare(QStringLiteral("INSERT INTO departments (name, director, doctor_count, nurse_count, bed_count) "
                                   "VALUES (?, ?, 0, ?, ?)"));
        ins.addBindValue(name);
        ins.addBindValue(in.value(QStringLiteral("director")).toString());
        ins.addBindValue(dbu::jsonInt(in.value(QStringLiteral("nurse_count"))));
        ins.addBindValue(dbu::jsonInt(in.value(QStringLiteral("bed_count"))));
        if (!dbu::exec(ins, &error))
            return dbu::serverError(error);

        return dbu::created(QJsonObject{{QStringLiteral("id"), ins.lastInsertId().toInt()}});
    });

    // 修改（改科室名时同步 doctors / schedules）
    server.route(QStringLiteral("/api/departments/<arg>"), QHttpServerRequest::Method::Put,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM departments WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject old;
        if (!dbu::fetchOne(find, old, &error))
            return dbu::serverError(error);
        if (old.isEmpty())
            return dbu::fail(QStringLiteral("科室不存在"), QHttpServerResponder::StatusCode::NotFound);

        const QJsonObject in = authu::body(req);
        const QString oldName = old.value(QStringLiteral("name")).toString();
        const QString newName = in.contains(QStringLiteral("name"))
                                    ? in.value(QStringLiteral("name")).toString().trimmed() : oldName;

        if (newName != oldName) {
            QSqlQuery u1(dbu::db());
            u1.prepare(QStringLiteral("UPDATE doctors SET dept = ? WHERE dept = ?"));
            u1.addBindValue(newName);
            u1.addBindValue(oldName);
            if (!dbu::exec(u1, &error)) return dbu::serverError(error);

            QSqlQuery u2(dbu::db());
            u2.prepare(QStringLiteral("UPDATE schedules SET dept = ? WHERE dept = ?"));
            u2.addBindValue(newName);
            u2.addBindValue(oldName);
            if (!dbu::exec(u2, &error)) return dbu::serverError(error);
        }

        QSqlQuery upd(dbu::db());
        upd.prepare(QStringLiteral("UPDATE departments SET name = ?, director = ?, nurse_count = ?, bed_count = ? "
                                   "WHERE id = ?"));
        upd.addBindValue(newName);
        upd.addBindValue(in.contains(QStringLiteral("director"))
                             ? in.value(QStringLiteral("director")).toString()
                             : old.value(QStringLiteral("director")).toString());
        upd.addBindValue(in.contains(QStringLiteral("nurse_count"))
                             ? dbu::jsonInt(in.value(QStringLiteral("nurse_count")))
                             : old.value(QStringLiteral("nurse_count")).toInt());
        upd.addBindValue(in.contains(QStringLiteral("bed_count"))
                             ? dbu::jsonInt(in.value(QStringLiteral("bed_count")))
                             : old.value(QStringLiteral("bed_count")).toInt());
        upd.addBindValue(id);
        if (!dbu::exec(upd, &error))
            return dbu::serverError(error);

        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}});
    });

    // 删除（下面还有医生就不让删）
    server.route(QStringLiteral("/api/departments/<arg>"), QHttpServerRequest::Method::Delete,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM departments WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject dept;
        if (!dbu::fetchOne(find, dept, &error))
            return dbu::serverError(error);
        if (dept.isEmpty())
            return dbu::fail(QStringLiteral("科室不存在"), QHttpServerResponder::StatusCode::NotFound);

        QSqlQuery used(dbu::db());
        used.prepare(QStringLiteral("SELECT COUNT(*) AS n FROM doctors WHERE dept = ?"));
        used.addBindValue(dept.value(QStringLiteral("name")).toString());
        QJsonObject usedRow;
        if (!dbu::fetchOne(used, usedRow, &error))
            return dbu::serverError(error);
        const int n = usedRow.value(QStringLiteral("n")).toInt();
        if (n > 0) {
            return dbu::fail(QStringLiteral("该科室下还有 %1 位医生，请先调整医生所属科室").arg(n));
        }

        QSqlQuery del(dbu::db());
        del.prepare(QStringLiteral("DELETE FROM departments WHERE id = ?"));
        del.addBindValue(id);
        if (!dbu::exec(del, &error))
            return dbu::serverError(error);

        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}});
    });
}
