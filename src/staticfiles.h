#pragma once

// ============================================================================
//  静态文件：把 public/ 下的前端页面与资源发出去
//   · "/"            -> index.html
//   · "/login"       -> login.html（自动补 .html）
//   · "/assets/x.js" -> public/assets/x.js
// ============================================================================
#include <QHttpServerResponse>
#include <QString>
#include <optional>

namespace staticfiles {

/** 按 URL 路径查找文件；找不到返回 nullopt（由调用方决定返回 404 还是别的） */
std::optional<QHttpServerResponse> serve(const QString &urlPath);

} // namespace staticfiles
