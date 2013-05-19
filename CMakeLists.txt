cmake_minimum_required(VERSION 2.6)
project(hdrmerge)

set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_C_COMPILER /usr/local/bin/gcc-4.8)
#set(CMAKE_CXX_COMPILER /usr/local/bin/g++-4.8)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/misc/")
find_package(LibXml2 REQUIRED)
find_package(JPEG REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(EXIV2 REQUIRED)
find_package(OpenMP)
find_package(Boost 1.40 COMPONENTS program_options REQUIRED )

if (OPENMP_FOUND)
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

include_directories(${LIBXML2_INCLUDE_DIR})
include_directories(${OPENEXR_INCLUDE_PATHS})
include_directories(${EXIV2_INCLUDE_DIR})
include_directories(${Boost_INCLUDE_DIR})

# Compile with C++11 features
set(CMAKE_CXX_FLAGS "-std=c++11 -g -Wall -fno-omit-frame-pointer ${CMAKE_CXX_FLAGS}")

add_subdirectory(rawspeed)
add_executable(hdrmerge input.cpp util.cpp main.cpp hdr.cpp resample.cpp)
target_link_libraries(hdrmerge rawspeed ${LIBXML2_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
	${JPEG_LIBRARIES} ${OPENEXR_ILMIMF_LIBRARY} ${OPENEXR_HALF_LIBRARY}
	${OPENEXR_IEX_LIBRARY} ${OPENEXR_ILMBASE_LIBRARY} ${EXIV2_LIBRARY}
	${Boost_LIBRARIES})
