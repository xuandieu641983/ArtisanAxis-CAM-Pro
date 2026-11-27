#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector3D>

class QWidget;

class MachineProfiles final : public QObject {
public:
    explicit MachineProfiles(QWidget* parent = nullptr);
    bool edit(QWidget* parent);
    QString name() const;
    int axisCount() const;
    QVector3D stockAxisDirection() const;
    QVector3D rotaryOrigin() const;
    QVector3D rotaryDirection() const;
    QString postForAxes(int axes) const;
    QJsonObject current() const;
    QString loadWarning() const;
    bool selectProfile(const QString& id, QString* error = nullptr);
    static QJsonArray postCatalog();
    static QString validate(const QJsonObject& profile);

private:
    QJsonArray records_;
    QString activeId_;
    QString warning_;
    bool store(const QJsonArray& records, const QString& active, QString* error) const;
};
