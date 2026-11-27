#pragma once
#include <QString>
#include <QVector3D>
#include <functional>
class QWidget;

struct Stock {
    QString shape = "box";
    double min[3] = {0,-50,-50};
    double max[3] = {200,50,50};
    double expansion = 0;
    double endRatio = 0.5;
    QVector3D low() const;
    QVector3D high() const;
    double radius(const QVector3D& pivot, const QVector3D& axis, bool alongY) const;
};
bool editStock(QWidget* parent, Stock& committed, bool alongY, const QVector3D& pivot, const QVector3D& axis,
               const std::function<void(const Stock&)>& preview);
