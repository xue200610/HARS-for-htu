#pragma once

// ============================================================================
//  路由层公用的小逻辑（多个模块都要用的）
// ============================================================================
#include <QJsonObject>
#include <QString>

namespace routehelp {

/** 让 departments.doctor_count 和 doctors 表保持一致 */
void syncDoctorCount(const QString &deptName);

/** 按现有医生批量生成未来 days 天号源（幂等：靠唯一键 + INSERT IGNORE 去重） */
QJsonObject generateSchedules(int days, QString *error);

/** 科室 -> 诊室 */
QString roomOf(const QString &dept);

} // namespace routehelp
