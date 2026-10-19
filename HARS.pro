# ----------------------------------------------------------
# 医院挂号管理系统 —— C++ 服务端（Qt QHttpServer + MySQL）
#
#   接口与 Node.js 版后端完全一致，前端页面（门户/登录/患者端/
#   医生端/管理端）
# ----------------------------------------------------------

QT       += core sql network httpserver

CONFIG   += console c++17
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = HARS

HEADERS  += \
    src/appconfig.h \
    src/dbutil.h \
    src/authutil.h \
    src/staticfiles.h \
    src/routes.h \
    src/routes/route-helpers.h

SOURCES  += \
    src/main.cpp \
    src/appconfig.cpp \
    src/dbutil.cpp \
    src/authutil.cpp \
    src/staticfiles.cpp \
    src/routes/route-helpers.cpp \
    src/routes/auth-routes.cpp \
    src/routes/department-routes.cpp \
    src/routes/doctor-routes.cpp \
    src/routes/schedule-routes.cpp \
    src/routes/appointment-routes.cpp \
    src/routes/patient-routes.cpp \
    src/routes/stats-routes.cpp

# 输出到 build/ 下，方便清理
DESTDIR   = $$PWD/build

# 关掉 qmake 默认的 debug_and_release，只出 release，路径更简单
CONFIG   -= debug_and_release
CONFIG   += release

# ----------------------------------------------------------
# 保险：必须用 MinGW 套件
#   Qt 官方安装包里，只有 MinGW 套件自带可用的 QMYSQL 驱动插件；
#   MSVC 套件的 sqldrivers 目录里没有 qsqlmysql.dll，
#   用 MSVC 编译能过，但一运行就会报 “QMYSQL driver not loaded”。
#   这里直接卡在构建阶段，并把解决办法打出来。
# ----------------------------------------------------------
win32-msvc*|msvc {
    error("Stop: use the MinGW kit. The MSVC kit ships without the QMYSQL driver plugin, so MySQL will fail with 'driver not loaded' at runtime. Switch the kit to 'Desktop Qt 6.12.0 MinGW 64-bit' in the lower-left corner of Qt Creator, then rebuild.")
}
