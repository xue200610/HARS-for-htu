#pragma once

// ============================================================================
//  运行配置：优先读环境变量，其次用默认值
// ============================================================================
#include <QString>

namespace cfg {

QString dbHost();
int     dbPort();
QString dbUser();
QString dbPassword();
QString dbName();

quint16 httpPort();

/** 前端静态文件目录（public/），默认取可执行文件旁边或工程根的 public */
QString publicDir();

/** 登录令牌签名密钥；没设 SESSION_SECRET 时用固定开发密钥并给出告警 */
QByteArray sessionSecret();
bool     usingDefaultSecret();

} // namespace cfg
