cmake_minimum_required(VERSION 2.6)

#install(FILES data/cameras.xml data/showcameras.xsl DESTINATION ${SHARE_INSTALL}/hdrmerge/rawspeed)

file(GLOB RAWSPEED_SOURCES RawSpeed/*.cpp
    )

#
# build librawspeed
#
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

add_definitions(-D_UNICODE -DUNICODE)
add_library(rawspeed STATIC ${RAWSPEED_SOURCES})
