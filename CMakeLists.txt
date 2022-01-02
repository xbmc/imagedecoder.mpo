cmake_minimum_required(VERSION 3.5)
project(imagedecoder.mpo)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(JPEG REQUIRED)
find_package(TinyXML2 REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/..
                    ${JPEG_INCLUDE_DIR}
                    ${TINYXML2_INCLUDE_DIRS}
                    ${PROJECT_SOURCE_DIR}/lib)

add_subdirectory(lib/libmpo EXCLUDE_FROM_ALL)

set(MPO_SOURCES src/MPOPicture.cpp
                lib/TinyEXIF/TinyEXIF.cpp)

set(MPO_HEADERS src/MPOPicture.h
                lib/TinyEXIF/TinyEXIF.h)

set(DEPLIBS mpo_static ${JPEG_LIBRARIES} ${TINYXML2_LIBRARIES})

build_addon(imagedecoder.mpo MPO DEPLIBS)

include(CPack)
