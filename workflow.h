#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QSet>
#include <functional>
class QWidget;

class Workflow {
public:
    Workflow();
    void setMachine(int axisCount, std::function<QString(int)> postForAxes);
    int axes() const { return axes_; }
    QString currentPost(int axes) const;
    bool setAxes(int axes, bool manual);
    void setStock(double length,double width,double height,double cornerRadius,double retract);
    void preset(int faces);
    bool editTools(QWidget*);
    bool editAI(QWidget*);
    bool editSurface(QWidget*);
    bool editPath(QWidget*,int index);
    void invalidateAll();
    QStringList pathNames() const;
    QStringList toolNames() const;
    QJsonObject snapshot() const;
    bool restore(const QJsonObject&);
private:
    struct Tool {QString name; double diameter,radius,angle,overhang,outLen; double flute=15,holderDiameter=20;};
    struct Path {QString name; int axis,tool; double angle,step,allowance,minZ; bool active=true; quint64 revision=1; bool manualStep=false,manualAllowance=false;};
    QVector<Tool> tools_;
    QVector<Path> paths_;
    int faces_=4,axes_=4,machineAxes_=5,rough_=0,finish_=3;
    int columns_=12,stacks_=16;
    double stockX_=0,stockY_=0,stockZ_=0,corner_=0,retract_=50;
    double semiAllowance_=1.2,finishStep_=0.22;
    QSet<QString> manual_;
    QString prompt_;
    std::function<QString(int)> post_;
    void syncLength(int,double);
    void updateReach();
    void syncPathAxes();
};
