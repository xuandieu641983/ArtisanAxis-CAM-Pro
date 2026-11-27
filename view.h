#pragma once

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>
#include <functional>
#include <array>
#include <vector>

class QMouseEvent;
class QWheelEvent;

class View final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit View(QWidget *parent = nullptr);
    ~View() override;

    bool loadSTL(const QString &file, QString &error);
    void loadSTLAsync(const QString &file, std::function<void(bool, QString)> done);
    void setModelTransform(const QMatrix4x4 &matrix);
    QMatrix4x4 modelTransform() const { return modelMatrix_; }
    bool modelBounds(QVector3D &lo, QVector3D &hi) const;
    qsizetype triangleCount() const { return static_cast<qsizetype>(positions_.size() / 9); }

    // Bounds are final stock extents, including expansion. For frusta the larger
    // end is at the low axial coordinate. X is axial unless axisY is true.
    void setStock(const QString &shape, const QVector3D &lo, const QVector3D &hi,
                  double endRatio = 0.5, bool axisY = false);
    void setStockVisible(bool yes) { stockVisible_ = yes; update(); }
    void setModelVisible(bool yes) { modelVisible_ = yes; update(); }
    void setOverlay(std::vector<float> paths,std::vector<float> surface);
    void setSimStock(std::vector<float> intact,std::vector<float> excess,std::vector<float> gouge,double pitch=0);
    void setPlayback(QVector3D tip,double a,double b,bool axisY,QVector3D pivot,double diameter,double overhang,double holder,double ball,double halfAngle,double flute);
    void clearPlayback(){if(playback_){playback_=false;update();}}
    void fit();
    void setCamera(const QString &view);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void acceptMesh(std::vector<float> points);
    void updateBounds();
    void cleanup();
    void draw(QOpenGLBuffer &buffer, int count, unsigned mode,
              const QMatrix4x4 &matrix, const QVector3D &color, float alpha, bool shaded);
    std::vector<float> positions_, stockFaces_, stockEdges_,pathPoints_,surfaceFaces_,simIntact_,simExcess_,simGouge_;
    QMatrix4x4 modelMatrix_, projectionView_;
    QVector3D stockLo_{0,-50,-50}, stockHi_{200,50,50};
    QVector3D modelLo_, modelHi_, target_;
    float span_ = 180, yaw_ = -55, elevation_ = 24;
    QPoint lastMouse_;
    quint64 loadGeneration_ = 0;
    bool stockVisible_ = true, modelVisible_ = true;
    bool modelDirty_ = true, stockDirty_ = true, overlayDirty_=true, ready_ = false;
    bool playback_=false;
    QMatrix4x4 partRotation_,toolPose_;
    std::array<double,6> cutterProfile_{};
    std::vector<float> cutterMesh_;
    bool cutterDirty_=true;
    QOpenGLShaderProgram *program_ = nullptr;
    QOpenGLBuffer modelBuffer_, stockFaceBuffer_, stockEdgeBuffer_,pathBuffer_,surfaceBuffer_,simIntactBuffer_,simExcessBuffer_,simGougeBuffer_,cylinderBuffer_,cutterBuffer_;
};
