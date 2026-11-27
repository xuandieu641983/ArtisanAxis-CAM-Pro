#include "stock.h"
#include <QtWidgets>
#include <algorithm>
#include <cmath>

QVector3D Stock::low() const { return QVector3D(min[0]-expansion,min[1]-expansion,min[2]-expansion); }
QVector3D Stock::high() const { return QVector3D(max[0]+expansion,max[1]+expansion,max[2]+expansion); }

// Exact farthest transverse radius of an axis-aligned ellipse. For a box use
// its farthest corner. The larger end bounds an aligned centred frustum.
double Stock::radius(const QVector3D& pivot,const QVector3D& axis,bool alongY) const {
    const int i = alongY ? 0 : 1;
    double c = std::abs((min[i]+max[i])/2-pivot[i]);
    double d = std::abs((min[2]+max[2])/2-pivot.z());
    double a = (max[i]-min[i])/2+expansion;
    double b = (max[2]-min[2])/2+expansion;
    const int axial=alongY?1:0;
    if(axis[i]!=0 || axis.z()!=0) {
        const double norm=std::hypot(std::hypot(double(axis.x()),double(axis.y())),double(axis.z()));
        auto distance=[&](const double* q){
            double x=q[0]-pivot.x(), y=q[1]-pivot.y(), z=q[2]-pivot.z();
            return std::hypot(std::hypot(y*axis.z()-z*axis.y(),z*axis.x()-x*axis.z()),x*axis.y()-y*axis.x())/norm;
        };
        double result=0;
        for(int end=0;end<2;++end){
            double q[3];q[axial]=end?max[axial]+expansion:min[axial]-expansion;
            double cy=(min[i]+max[i])/2, cz=(min[2]+max[2])/2;
            if(shape=="box"){
                for(int sy:{-1,1})for(int sz:{-1,1}){q[i]=cy+sy*a;q[2]=cz+sz*b;result=std::max(result,distance(q));}
            } else {
                double k=end && shape=="frustum"?endRatio:1;
                // A chord approximates the ellipse to <= max(a,b)*(1-cos(pi/N)).
                // Distance to an axis is convex along each chord.
                double major=k*std::max(a,b);
                int n=std::max(16,int(std::ceil(3.141592653589793*std::sqrt(major/(2*0.00005)))));
                double peak=0;
                for(int j=0;j<n;++j){double t=2*3.141592653589793*j/n;
                    q[i]=cy+k*a*std::cos(t);q[2]=cz+k*b*std::sin(t);peak=std::max(peak,distance(q));}
                result=std::max(result,peak+major*(1-std::cos(3.141592653589793/n)));
            }
        }
        return result;
    }
    if(shape=="box") return std::hypot(c+a,d+b);
    if(a<b){ std::swap(a,b); std::swap(c,d); }
    if(a==b) return std::hypot(c,d)+a;
    // On the first quadrant the stationary equation, divided by cos(t),
    // is bd - ac*tan(t) - (a*a-b*b)*sin(t) = 0, monotone decreasing.
    double l=0, r=std::acos(-1.0)/2;
    for(int k=0;k<80;++k){
        const double t=(l+r)/2;
        if(b*d-a*c*std::tan(t)-(a*a-b*b)*std::sin(t)>0) l=t; else r=t;
    }
    return std::hypot(c+a*std::cos((l+r)/2),d+b*std::sin((l+r)/2));
}

bool editStock(QWidget* parent, Stock& committed,bool alongY,const QVector3D& pivot,const QVector3D& axis,
               const std::function<void(const Stock&)>& preview) {
    Stock s=committed;
    QDialog dialog(parent); dialog.setWindowTitle("Phôi · kích thước thực (mm)");
    QGridLayout g(&dialog);
    QComboBox shapes; shapes.addItem("Hộp","box"); shapes.addItem("Trụ","cylinder"); shapes.addItem("Chóp cụt","frustum");
    shapes.setCurrentIndex(shapes.findData(s.shape)); g.addWidget(&shapes,0,0,1,4);
    QStringList labels{"Trục","Min","Max","Kích thước gốc"};
    for(int j=0;j<4;++j)g.addWidget(new QLabel(labels[j]),1,j);
    QDoubleSpinBox* fields[3][3];
    auto box=[&](double lo,double hi,double v){auto* b=new QDoubleSpinBox; b->setDecimals(4); b->setRange(lo,hi); b->setValue(v);  return b;};
    auto* expansion=box(0,100000,s.expansion); auto* ratio=box(0,1,s.endRatio);
    QLabel report;
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Ok)->setText("Lưu và đóng");
    buttons.button(QDialogButtonBox::Cancel)->setText("Hủy");
    for(int i=0;i<3;++i){
        g.addWidget(new QLabel(QString("XYZ").mid(i,1)),2+i,0);
        fields[i][0]=box(-1000000,1000000,s.min[i]); fields[i][1]=box(-1000000,1000000,s.max[i]);
        fields[i][2]=box(.0001,2000000,s.max[i]-s.min[i]);
        for(int j=0;j<3;++j)g.addWidget(fields[i][j],2+i,j+1);
    }
    g.addWidget(new QLabel("Nở mỗi phía (mm)"),5,0,1,2); g.addWidget(expansion,5,2);
    g.addWidget(new QLabel("Tỷ lệ đầu nhỏ / đầu lớn"),6,0,1,2); g.addWidget(ratio,6,2);
    g.addWidget(&report,7,0,1,4); g.addWidget(&buttons,8,0,1,4);
    auto update=[&]{
        bool valid=true;
        for(int i=0;i<3;i++){s.min[i]=fields[i][0]->value();s.max[i]=fields[i][1]->value();valid &= s.min[i]<s.max[i];}
        buttons.button(QDialogButtonBox::Ok)->setEnabled(valid);
        if(!valid){report.setText("Max phải lớn hơn Min trên cả ba trục.");return;}
        for(int i=0;i<3;i++){QSignalBlocker guard(fields[i][2]);fields[i][2]->setValue(s.max[i]-s.min[i]);}
        s.shape=shapes.currentData().toString(); s.expansion=expansion->value();s.endRatio=ratio->value();
        ratio->setEnabled(s.shape=="frustum");
        const auto R=s.radius(pivot,axis,alongY);
        report.setText(QString("R góc %1 mm   ·   Nhấc tối thiểu %2 mm").arg(R,0,'f',4).arg(R+50,0,'f',4));preview(s);
    };
    for(int i=0;i<3;i++)for(int j=0;j<3;j++)
        QObject::connect(fields[i][j],&QDoubleSpinBox::valueChanged,&dialog,[&,i,j](double value){
            if(j==2){QSignalBlocker guard(fields[i][1]);fields[i][1]->setValue(fields[i][0]->value()+value);}
            update();
        });
    QObject::connect(expansion,&QDoubleSpinBox::valueChanged,&dialog,update);QObject::connect(ratio,&QDoubleSpinBox::valueChanged,&dialog,update);
    QObject::connect(&shapes,&QComboBox::currentIndexChanged,&dialog,update);
    QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    update();
    if(dialog.exec()==QDialog::Accepted){committed=s;return true;}preview(committed);return false;
}
