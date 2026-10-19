#pragma once

#include <QHttpServer>

// 各模块的路由注册入口（在 main.cpp 里统一调用）
void registerAuthRoutes(QHttpServer &server);
void registerDepartmentRoutes(QHttpServer &server);
void registerDoctorRoutes(QHttpServer &server);
void registerScheduleRoutes(QHttpServer &server);
void registerAppointmentRoutes(QHttpServer &server);
void registerPatientRoutes(QHttpServer &server);
void registerStatsRoutes(QHttpServer &server);
