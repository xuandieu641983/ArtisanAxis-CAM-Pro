#include "cam/geometry.h"
#include "stl_reader.h"

#include <QCoreApplication>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

double duration(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}

double peakMiB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        throw std::runtime_error("GetProcessMemoryInfo failed");
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024. * 1024.);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        throw std::runtime_error("getrusage failed");
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss) / (1024. * 1024.);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.;
#endif
#endif
}

// Fixed-width generator avoids using a library-specific distribution.
double sample(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<double>(state >> 8) / 16777216.;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 2) {
        std::cerr << "Usage: ArtisanBVHBenchmark heightfield.stl\n";
        return 2;
    }
    try {
        std::vector<float> raw;
        QString error;
        const auto startRead = Clock::now();
        if (!readNativeSTL(args[1], raw, error)) {
            std::cerr << error.toStdString() << '\n';
            return 1;
        }
        std::vector<cam::Tri> triangles(raw.size() / 9);
        for (std::size_t i = 0; i < raw.size(); i += 3) {
            triangles[i / 9].p[(i / 3) % 3] = {raw[i], raw[i + 1], raw[i + 2]};
        }
        const auto count = triangles.size();
        std::vector<float>().swap(raw);
        const auto startBuild = Clock::now();
        const cam::Tree bvh(std::move(triangles));
        const auto startQuery = Clock::now();
        constexpr unsigned queryCount = 10000;
        std::uint32_t state = 0x41a10u;
        double checksum = 0.;
        for (unsigned i = 0; i < queryCount; ++i) {
            const auto x = 1. + 98. * sample(state);
            const auto y = 1. + 43. * sample(state);
            const auto height = bvh.drop(x, y, 0.4, true);
            if (!std::isfinite(height)) throw std::runtime_error("Drop-Cutter query missed STL");
            checksum += height;
        }
        const auto finish = Clock::now();
        const double querySeconds = duration(startQuery, finish);
        std::cout.imbue(std::locale::classic());
        std::cout << std::fixed << std::setprecision(6)
                  << "{\"triangles\":" << count
                  << ",\"decode_and_convert_seconds\":" << duration(startRead, startBuild)
                  << ",\"bvh_seconds\":" << duration(startBuild, startQuery)
                  << ",\"queries\":" << queryCount
                  << ",\"query_seconds\":" << querySeconds
                  << ",\"queries_per_second\":" << (queryCount / querySeconds)
                  << ",\"peak_mib\":" << peakMiB()
                  << ",\"checksum\":" << checksum << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
