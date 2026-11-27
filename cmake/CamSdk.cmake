find_package(embree 4 CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(OpenVDB REQUIRED)
find_package(OpenNURBS CONFIG QUIET)
if(NOT OpenNURBS_FOUND)
    find_path(ARTISAN_OPENNURBS_INCLUDE NAMES opennurbs.h REQUIRED)
    find_library(ARTISAN_OPENNURBS_LIBRARY NAMES opennurbs OpenNURBS REQUIRED)
endif()
# M1 viewport uses Qt OpenGL. The geometry libraries above are checked on demand
# before M2+ integration and are not load-time dependencies of either M1 target.
