#include "dbutil.h"
#include "appconfig.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlRecord>
#include <QTime>
#include <QDebug>

namespace dbu {

QSqlDatabase &db()
{
    static QSqlDatabase database = [] {
        QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("main"));
        d.setHostName(cfg::dbHost());
        d.setPort(cfg::dbPort());
        d.setUserName(cfg::dbUser());
        d.setPassword(cfg::dbPassword());
        d.setDatabaseName(cfg::dbName());
        return d;
    }();
    return database;
}

bool open(QString *error)
{
    if (db().isOpen())
        return true;
    if (!db().open()) {
        if (error)
            *error = db().lastError().text();
        return false;
    }
    return true;
}

QJsonValue toJson(const QVariant &v)
{
    if (v.isNull())
        return QJsonValue(QJsonValue::Null);

    switch (v.typeId()) {
    case QMetaType::Bool:       return v.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:  return v.toLongLong();
    case QMetaType::Double:
    case QMetaType::Float:      return v.toDouble();
    case QMetaType::QDate:      return v.toDate().toString(Qt::ISODate);
    case QMetaType::QDateTime:  return v.toDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    case QMetaType::QTime:      return v.toTime().toString(QStringLiteral("HH:mm:ss"));
    default:                    return v.toString();
    }
}

bool fetchAll(QSqlQuery &query, QJsonArray &out, QString *error)
{
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    const QSqlRecord rec = query.record();
    while (query.next()) {
        QJsonObject row;
        for (int i = 0; i < rec.count(); ++i)
            row.insert(rec.fieldName(i), toJson(query.value(i)));
        out.append(row);
    }
    return true;
}

bool fetchOne(QSqlQuery &query, QJsonObject &out, QString *error)
{
    QJsonArray rows;
    if (!fetchAll(query, rows, error))
        return false;
    if (!rows.isEmpty())
        out = rows.first().toObject();
    return true;
}

QJsonValue fetchScalar(QSqlQuery &query, const QString &column, QString *error)
{
    QJsonObject row;
    if (!fetchOne(query, row, error))
        return QJsonValue(QJsonValue::Null);
    return row.value(column);
}

bool exec(QSqlQuery &query, QString *error)
{
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    return true;
}

QHttpServerResponse ok(const QJsonValue &data, QHttpServerResponder::StatusCode status)
{
    return QHttpServerResponse(QJsonObject{{QStringLiteral("ok"), true},
                                           {QStringLiteral("data"), data}}, status);
}

QHttpServerResponse created(const QJsonValue &data)
{
    return ok(data, QHttpServerResponder::StatusCode::Created);
}

QHttpServerResponse fail(const QString &message, QHttpServerResponder::StatusCode status)
{
    return QHttpServerResponse(QJsonObject{{QStringLiteral("ok"), false},
                                           {QStringLiteral("message"), message}}, status);
}

QHttpServerResponse serverError(const QString &message)
{
    // 唯一约束冲突：MySQL 报 "Duplicate entry 'xxx' for key '表.索引名'"
    // 转成 409 + 看得懂的提示，不要把 SQL 原文抛给前端
    if (message.contains(QLatin1String("Duplicate entry"), Qt::CaseInsensitive)) {
        static const QHash<QString, QString> hints = {
            {QStringLiteral("uk_patient_id_card"),    QStringLiteral("身份证号已被其他患者使用")},
            {QStringLiteral("username"),              QStringLiteral("该登录账号已被占用")},
            {QStringLiteral("uk_dept_name"),          QStringLiteral("该科室名称已存在")},
            {QStringLiteral("uk_doctor_date_period"), QStringLiteral("该医生在这个时段已有排班")},
            {QStringLiteral("code"),                  QStringLiteral("该药品编码已存在")},
            {QStringLiteral("bed_no"),                QStringLiteral("该床位号已存在")}
        };
        QString key;
        const QRegularExpressionMatch m =
            QRegularExpression(QStringLiteral("for key '([^']+)'")).match(message);
        if (m.hasMatch()) {
            key = m.captured(1);
            const int dot = key.lastIndexOf(QLatin1Char('.'));
            if (dot >= 0)
                key = key.mid(dot + 1);
        }
        qWarning().noquote() << "[唯一约束冲突]" << message;
        return fail(hints.value(key, QStringLiteral("该记录已存在（唯一字段重复）")),
                    QHttpServerResponder::StatusCode::Conflict);
    }

    qWarning().noquote() << "[SQL 错误]" << message;
    return fail(QStringLiteral("服务器内部错误：%1").arg(message),
                QHttpServerResponder::StatusCode::InternalServerError);
}

} // namespace dbu
