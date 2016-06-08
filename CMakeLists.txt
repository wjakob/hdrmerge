cmake_minimum_required(VERSION 2.6)
project(hdrmerge)

set(CMAKE_BUILD_TYPE Release)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/misc/")

if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
	find_package(LibXml2 REQUIRED)
endif()

find_package(JPEG REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(EXIV2 REQUIRED)
find_package(OpenMP)
find_package(JPEG)
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.59 COMPONENTS system program_options filesystem REQUIRED )

if (OPENMP_FOUND)
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
	include_directories(${LIBXML2_INCLUDE_DIR})
endif()


if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
  find_library(CORESERVICES_LIBRARY CoreServices)
endif()

include_directories(${OPENEXR_INCLUDE_PATHS})
include_directories(${EXIV2_INCLUDE_DIR})
include_directories(${Boost_INCLUDE_DIR})
include_directories(${JPEG_INCLUDE_DIR})

if("${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
	# for pthread.h
	include_directories(${CMAKE_SOURCE_DIR}/rawspeed/include)
endif()

# Compile with C++11 features
set(CMAKE_CXX_FLAGS "-std=c++11 -Wall -Wno-deprecated-declarations -Wno-deprecated-register -Wno-unused-local-typedefs ${CMAKE_CXX_FLAGS}")

add_definitions(-D_USE_MATH_DEFINES -D_UNICODE -DUNICODE)

add_subdirectory(rawspeed)

if("${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
	set(PTHREAD_LIBRARY	"${CMAKE_SOURCE_DIR}/rawspeed/lib64/pthreadVC2.lib")
endif()

add_executable(hdrmerge input.cpp output.cpp main.cpp hdr.cpp fitexp.cpp resample.cpp misc.cpp ${RAWSPEED_SOURCES})

target_link_libraries(hdrmerge rawspeed ${LIBXML2_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} 
  ${JPEG_LIBRARIES} ${OPENEXR_LIBRARIES} ${EXIV2_LIBRARY}
  ${Boost_LIBRARIES} ${JPEG_LIBRARIES} ${PTHREAD_LIBRARY} ${CORESERVICES_LIBRARY})
