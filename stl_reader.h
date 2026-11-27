#pragma once
#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace native_stl {
constexpr size_t maxTriangles = 12000000;
inline float readFloat(const char *p) {
    const quint32 bits = qFromLittleEndian<quint32>(p);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline bool parseSTL(const QString &path, std::vector<float> &output, QString &error) {
    error.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray header = file.peek(84);
    const quint32 n = header.size() == 84 ? qFromLittleEndian<quint32>(header.constData()+80) : 0;
    // Binary STL headers may start with "solid". The record count is authoritative.
    if (header.size() == 84 && 84LL + 50LL*n == file.size()) {
        if (!n || n > maxTriangles) { error = QStringLiteral("Số tam giác STL nằm ngoài phạm vi hỗ trợ."); return false; }
        output.reserve(size_t(n)*9);
        file.seek(84);
        quint32 read = 0;
        while (read < n) {
            const quint32 chunk = std::min<quint32>(16384, n-read);
            const auto bytes = file.read(qint64(chunk)*50);
            if (bytes.size() != qint64(chunk)*50) { error = QStringLiteral("Bản ghi STL bị thiếu."); return false; }
            for (quint32 t=0; t<chunk; ++t) {
                const char *point = bytes.constData()+t*50+12;
                for (int j=0;j<9;++j) {
                    const auto value = readFloat(point+j*4);
                    if (!std::isfinite(value)) { error = QStringLiteral("STL chứa tọa độ không hữu hạn."); return false; }
                    output.push_back(value);
                }
            }
            read += chunk;
        }
        return true;
    }

    enum class Stage { Header, Facet, Loop, Vertex, EndLoop, EndFacet };
    Stage stage = Stage::Header;
    int vertices = 0, line = 0;
    while (!file.atEnd()) {
        QByteArray row = file.readLine(1024*1024);
        ++line;
        if (row.isEmpty() && file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
        if (line == 1 && row.startsWith("\xEF\xBB\xBF")) row.remove(0,3);
        row = row.simplified();
        if (row.isEmpty()) continue;
        const auto words = row.split(' ');
        const auto key = words[0].toLower();
        bool ok = true;
        if (stage == Stage::Header && key == "solid") stage = Stage::Facet;
        else if (stage == Stage::Facet && key == "endsolid") stage = Stage::Header;
        else if (stage == Stage::Facet && key == "facet" && words.size()==5 && words[1].toLower()=="normal") stage = Stage::Loop;
        else if (stage == Stage::Loop && row.toLower()=="outer loop") { stage = Stage::Vertex; vertices = 0; }
        else if (stage == Stage::Vertex && key=="vertex" && words.size()==4) {
            for (int i=1;i<4;++i) {
                bool parsed;
                const auto value = words[i].toFloat(&parsed);
                if (!parsed || !std::isfinite(value)) { ok=false; break; }
                output.push_back(value);
            }
            if (++vertices == 3) stage = Stage::EndLoop;
            if (output.size()>maxTriangles*9) { error = QStringLiteral("STL vượt giới hạn số tam giác."); return false; }
        } else if (stage==Stage::EndLoop && key=="endloop") stage=Stage::EndFacet;
        else if (stage==Stage::EndFacet && key=="endfacet") stage=Stage::Facet;
        else ok=false;
        if (!ok) { error = QStringLiteral("STL ASCII sai cấu trúc ở dòng %1 hoặc STL nhị phân bị hỏng.").arg(line); return false; }
    }
    if (stage!=Stage::Header || output.empty()) { error = QStringLiteral("STL rỗng hoặc thiếu phần kết thúc."); return false; }
    return true;
}

inline bool readMesh(const QString &file, std::vector<float> &out, QString &error) {
    try { return parseSTL(file, out, error); }
    catch (const std::bad_alloc &) { error = QStringLiteral("Không đủ bộ nhớ để đọc STL."); return false; }
}
}

inline bool readNativeSTL(const QString &file,std::vector<float> &out,QString &error) {
    std::vector<float> parsed;
    if (!native_stl::readMesh(file,parsed,error)) return false;
    out.swap(parsed);
    return true;
}
