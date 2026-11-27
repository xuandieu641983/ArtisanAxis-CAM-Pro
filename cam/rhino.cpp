#include "rhino.h"
#include <QJsonArray>
#include <cmath>
#ifdef ARTISAN_RHINO
#if defined(_WIN64) && defined(WIN32)
#undef WIN32
#endif
#include "opennurbs.h"
#endif

QJsonObject readRhinoSurface(const QString& path){
#ifdef ARTISAN_RHINO
    ON::Begin();
    ONX_Model model;
    if(!model.Read(path.toStdWString().c_str()))
        return {{"ok",false},{"error",QStringLiteral("Không đọc được tệp 3DM.")}};
    const double scale=ON::UnitScale(model.m_settings.m_ModelUnitsAndTolerances.m_unit_system,ON_UnitSystem(ON::LengthUnitSystem::Millimeters));
    if(!std::isfinite(scale)||scale<=0)return {{"ok",false},{"error",QStringLiteral("Không xác định được đơn vị 3DM.")}};
    ONX_ModelComponentIterator it(model,ON_ModelComponent::Type::ModelGeometry);
    QJsonArray mesh, guide;
    auto addSurface=[&](const ON_Surface& s){
        constexpr int n=48,m=48;
        const auto u=s.Domain(0),v=s.Domain(1);
        auto point=[&](int i,int j){
            const ON_3dPoint p=s.PointAt(u.ParameterAt(double(i)/n),v.ParameterAt(double(j)/m));
            return QJsonArray{p.x*scale,p.y*scale,p.z*scale};
        };
        if(guide.isEmpty()){
            for(int j=0;j<=m;++j)for(int i=0;i<=n;++i){
                const auto normal=s.NormalAt(u.ParameterAt(double(i)/n),v.ParameterAt(double(j)/m));
                guide.append(QJsonArray{normal.x, normal.y, normal.z});
            }
        }
        for(int j=0;j<m;++j)for(int i=0;i<n;++i){
            auto a=point(i,j),b=point(i+1,j),c=point(i+1,j+1),d=point(i,j+1);
            mesh.append(a);mesh.append(b);mesh.append(c);
            mesh.append(a);mesh.append(c);mesh.append(d);
        }
    };
    int objects=0,skipped=0;
    for(auto c=it.FirstComponent();c;c=it.NextComponent()){
        auto entity=ON_ModelGeometryComponent::Cast(c);
        if(!entity||entity->IsInstanceDefinitionGeometry())continue;
        auto g=entity->Geometry(nullptr);
        if(auto s=ON_Surface::Cast(g)){addSurface(*s);++objects;}
        else if(auto brep=ON_Brep::Cast(g)){
            for(int i=0;i<brep->m_F.Count();++i){
                auto s=brep->m_F[i].SurfaceOf();
                if(s&&brep->FaceIsSurface(i)){addSurface(*s);++objects;}else ++skipped;
            }
        }else ++skipped;
        if(objects>=20)break;
    }
    if(!objects)return {{"ok",false},{"error",QStringLiteral("Không tìm thấy mặt độc lập hoặc BRep không cắt biên.")}};
    return {{"ok",true},{"surface",mesh},{"guide",guide},{"skipped",skipped},{"surfaceCount",objects},{"kind","rhino"}};
#else
    Q_UNUSED(path);
    return {{"ok",false},{"error",QStringLiteral("Bản build này chưa bật OpenNURBS. Biên dịch với ARTISAN_RHINO=ON.")}};
#endif
}
