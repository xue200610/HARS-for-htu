#include "../authutil.h"
#include "../dbutil.h"
#include "../routes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

namespace {

/** 对外返回的用户信息（和 Node 版 publicUser 字段一致，前端依赖这些字段） */
QJsonObject publicUser(const QJsonObject &row)
{
    return QJsonObject{
        {QStringLiteral("id"),         row.value(QStringLiteral("id"))},
        {QStringLiteral("username"),   row.value(QStringLiteral("username"))},
        {QStringLiteral("role"),       row.value(QStringLiteral("role"))},
        {QStringLiteral("name"),       row.value(QStringLiteral("name"))},
        {QStringLiteral("phone"),      row.value(QStringLiteral("phone"))},
        {QStringLiteral("dept"),       row.value(QStringLiteral("dept"))},
        {QStringLiteral("patient_id"), row.value(QStringLiteral("patient_id")).toInt()}
    };
}

} // namespace

void registerAuthRoutes(QHttpServer &server)
{
    /* ---------- 登录：账号 + MD5 密码，可选校验角色 ---------- */
    server.route(QStringLiteral("/api/auth/login"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        const QJsonObject in = authu::body(req);
        const QString username = in.value(QStringLiteral("username")).toString().trimmed();
        const QString password = in.value(QStringLiteral("password")).toString();
        const QString role     = in.value(QStringLiteral("role")).toString();
        if (username.isEmpty() || password.isEmpty())
            return dbu::fail(QStringLiteral("请输入账号和密码"));

        QSqlQuery query(dbu::db());
        query.prepare(QStringLiteral("SELECT * FROM users WHERE username = ?"));
        query.addBindValue(username);
        QJsonObject user;
        QString error;
        if (!dbu::fetchOne(query, user, &error))
            return dbu::serverError(error);

        if (user.isEmpty() || user.value(QStringLiteral("password")).toString() != authu::md5(password))
            return dbu::fail(QStringLiteral("账号或密码错误"), QHttpServerResponder::StatusCode::Unauthorized);

        if (!role.isEmpty() && user.value(QStringLiteral("role")).toString() != role) {
            return dbu::fail(QStringLiteral("该账号与所选角色不匹配，请按实际角色登录"),
                             QHttpServerResponder::StatusCode::Forbidden);
        }

        const QJsonObject info = publicUser(user);
        return dbu::ok(QJsonObject{{QStringLiteral("token"), authu::signToken(info)},
                                   {QStringLiteral("user"), info}});
    });

    /* ---------- 患者自助注册：同时写 patients 与 users ---------- */
    server.route(QStringLiteral("/api/auth/register"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        const QJsonObject in = authu::body(req);
        const QString username = in.value(QStringLiteral("username")).toString().trimmed();
        const QString password = in.value(QStringLiteral("password")).toString();
        const QString name     = in.value(QStringLiteral("name")).toString();
        if (username.isEmpty() || password.isEmpty())
            return dbu::fail(QStringLiteral("请输入账号和密码"));
        if (name.isEmpty())
            return dbu::fail(QStringLiteral("请输入姓名"));
        if (password.length() < 6)
            return dbu::fail(QStringLiteral("密码至少 6 位"));

        QSqlQuery dup(dbu::db());
        dup.prepare(QStringLiteral("SELECT id FROM users WHERE username = ?"));
        dup.addBindValue(username);
        QJsonObject exists;
        QString error;
        if (!dbu::fetchOne(dup, exists, &error))
            return dbu::serverError(error);
        if (!exists.isEmpty())
            return dbu::fail(QStringLiteral("该账号已被注册"), QHttpServerResponder::StatusCode::Conflict);

        if (!dbu::db().transaction())
            return dbu::serverError(QStringLiteral("无法开启事务"));

        QSqlQuery ins(dbu::db());
        ins.prepare(QStringLiteral("INSERT INTO patients (name, gender, age, id_card, phone, status) "
                                   "VALUES (?, ?, ?, ?, ?, '门诊')"));
        ins.addBindValue(name);
        ins.addBindValue(in.value(QStringLiteral("gender")).toString().isEmpty()
                             ? QStringLiteral("男") : in.value(QStringLiteral("gender")).toString());
        const int age = dbu::jsonInt(in.value(QStringLiteral("age")));
        if (age > 0) ins.addBindValue(age); else ins.addBindValue(QVariant(QMetaType(QMetaType::Int)));
        ins.addBindValue(in.value(QStringLiteral("id_card")).toString().isEmpty()
                             ? QVariant(QMetaType(QMetaType::QString)) : in.value(QStringLiteral("id_card")).toString());
        ins.addBindValue(in.value(QStringLiteral("phone")).toString().isEmpty()
                             ? QVariant(QMetaType(QMetaType::QString)) : in.value(QStringLiteral("phone")).toString());
        if (!dbu::exec(ins, &error)) {
            dbu::db().rollback();
            return dbu::serverError(error);
        }
        const int patientId = ins.lastInsertId().toInt();

        QSqlQuery insUser(dbu::db());
        insUser.prepare(QStringLiteral("INSERT INTO users (username, password, role, name, phone, id_card, dept, patient_id) "
                                       "VALUES (?, ?, 'patient', ?, ?, ?, NULL, ?)"));
        insUser.addBindValue(username);
        insUser.addBindValue(authu::md5(password));
        insUser.addBindValue(name);
        insUser.addBindValue(in.value(QStringLiteral("phone")).toString());
        insUser.addBindValue(in.value(QStringLiteral("id_card")).toString());
        insUser.addBindValue(patientId);
        if (!dbu::exec(insUser, &error)) {
            dbu::db().rollback();
            return dbu::serverError(error);
        }
        dbu::db().commit();

        QSqlQuery back(dbu::db());
        back.prepare(QStringLiteral("SELECT * FROM users WHERE patient_id = ? AND role = 'patient'"));
        back.addBindValue(patientId);
        QJsonObject created;
        if (!dbu::fetchOne(back, created, &error))
            return dbu::serverError(error);

        const QJsonObject info = publicUser(created);
        return dbu::created(QJsonObject{{QStringLiteral("token"), authu::signToken(info)},
                                        {QStringLiteral("user"), info}});
    });

    /* ---------- 当前登录用户 ---------- */
    server.route(QStringLiteral("/api/auth/me"), QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req)) return std::move(*denied);
        const QJsonObject me = authu::currentUser(req);

        QSqlQuery query(dbu::db());
        query.prepare(QStringLiteral("SELECT * FROM users WHERE id = ?"));
        query.addBindValue(me.value(QStringLiteral("id")).toInt());
        QJsonObject row;
        QString error;
        if (!dbu::fetchOne(query, row, &error))
            return dbu::serverError(error);
        if (row.isEmpty())
            return dbu::fail(QStringLiteral("用户不存在"), QHttpServerResponder::StatusCode::NotFound);
        return dbu::ok(publicUser(row));
    });

    /* ---------- 修改自己的密码 ---------- */
    server.route(QStringLiteral("/api/auth/change-password"), QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest &req) -> QHttpServerResponse {
        if (auto denied = authu::check(req)) return std::move(*denied);
        const QJsonObject in = authu::body(req);
        const QString oldPassword = in.value(QStringLiteral("oldPassword")).toString();
        const QString newPassword = in.value(QStringLiteral("newPassword")).toString();
        if (oldPassword.isEmpty() || newPassword.isEmpty())
            return dbu::fail(QStringLiteral("请填写原密码和新密码"));
        if (newPassword.length() < 6)
            return dbu::fail(QStringLiteral("新密码至少 6 位"));

        const QJsonObject me = authu::currentUser(req);
        QSqlQuery query(dbu::db());
        query.prepare(QStringLiteral("SELECT * FROM users WHERE id = ?"));
        query.addBindValue(me.value(QStringLiteral("id")).toInt());
        QJsonObject row;
        QString error;
        if (!dbu::fetchOne(query, row, &error))
            return dbu::serverError(error);
        if (row.isEmpty() || row.value(QStringLiteral("password")).toString() != authu::md5(oldPassword))
            return dbu::fail(QStringLiteral("原密码不正确"));

        QSqlQuery upd(dbu::db());
        upd.prepare(QStringLiteral("UPDATE users SET password = ? WHERE id = ?"));
        upd.addBindValue(authu::md5(newPassword));
        upd.addBindValue(me.value(QStringLiteral("id")).toInt());
        if (!dbu::exec(upd, &error))
            return dbu::serverError(error);

        return dbu::ok(QJsonObject{{QStringLiteral("message"), QStringLiteral("密码已更新")}});
    });
}
