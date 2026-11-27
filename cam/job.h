#pragma once
#include <QJsonObject>
#include <functional>
using JobProgress=std::function<void(int percent,const QString& stage,qint64 turn,qint64 totalTurns)>;
QJsonObject runCamJob(const QJsonObject&,const JobProgress& progress={});
