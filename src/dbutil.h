#pragma once

// ============================================================================
//  数据库与响应封装
//   · 全局一条 QSqlDatabase 连接（QHttpServer 在同一个线程里处理请求，安全）
//   · 统一响应格式 { ok: true, data: ... } / { ok: false, message: ... }
// ============================================================================
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

namespace dbu {

/** 全局连接 */
QSqlDatabase &db();

/** 打开连接，失败时把错误写进 error */
bool open(QString *error);

/** QVariant -> JSON，尽量保持数字/布尔类型 */
QJsonValue toJson(const QVariant &v);

/** 执行查询并把结果集转成 JSON 数组 */
bool fetchAll(QSqlQuery &query, QJsonArray &out, QString *error);

/** 执行查询并取第一行（没有行时返回空对象） */
bool fetchOne(QSqlQuery &query, QJsonObject &out, QString *error);

/** 取第一行的某个列（没有行时返回 QJsonValue::Null） */
QJsonValue fetchScalar(QSqlQuery &query, const QString &column, QString *error);

/** 简单的执行（INSERT/UPDATE/DELETE），返回是否成功 */
bool exec(QSqlQuery &query, QString *error);

// ---------- 统一响应 ----------
QHttpServerResponse ok(const QJsonValue &data,
                       QHttpServerResponder::StatusCode status = QHttpServerResponder::StatusCode::Ok);
QHttpServerResponse created(const QJsonValue &data);
QHttpServerResponse fail(const QString &message,
                         QHttpServerResponder::StatusCode status = QHttpServerResponder::StatusCode::BadRequest);
QHttpServerResponse serverError(const QString &message);

/**
 * JSON 取值：前端表单传来的是字符串（"1"），接口脚本/前端 JS 传来的是数字（1）。
 * 两种都要能取到，所以统一走这两个函数，不要直接用 toString().toInt()。
 */
inline int jsonInt(const QJsonValue &v, int fallback = 0)
{
    if (v.isDouble()) return v.toInt();
    if (v.isBool())   return v.toBool() ? 1 : 0;
    if (v.isString()) { bool ok = false; const int n = v.toString().toInt(&ok); return ok ? n : fallback; }
    return fallback;
}

inline double jsonDouble(const QJsonValue &v, double fallback = 0.0)
{
    if (v.isDouble()) return v.toDouble();
    if (v.isString()) { bool ok = false; const double d = v.toString().toDouble(&ok); return ok ? d : fallback; }
    return fallback;
}

} // namespace dbu
