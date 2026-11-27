#include "stl_reader.h"
#include "cam/job.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <array>
#include <future>
#include <cstdio>

static QJsonObject inspect(const QString& path) {
    std::vector<float> vertices;
    QString error;
    if(!native_stl::parseSTL(path,vertices,error))
        return {{"ok",false},{"error",error}};
    using Bounds=std::array<std::array<double,3>,2>;
    auto bounds=[&vertices](size_t begin,size_t end){
        Bounds result{{{vertices[begin],vertices[begin+1],vertices[begin+2]},
                       {vertices[begin],vertices[begin+1],vertices[begin+2]}}};
        for(size_t i=begin;i<end;i+=3)for(int a=0;a<3;++a){
            result[0][a]=std::min(result[0][a],double(vertices[i+a]));
            result[1][a]=std::max(result[1][a],double(vertices[i+a]));
        }
        return result;
    };
    const size_t midpoint=(vertices.size()/6)*3;
    auto first=std::async(std::launch::async,bounds,0,midpoint);
    auto result=bounds(midpoint,vertices.size());
    const auto other=first.get();
    QJsonArray lo,hi;
    for(int i=0;i<3;++i){lo.append(std::min(result[0][i],other[0][i]));hi.append(std::max(result[1][i],other[1][i]));}
    return {{"ok",true},{"triangles",qint64(vertices.size()/9)},{"min",lo},{"max",hi}};
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    QTextStream input(stdin);input.setEncoding(QStringConverter::Utf8);
    const auto request=QJsonDocument::fromJson(input.readLine(1048576).toUtf8()).object();
    QJsonObject result;
    try{
        if(request.value("file").toString().isEmpty())
            result={{"ok",false},{"error",QStringLiteral("Yêu cầu kiểm tra STL không hợp lệ.")}};
        else if(request.value("operation").toString()=="inspect-stl")result=inspect(request.value("file").toString());
        else result=runCamJob(request,[](int percent,const QString& stage,qint64 turn,qint64 total){
            const auto record=QJsonDocument(QJsonObject{{"percent",percent},{"stage",stage},{"turn",turn},{"total",total}}).toJson(QJsonDocument::Compact);
            std::fwrite(record.constData(),1,size_t(record.size()),stderr);
            std::fputc('\n',stderr);
            std::fflush(stderr);
        });
    }catch(const std::exception& error){result={{"ok",false},{"error",QString::fromUtf8(error.what())}};}
    QFile output; if(!output.open(stdout,QIODevice::WriteOnly))return 2;
    output.write(QJsonDocument(result).toJson(QJsonDocument::Compact)+"\n");
    return result.value("ok").toBool()?0:1;
}
