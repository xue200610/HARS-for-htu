#include "route-helpers.h"
#include "../dbutil.h"

#include <QDate>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>

namespace routehelp {

void syncDoctorCount(const QString &deptName)
{
    if (deptName.isEmpty())
        return;
    QSqlQuery query(dbu::db());
    query.prepare(QStringLiteral(
        "UPDATE departments SET doctor_count = (SELECT COUNT(*) FROM doctors WHERE dept = ?) WHERE name = ?"));
    query.addBindValue(deptName);
    query.addBindValue(deptName);
    QString error;
    if (!dbu::exec(query, &error))
        qWarning().noquote() << "同步科室医生数失败：" << error;
}

QString roomOf(const QString &dept)
{
    if (dept == QStringLiteral("心内科"))   return QStringLiteral("门诊楼2楼201室");
    if (dept == QStringLiteral("呼吸内科")) return QStringLiteral("门诊楼2楼203室");
    if (dept == QStringLiteral("消化内科")) return QStringLiteral("门诊楼2楼205室");
    if (dept == QStringLiteral("神经内科")) return QStringLiteral("门诊楼3楼301室");
    if (dept == QStringLiteral("骨科"))     return QStringLiteral("门诊楼3楼305室");
    return QStringLiteral("门诊楼1楼大厅分诊台");
}

QJsonObject generateSchedules(int days, QString *error)
{
    QSqlQuery doctors(dbu::db());
    doctors.prepare(QStringLiteral("SELECT id, name, dept, title FROM doctors ORDER BY id"));
    QJsonArray doctorRows;
    if (!dbu::fetchAll(doctors, doctorRows, error))
        return {};

    struct Period { const char *name; const char *start; const char *end; };
    const Period periods[] = {
        {"上午", "08:30", "12:00"},
        {"下午", "14:00", "17:30"}
    };

    int created = 0;
    for (int i = 0; i < days; ++i) {
        const QDate date = QDate::currentDate().addDays(i);
        const int weekday = date.dayOfWeek();          // 1=周一 ... 7=周日
        for (const QJsonValue &dv : doctorRows) {
            const QJsonObject doc = dv.toObject();
            const QString title = doc.value(QStringLiteral("title")).toString();
            const bool expert = title.contains(QStringLiteral("主任"));
            for (const Period &p : periods) {
                if (weekday == 7 && QLatin1String(p.name) == QStringLiteral("下午"))
                    continue;                           // 周日只排上午
                QSqlQuery ins(dbu::db());
                ins.prepare(QStringLiteral(
                    "INSERT IGNORE INTO schedules "
                    "(doctor_id, doctor, dept, work_date, period, start_time, end_time, room,"
                    " source_type, price, total_quota, booked, status) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, '正常')"));
                ins.addBindValue(doc.value(QStringLiteral("id")).toInt());
                ins.addBindValue(doc.value(QStringLiteral("name")).toString());
                ins.addBindValue(doc.value(QStringLiteral("dept")).toString());
                ins.addBindValue(date.toString(Qt::ISODate));
                ins.addBindValue(QString::fromUtf8(p.name));
                ins.addBindValue(QString::fromLatin1(p.start));
                ins.addBindValue(QString::fromLatin1(p.end));
                ins.addBindValue(roomOf(doc.value(QStringLiteral("dept")).toString()));
                ins.addBindValue(expert ? QStringLiteral("专家号") : QStringLiteral("普通号"));
                ins.addBindValue(expert ? 30.00 : 10.00);
                ins.addBindValue(expert ? 15 : 20);
                if (!dbu::exec(ins, error))
                    return {};
                created += ins.numRowsAffected() > 0 ? 1 : 0;
            }
        }
    }

    QSqlQuery total(dbu::db());
    total.prepare(QStringLiteral("SELECT COUNT(*) AS n FROM schedules WHERE work_date >= CURDATE()"));
    QJsonObject totalRow;
    if (!dbu::fetchOne(total, totalRow, error))
        return {};

    return QJsonObject{
        {QStringLiteral("created"), created},
        {QStringLiteral("total"), totalRow.value(QStringLiteral("n"))},
        {QStringLiteral("days"), days}
    };
}

} // namespace routehelp
