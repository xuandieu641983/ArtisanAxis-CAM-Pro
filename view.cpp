#include "view.h"
#include "stl_reader.h"

#include <QFile>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>
#include <QThread>
#include <QWheelEvent>
#include <QtEndian>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <memory>
#include <new>

namespace {
using Triangles = std::vector<float>;

bool valid(float v) { return std::isfinite(v); }
void append(Triangles &v, const QVector3D &p) {
    v.insert(v.end(), {p.x(), p.y(), p.z()});
}
void triangle(Triangles &v, const QVector3D &a, const QVector3D &b, const QVector3D &c) {
    append(v,a); append(v,b); append(v,c);
}
void edge(Triangles &v, const QVector3D &a, const QVector3D &b) { append(v,a); append(v,b); }
QVector3D eyeDirection(float yaw, float elevation) {
    const float a=qDegreesToRadians(yaw), b=qDegreesToRadians(elevation);
    return {std::cos(a)*std::cos(b),std::sin(a)*std::cos(b),std::sin(b)};
}
}

View::View(QWidget *parent) : QOpenGLWidget(parent) {
    QSurfaceFormat requested;
    requested.setRenderableType(QSurfaceFormat::OpenGL);
    requested.setVersion(2,1);
    requested.setDepthBufferSize(24);
    setFormat(requested);
    setMinimumSize(240,180);
    setStock(QStringLiteral("box"),stockLo_,stockHi_);
    fit();
}
View::~View() {
    if (context()) {
        disconnect(context(),nullptr,this,nullptr);
        cleanup();
    }
}
void View::cleanup() {
    if (!context()) return;
    makeCurrent();
    modelBuffer_.destroy(); stockFaceBuffer_.destroy(); stockEdgeBuffer_.destroy();pathBuffer_.destroy();surfaceBuffer_.destroy();simIntactBuffer_.destroy();simExcessBuffer_.destroy();simGougeBuffer_.destroy();cylinderBuffer_.destroy();cutterBuffer_.destroy();cutterDirty_=true;
    delete program_; program_=nullptr;
    ready_=false;
    modelDirty_=stockDirty_=overlayDirty_=true;
    doneCurrent();
}
bool View::loadSTL(const QString &file, QString &error) {
    ++loadGeneration_;
    Triangles mesh;
    if (!readNativeSTL(file,mesh,error)) return false;
    acceptMesh(std::move(mesh));
    return true;
}
void View::loadSTLAsync(const QString &file, std::function<void(bool,QString)> done) {
    struct Result { Triangles mesh; QString error; bool ok=false; };
    auto result=std::make_shared<Result>();
    const auto generation=++loadGeneration_;
    auto thread=QThread::create([result,file] { result->ok=readNativeSTL(file,result->mesh,result->error); });
    connect(thread,&QThread::finished,this,[this,generation,result,done=std::move(done)] {
        if (generation!=loadGeneration_) return;
        if (result->ok) acceptMesh(std::move(result->mesh));
        if (done) done(result->ok,result->error);
    });
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    thread->start();
}
void View::acceptMesh(Triangles points) {
    positions_=std::move(points); modelMatrix_.setToIdentity(); modelDirty_=true;
    updateBounds(); fit();
}
void View::setModelTransform(const QMatrix4x4 &matrix) {
    modelMatrix_=matrix; updateBounds(); update();
}
void View::updateBounds() {
    if (positions_.empty()) return;
    modelLo_=modelHi_=modelMatrix_.map(QVector3D(positions_[0],positions_[1],positions_[2]));
    for (size_t i=3;i<positions_.size();i+=3) {
        const auto p=modelMatrix_.map(QVector3D(positions_[i],positions_[i+1],positions_[i+2]));
        for (int d=0;d<3;++d) {
            modelLo_[d]=std::min(modelLo_[d],p[d]); modelHi_[d]=std::max(modelHi_[d],p[d]);
        }
    }
}
bool View::modelBounds(QVector3D &lo,QVector3D &hi) const {
    if (positions_.empty()) return false;
    lo=modelLo_; hi=modelHi_; return true;
}

void View::setStock(const QString &shape,const QVector3D &lo,const QVector3D &hi,double endRatio,bool axisY) {
    for (int j=0;j<3;++j) if (!valid(lo[j]) || !valid(hi[j]) || hi[j]<=lo[j]) return;
    if (!std::isfinite(endRatio) || endRatio<0 || endRatio>1) return;
    stockLo_=lo; stockHi_=hi;
    stockFaces_.clear(); stockEdges_.clear();
    if (shape=="box") {
        std::array<QVector3D,8> v;
        for (int i=0;i<8;++i) v[i]={i&1?hi.x():lo.x(),i&2?hi.y():lo.y(),i&4?hi.z():lo.z()};
        constexpr int faces[][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
        for (const auto &f:faces) { triangle(stockFaces_,v[f[0]],v[f[1]],v[f[2]]); triangle(stockFaces_,v[f[0]],v[f[2]],v[f[3]]); }
        for (int i=0;i<8;++i) for (int bit: {1,2,4}) if (!(i&bit)) edge(stockEdges_,v[i],v[i|bit]);
    } else {
        const float ratio=shape=="frustum"?float(endRatio):1.0f;
        const int axis=axisY?1:0, radial=axisY?0:1;
        const auto center=(hi+lo)*0.5f, half=(hi-lo)*0.5f;
        auto point=[&](int i,bool upper,float scale) {
            const double angle=i*(2.0*3.14159265358979323846/128.0);
            QVector3D p=center;
            p[axis]=upper?hi[axis]:lo[axis];
            p[radial]+=float(std::cos(angle))*half[radial]*scale;
            p[2]+=float(std::sin(angle))*half[2]*scale;
            return p;
        };
        for (int i=0;i<128;++i) {
            const auto a=point(i,false,1),b=point(i+1,false,1),c=point(i+1,true,ratio),d=point(i,true,ratio);
            triangle(stockFaces_,a,b,c); triangle(stockFaces_,a,c,d);
            triangle(stockFaces_,point(0,false,0),b,a); triangle(stockFaces_,point(0,true,0),d,c);
            edge(stockEdges_,a,b); edge(stockEdges_,c,d);
            if(i%16==0) edge(stockEdges_,a,d);
        }
    }
    stockDirty_=true; update();
}

void View::fit() {
    QVector3D lo=stockLo_,hi=stockHi_;
    if (!positions_.empty()) for (int i=0;i<3;++i) { lo[i]=std::min(lo[i],modelLo_[i]); hi[i]=std::max(hi[i],modelHi_[i]); }
    target_=(hi+lo)*0.5f; span_=std::max(1.0f,(hi-lo).length()*0.58f); update();
}
void View::setCamera(const QString &view) {
    const QString key=view.toLower();
    if (key=="xy") { yaw_=-90; elevation_=90; }
    else if(key=="yz") { yaw_=0; elevation_=0; }
    else if(key=="zx" || key=="xz") { yaw_=-90; elevation_=0; }
    else { yaw_=-55; elevation_=24; }
    update();
}
void View::initializeGL() {
    initializeOpenGLFunctions();
    connect(context(),&QOpenGLContext::aboutToBeDestroyed,this,&View::cleanup,Qt::DirectConnection);
    program_=new QOpenGLShaderProgram;
    const char *vs="#version 120\nattribute vec3 pos; uniform mat4 mvp; uniform mat4 world; varying vec3 wp; void main(){wp=(world*vec4(pos,1)).xyz; gl_Position=mvp*vec4(pos,1);}";
    const char *fs="#version 120\nvarying vec3 wp; uniform vec3 tint; uniform float alpha; uniform int lit; void main(){float k=1.; if(lit!=0){vec3 n=normalize(cross(dFdx(wp),dFdy(wp))); k=.38+.62*abs(dot(n,normalize(vec3(.3,-.55,.85))));}gl_FragColor=vec4(k*tint,alpha);}";
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex,vs) ||
        !program_->addShaderFromSourceCode(QOpenGLShader::Fragment,fs)) return;
    program_->bindAttributeLocation("pos",0);
    ready_=program_->link() && modelBuffer_.create() && stockFaceBuffer_.create() && stockEdgeBuffer_.create() && pathBuffer_.create() && surfaceBuffer_.create() && simIntactBuffer_.create() && simExcessBuffer_.create() && simGougeBuffer_.create() && cylinderBuffer_.create() && cutterBuffer_.create();
    if(ready_){
        Triangles cylinder;
        for(int i=0;i<24;++i){
            auto rim=[](int sector){
                const auto a=qDegreesToRadians(15.f*sector);
                return QVector3D(std::cos(a),std::sin(a),0);
            };
            const auto a=rim(i),b=rim(i+1);
            const QVector3D rise(0,0,1);
            triangle(cylinder,a,b,b+rise);triangle(cylinder,a,b+rise,a+rise);
            triangle(cylinder,{},b,a);triangle(cylinder,rise,a+rise,b+rise);
        }
        cylinderBuffer_.bind();
        cylinderBuffer_.allocate(cylinder.data(),int(cylinder.size()*sizeof(float)));
        cylinderBuffer_.release();
    }
}
void View::draw(QOpenGLBuffer &buf,int count,unsigned mode,const QMatrix4x4 &world,const QVector3D &color,float alpha,bool shaded) {
    if (!count || !buf.bind()) return;
    program_->setUniformValue("mvp",projectionView_*world); program_->setUniformValue("world",world);
    program_->setUniformValue("tint",color); program_->setUniformValue("alpha",alpha); program_->setUniformValue("lit",int(shaded));
    program_->enableAttributeArray(0); program_->setAttributeBuffer(0,GL_FLOAT,0,3);
    glDrawArrays(mode,0,count);
    program_->disableAttributeArray(0); buf.release();
}
void View::setOverlay(std::vector<float> paths,std::vector<float> surface){
    pathPoints_=std::move(paths);surfaceFaces_=std::move(surface);overlayDirty_=true;update();
}
void View::setSimStock(std::vector<float> intact,std::vector<float> excess,std::vector<float> gouge,double pitch){
    auto voxels=[pitch](const std::vector<float>& centers){
        std::vector<float> mesh;
        if(!(pitch>0))return mesh;
        const float r=float(pitch*.49);
        const int faces[][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
        const size_t step=std::max(size_t(1),(centers.size()/3+19999)/20000);
        for(size_t i=0;i+2<centers.size();i+=3*step){
            const QVector3D p(centers[i],centers[i+1],centers[i+2]);
            QVector3D v[8];
            for(int j=0;j<8;++j)v[j]=p+QVector3D(j&1?r:-r,j&2?r:-r,j&4?r:-r);
            for(const auto& f:faces){triangle(mesh,v[f[0]],v[f[1]],v[f[2]]);triangle(mesh,v[f[0]],v[f[2]],v[f[3]]);}
        }
        return mesh;
    };
    simIntact_=voxels(intact);simExcess_=voxels(excess);simGouge_=voxels(gouge);
    overlayDirty_=true;update();
}
void View::setPlayback(QVector3D tip,double a,double b,bool axisY,QVector3D pivot,double diameter,double overhang,double holder,double ball,double halfAngle,double flute){
    playback_=true;
    partRotation_.setToIdentity();
    partRotation_.translate(pivot);
    partRotation_.rotate(float(std::remainder(a,360.)),axisY?QVector3D(0,1,0):QVector3D(1,0,0));
    partRotation_.translate(-pivot);
    toolPose_.setToIdentity();
    toolPose_.translate(tip);
    toolPose_.rotate(float(b),axisY?QVector3D(-1,0,0):QVector3D(0,1,0));
    const std::array profile{diameter,overhang,holder,ball,halfAngle,flute};
    if(cutterProfile_!=profile){
        cutterProfile_=profile;
        cutterDirty_=true;
        cutterMesh_.clear();
        std::vector<std::pair<double,double>> rings;
        const double body=diameter/2;
        if(ball>0){
            const double lean=qDegreesToRadians(halfAngle);
            for(int i=0;i<=8;++i){
                const double polar=(std::numbers::pi/2-lean)*i/8;
                rings.emplace_back(ball*(1-std::cos(polar)),ball*std::sin(polar));
            }
            const auto [height,radius]=rings.back();
            if(halfAngle>0){
                const auto top=std::clamp(height+(body-radius)/std::tan(lean),height,std::max(height,flute));
                rings.emplace_back(top,std::min(body,radius+(top-height)*std::tan(lean)));
            }
        }else{
            rings.emplace_back(0,0);
            rings.emplace_back(0,body);
        }
        rings.emplace_back(std::max(rings.back().first,flute),rings.back().second);
        rings.emplace_back(rings.back().first,body);
        rings.emplace_back(overhang,body);
        rings.emplace_back(overhang,0);
        for(size_t r=1;r<rings.size();++r){
            auto vertex=[&](size_t ring,int side){
                const auto [z,radius]=rings[ring];
                const auto angle=qDegreesToRadians(15.*side);
                return QVector3D(float(radius*std::cos(angle)),float(radius*std::sin(angle)),float(z));
            };
            for(int i=0;i<24;++i){
                const auto a=vertex(r-1,i),b=vertex(r-1,i+1),c=vertex(r,i+1),d=vertex(r,i);
                triangle(cutterMesh_,a,b,c);triangle(cutterMesh_,a,c,d);
            }
        }
    }
    update();
}
void View::paintGL() {
    glClearColor(.075f,.095f,.12f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    if (!ready_) { QPainter p(this); p.setPen(Qt::white); p.drawText(rect(),Qt::AlignCenter,QStringLiteral("Khung nhìn cần OpenGL 2.1.")); return; }
    auto upload=[](QOpenGLBuffer &b,const Triangles &v){b.bind(); b.allocate(v.data(),int(v.size()*sizeof(float))); b.release();};
    if (modelDirty_) { upload(modelBuffer_,positions_); modelDirty_=false; }
    if (stockDirty_) { upload(stockFaceBuffer_,stockFaces_); upload(stockEdgeBuffer_,stockEdges_); stockDirty_=false; }
    if(overlayDirty_){upload(pathBuffer_,pathPoints_);upload(surfaceBuffer_,surfaceFaces_);
        upload(simIntactBuffer_,simIntact_);upload(simExcessBuffer_,simExcess_);upload(simGougeBuffer_,simGouge_);overlayDirty_=false;}
    if(cutterDirty_){upload(cutterBuffer_,cutterMesh_);cutterDirty_=false;}
    const auto direction=eyeDirection(yaw_,elevation_);
    const float sceneDepth=std::max({span_,(stockLo_-target_).length(),(stockHi_-target_).length(),
        (modelLo_-target_).length(),(modelHi_-target_).length(),1.0f})*2.0f;
    const float aspect=float(width())/float(std::max(1,height()));
    QMatrix4x4 view,proj;
    const auto up=std::abs(elevation_)>=89.99f?QVector3D(0,1,0):QVector3D(0,0,1);
    view.lookAt(target_+direction*(sceneDepth*2),target_,up);
    proj.ortho(-span_*std::max(1.0f,aspect),span_*std::max(1.0f,aspect),
               -span_/std::min(1.0f,aspect),span_/std::min(1.0f,aspect),sceneDepth*.1f,sceneDepth*4);
    projectionView_=proj*view;
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE);
    program_->bind();
    const QMatrix4x4 part=playback_?partRotation_:QMatrix4x4{};
    const bool sim=!simIntact_.empty()||!simExcess_.empty()||!simGouge_.empty();
    if (modelVisible_) draw(modelBuffer_,int(positions_.size()/3),GL_TRIANGLES,part*modelMatrix_,{.85f,.72f,.54f},1,true);
    if (stockVisible_&&!sim) {
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        draw(stockFaceBuffer_,int(stockFaces_.size()/3),GL_TRIANGLES,part,{.30f,.67f,.80f},.12f,false);
        draw(stockEdgeBuffer_,int(stockEdges_.size()/3),GL_LINES,part,{.48f,.83f,.90f},.85f,false);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    }
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
    if(surfaceVisible_)draw(surfaceBuffer_,int(surfaceFaces_.size()/3),GL_TRIANGLES,part,{.21f,.84f,.64f},.30f,true);
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    if(sim&&stockVisible_){
        draw(simIntactBuffer_,int(simIntact_.size()/3),GL_TRIANGLES,part,{.82f,.65f,.45f},1,true);
        draw(simExcessBuffer_,int(simExcess_.size()/3),GL_TRIANGLES,part,{1.f,.65f,.18f},1,true);
        draw(simGougeBuffer_,int(simGouge_.size()/3),GL_TRIANGLES,part,{1.f,.18f,.24f},1,true);
    }else if(!sim)draw(pathBuffer_,int(pathPoints_.size()/3),GL_LINES,part,{1.f,.77f,.16f},1,false);
    if(playback_){
        auto drawTool=[&](float start,float diameter,float length,QVector3D color){
            auto pose=toolPose_;
            pose.translate(0,0,start);
            pose.scale(diameter*.5f,diameter*.5f,length);
            draw(cylinderBuffer_,24*12,GL_TRIANGLES,pose,color,1,true);
        };
        draw(cutterBuffer_,int(cutterMesh_.size()/3),GL_TRIANGLES,toolPose_,{.76f,.86f,.95f},1,true);
        drawTool(float(cutterProfile_[1]),float(cutterProfile_[2]),40,{.35f,.55f,.74f});
    }
    program_->release();
}
void View::mousePressEvent(QMouseEvent *e) { lastMouse_=e->position().toPoint(); e->accept(); }
void View::mouseMoveEvent(QMouseEvent *e) {
    const auto p=e->position().toPoint(), delta=p-lastMouse_; lastMouse_=p;
    if (e->buttons()&Qt::LeftButton) {
        yaw_-=delta.x()*.5f; elevation_=std::clamp(elevation_+delta.y()*.5f,-89.8f,89.8f);
    } else if(e->buttons()&(Qt::MiddleButton|Qt::RightButton)) {
        auto dir=eyeDirection(yaw_,elevation_); const auto ref=std::abs(elevation_)>=89.99f?QVector3D(0,1,0):QVector3D(0,0,1);
        const auto right=QVector3D::crossProduct(ref,dir).normalized(),up=QVector3D::crossProduct(dir,right);
        const float k=2*span_/float(std::max(1,std::min(width(),height())));
        target_+=(-right*delta.x()+up*delta.y())*k;
    }
    update(); e->accept();
}
void View::wheelEvent(QWheelEvent *e) {
    span_=std::clamp(span_*float(std::exp(-e->angleDelta().y()/800.0)),.05f,1.0e8f); update(); e->accept();
}
