#include "../authutil.h"
#include "../dbutil.h"
#include "../routes.h"

#include <QJsonObject>
#include <QSqlError>
#include <QDebug>
#include <QSqlQuery>

/* ==================== 患者档案 ==================== */

void registerPatientRoutes(QHttpServer &server)
{
    // 列表（管理员/医生）
    server.route(QStringLiteral("/api/patients"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin"), QStringLiteral("doctor")})) return std::move(*denied);
        const QJsonObject user = authu::currentUser(req);

        const QUrlQuery q(req.query());
        const QString keyword = q.queryItemValue(QStringLiteral("keyword"));
        const QString dept    = q.queryItemValue(QStringLiteral("dept"));
        const QString status  = q.queryItemValue(QStringLiteral("status"));

        QStringList where;
        QVariantList binds;
        if (!keyword.isEmpty()) {
            where << QStringLiteral("(p.name LIKE ? OR IFNULL(p.phone, \"\") LIKE ? OR IFNULL(p.id_card, \"\") LIKE ?)");
            binds << QStringLiteral("%1%").arg(keyword) << QStringLiteral("%1%").arg(keyword)
                  << QStringLiteral("%1%").arg(keyword);
        }
        if (!dept.isEmpty())   { where << QStringLiteral("p.dept = ?");   binds << dept; }
        if (!status.isEmpty()) { where << QStringLiteral("p.status = ?"); binds << status; }
        if (user.value(QStringLiteral("role")).toString() == QLatin1String("doctor")) {
            where << QStringLiteral("(p.doctor = ? OR p.doctor IS NULL)");
            binds << user.value(QStringLiteral("name")).toString();
        }

        QString sql = QStringLiteral(
            "SELECT p.id, p.name, p.gender, p.age, p.id_card, p.phone, p.dept, p.doctor,"
            "       p.diagnosis, p.status,"
            "       (SELECT COUNT(*) FROM appointments a WHERE a.patient_id = p.id) AS appointment_count,"
            "       (SELECT MAX(a.appt_time) FROM appointments a WHERE a.patient_id = p.id) AS last_appt_time,"
            "       u.username"
            "  FROM patients p LEFT JOIN users u ON u.patient_id = p.id AND u.role = 'patient'");
        if (!where.isEmpty())
            sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
        sql += QStringLiteral(" ORDER BY p.id DESC LIMIT 200");

        QSqlQuery query(dbu::db());
        query.prepare(sql);
        for (const QVariant &b : binds)
            query.addBindValue(b);

        QJsonArray rows;
        QString error;
        if (!dbu::fetchAll(query, rows, &error))
            return dbu::serverError(error);
        return dbu::ok(rows);
    });

    // 新增患者，可选同时开通登录账号（管理员）
    server.route(QStringLiteral("/api/patients"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);
        const QJsonObject in = authu::body(req);
        const QString name = in.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            return dbu::fail(QStringLiteral("请填写患者姓名"));

        QString error;
        if (!dbu::db().transaction())
            return dbu::serverError(QStringLiteral("无法开启事务"));
        auto abort = [&](QHttpServerResponse resp) {
            dbu::db().rollback();
            return resp;
        };

        QSqlQuery ins(dbu::db());
        ins.prepare(QStringLiteral(
            "INSERT INTO patients (name, gender, age, id_card, phone, dept, doctor, diagnosis, status)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        ins.addBindValue(name);
        ins.addBindValue(in.value(QStringLiteral("gender")).toString().isEmpty()
                             ? QStringLiteral("男") : in.value(QStringLiteral("gender")).toString());
        const int age = dbu::jsonInt(in.value(QStringLiteral("age")));
        if (age > 0) ins.addBindValue(age); else ins.addBindValue(QVariant(QMetaType(QMetaType::Int)));
        ins.addBindValue(in.value(QStringLiteral("id_card")).toString());
        ins.addBindValue(in.value(QStringLiteral("phone")).toString());
        ins.addBindValue(in.value(QStringLiteral("dept")).toString());
        ins.addBindValue(in.value(QStringLiteral("doctor")).toString());
        ins.addBindValue(in.value(QStringLiteral("diagnosis")).toString());
        ins.addBindValue(in.value(QStringLiteral("status")).toString().isEmpty()
                             ? QStringLiteral("门诊") : in.value(QStringLiteral("status")).toString());
        if (!dbu::exec(ins, &error))
            return abort(dbu::serverError(error));
        const int patientId = ins.lastInsertId().toInt();

        const QString username = in.value(QStringLiteral("username")).toString();
        const QString password = in.value(QStringLiteral("password")).toString();
        if (!username.isEmpty() && !password.isEmpty()) {
            QSqlQuery dup(dbu::db());
            dup.prepare(QStringLiteral("SELECT id FROM users WHERE username = ?"));
            dup.addBindValue(username);
            QJsonObject exists;
            if (!dbu::fetchOne(dup, exists, &error))
                return abort(dbu::serverError(error));
            if (!exists.isEmpty())
                return abort(dbu::fail(QStringLiteral("登录账号已存在"),
                                       QHttpServerResponder::StatusCode::Conflict));

            QSqlQuery insUser(dbu::db());
            insUser.prepare(QStringLiteral(
                "INSERT INTO users (username, password, role, name, phone, id_card, dept, patient_id)"
                " VALUES (?, ?, 'patient', ?, ?, ?, ?, ?)"));
            insUser.addBindValue(username);
            insUser.addBindValue(authu::md5(password));
            insUser.addBindValue(name);
            insUser.addBindValue(in.value(QStringLiteral("phone")).toString());
            insUser.addBindValue(in.value(QStringLiteral("id_card")).toString());
            insUser.addBindValue(in.value(QStringLiteral("dept")).toString());
            insUser.addBindValue(patientId);
            if (!dbu::exec(insUser, &error))
                return abort(dbu::serverError(error));
        }

        if (!dbu::db().commit())
            return dbu::serverError(QStringLiteral("提交事务失败"));
        return dbu::created(QJsonObject{{QStringLiteral("id"), patientId}});
    });

    // 修改患者（管理员/医生，医生主要用来写诊断）
    server.route(QStringLiteral("/api/patients/<arg>"), QHttpServerRequest::Method::Put,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin"), QStringLiteral("doctor")})) return std::move(*denied);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM patients WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject p;
        if (!dbu::fetchOne(find, p, &error))
            return dbu::serverError(error);
        if (p.isEmpty())
            return dbu::fail(QStringLiteral("患者不存在"), QHttpServerResponder::StatusCode::NotFound);

        const QJsonObject in = authu::body(req);
        auto pickStr = [&](const char *key, const QString &fallback) {
            const QString k = QString::fromUtf8(key);
            return in.contains(k) && !in.value(k).isNull() ? in.value(k).toString() : fallback;
        };
        auto pickInt = [&](const char *key, int fallback) {
            const QString k = QString::fromUtf8(key);
            const int v = dbu::jsonInt(in.value(k));
            return v > 0 ? v : fallback;
        };

        QSqlQuery upd(dbu::db());
        upd.prepare(QStringLiteral(
            "UPDATE patients SET name = ?, gender = ?, age = ?, id_card = ?, phone = ?,"
            " dept = ?, doctor = ?, diagnosis = ?, status = ? WHERE id = ?"));
        const QString oldName = p.value(QStringLiteral("name")).toString();
        QString newName = pickStr("name", oldName).trimmed();
        if (newName.isEmpty())
            newName = oldName;
        upd.addBindValue(newName);
        upd.addBindValue(pickStr("gender", p.value(QStringLiteral("gender")).toString()));
        const int age = pickInt("age", p.value(QStringLiteral("age")).toInt());
        if (age > 0) upd.addBindValue(age); else upd.addBindValue(QVariant(QMetaType(QMetaType::Int)));
        upd.addBindValue(pickStr("id_card", p.value(QStringLiteral("id_card")).toString()));
        upd.addBindValue(pickStr("phone", p.value(QStringLiteral("phone")).toString()));
        upd.addBindValue(pickStr("dept", p.value(QStringLiteral("dept")).toString()));
        upd.addBindValue(pickStr("doctor", p.value(QStringLiteral("doctor")).toString()));
        upd.addBindValue(pickStr("diagnosis", p.value(QStringLiteral("diagnosis")).toString()));
        upd.addBindValue(pickStr("status", p.value(QStringLiteral("status")).toString()));
        upd.addBindValue(id);
        if (!dbu::exec(upd, &error))
            return dbu::serverError(error);

        // —— 患者姓名同样被多处冗余引用：登录账号、挂号、费用、病历、住院床位。
        //    患者端筛「我的挂号」用的就是登录账号里的姓名，所以改名要一起同步。
        //    这些表大多带 patient_id，可以直接精确定位；只有桌面端早期数据
        //    （patient_id 为空或 0）才需要按旧姓名兜底，且要求旧姓名在患者表里唯一。
        if (newName != oldName) {
            QSqlQuery n1(dbu::db());
            n1.prepare(QStringLiteral(
                "UPDATE users SET name = ? WHERE role = 'patient' AND patient_id = ?"));
            n1.addBindValue(newName);
            n1.addBindValue(id);
            if (!dbu::exec(n1, &error)) return dbu::serverError(error);

            const char *byPid[] = {
                "UPDATE appointments    SET patient_name = ? WHERE patient_id = ?",
                "UPDATE fees            SET patient_name = ? WHERE patient_id = ?",
                "UPDATE medical_records SET patient_name = ? WHERE patient_id = ?",
                "UPDATE inpatient       SET patient_name = ? WHERE patient_id = ?"
            };
            for (const char *sql : byPid) {
                QSqlQuery q(dbu::db());
                q.prepare(QString::fromUtf8(sql));
                q.addBindValue(newName);
                q.addBindValue(id);
                if (!dbu::exec(q, &error)) return dbu::serverError(error);
            }

            QString err2;
            QSqlQuery cnt(dbu::db());
            cnt.prepare(QStringLiteral("SELECT COUNT(*) AS n FROM patients WHERE name = ?"));
            cnt.addBindValue(oldName);
            const QJsonValue sameV = dbu::fetchScalar(cnt, QStringLiteral("n"), &err2);
            if (!err2.isEmpty())
                return dbu::serverError(err2);

            if (sameV.toInt() > 1) {
                qWarning().noquote()
                    << QStringLiteral("[改名] 有 %1 位患者都叫「%2」，无法区分，已跳过按姓名的兜底同步")
                           .arg(sameV.toInt()).arg(oldName);
            } else {
                const char *legacy[] = {
                    "UPDATE appointments    SET patient_name = ? WHERE patient_name = ? AND (patient_id IS NULL OR patient_id = 0)",
                    "UPDATE fees            SET patient_name = ? WHERE patient_name = ? AND (patient_id IS NULL OR patient_id = 0)",
                    "UPDATE medical_records SET patient_name = ? WHERE patient_name = ? AND (patient_id IS NULL OR patient_id = 0)",
                    "UPDATE inpatient       SET patient_name = ? WHERE patient_name = ? AND (patient_id IS NULL OR patient_id = 0)"
                };
                for (const char *sql : legacy) {
                    QSqlQuery q(dbu::db());
                    q.prepare(QString::fromUtf8(sql));
                    q.addBindValue(newName);
                    q.addBindValue(oldName);
                    if (!dbu::exec(q, &error)) return dbu::serverError(error);
                }
            }
        }

        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}});
    });

    // 删除患者（有挂号记录不让删，会连登录账号一起删）
    server.route(QStringLiteral("/api/patients/<arg>"), QHttpServerRequest::Method::Delete,
                 [](int id, const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req, {QStringLiteral("admin")})) return std::move(*denied);

        QString error;
        QSqlQuery find(dbu::db());
        find.prepare(QStringLiteral("SELECT * FROM patients WHERE id = ?"));
        find.addBindValue(id);
        QJsonObject p;
        if (!dbu::fetchOne(find, p, &error))
            return dbu::serverError(error);
        if (p.isEmpty())
            return dbu::fail(QStringLiteral("患者不存在"), QHttpServerResponder::StatusCode::NotFound);

        QSqlQuery used(dbu::db());
        used.prepare(QStringLiteral("SELECT COUNT(*) AS n FROM appointments WHERE patient_id = ?"));
        used.addBindValue(id);
        QJsonObject usedRow;
        if (!dbu::fetchOne(used, usedRow, &error))
            return dbu::serverError(error);
        const int n = usedRow.value(QStringLiteral("n")).toInt();
        if (n > 0)
            return dbu::fail(QStringLiteral("该患者有 %1 条挂号记录，不能删除").arg(n));

        if (!dbu::db().transaction())
            return dbu::serverError(QStringLiteral("无法开启事务"));
        auto abort = [&](QHttpServerResponse resp) {
            dbu::db().rollback();
            return resp;
        };

        QSqlQuery d1(dbu::db());
        d1.prepare(QStringLiteral("DELETE FROM users WHERE patient_id = ? AND role = 'patient'"));
        d1.addBindValue(id);
        if (!dbu::exec(d1, &error))
            return abort(dbu::serverError(error));

        QSqlQuery d2(dbu::db());
        d2.prepare(QStringLiteral("DELETE FROM patients WHERE id = ?"));
        d2.addBindValue(id);
        if (!dbu::exec(d2, &error))
            return abort(dbu::serverError(error));

        if (!dbu::db().commit())
            return dbu::serverError(QStringLiteral("提交事务失败"));
        return dbu::ok(QJsonObject{{QStringLiteral("id"), id}});
    });
}
