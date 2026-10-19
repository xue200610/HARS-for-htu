#pragma once

// ============================================================================
//  鉴权：密码摘要 + 令牌签发校验 + 角色守卫
// ============================================================================
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>

namespace authu {

/** 密码摘要：32 位小写 MD5 十六进制（与 Qt 桌面端、Node 版保持一致） */
QString md5(const QString &text);

/** 用用户信息签发令牌（payload 里带 exp，单位毫秒） */
QString signToken(const QJsonObject &user);

/** 校验令牌；失败返回空对象 */
QJsonObject verifyToken(const QString &token);

/** 从 Authorization: Bearer xxx 或 ?token=xxx 取令牌 */
QString bearerToken(const QHttpServerRequest &req);

/** 当前登录用户；未登录返回空对象 */
QJsonObject currentUser(const QHttpServerRequest &req);

/**
 * 鉴权守卫。
 *   通过  -> 返回 std::nullopt，继续走业务逻辑
 *   不通过 -> 返回要直接回给客户端的 401/403 响应
 *
 * 用法：
 *   if (auto denied = authu::check(req, {QStringLiteral("admin")})) return *denied;
 */
std::optional<QHttpServerResponse> check(const QHttpServerRequest &req,
                                         const QStringList &roles = {});

/** 解析 JSON 请求体（非法或空时返回空对象） */
QJsonObject body(const QHttpServerRequest &req);

QString roleName(const QString &role);

} // namespace authu
