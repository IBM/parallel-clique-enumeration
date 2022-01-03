cmake_minimum_required(VERSION 3.2)
project(hash-join-mce)

INCLUDE($ENV{TBBROOT}/cmake/TBBBuild.cmake)
SET(TBB_DIR "$ENV{TBBROOT}/cmake")

SET(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_COLOR_MAKEFILE ON)

string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE)
ADD_DEFINITIONS(-pthread) #-Wall

IF(CMAKE_C_COMPILER MATCHES "clang")
    MESSAGE(STATUS "Compiling with CLANG")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx512f -stdlib=libc++")#-fopenmp
ELSEIF(CMAKE_C_COMPILER MATCHES "gcc")
    MESSAGE(STATUS "Compiling with GCC")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx512f -ffast-math -mveclibabi=svml -malign-data=cacheline") #-fopenmp
ELSEIF(CMAKE_C_COMPILER MATCHES "icc")
    MESSAGE(STATUS "Compiling with ICC")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -xMIC-AVX512 -align")# -qopenmp
ENDIF()

IF(CMAKE_BUILD_TYPE MATCHES "DEBUG")
    MESSAGE(STATUS "DEBUG build")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG -O0 -fno-inline -fno-omit-frame-pointer")
    SET(CMAKE_VERBOSE_MAKEFILE ON)
    IF(CMAKE_C_COMPILER MATCHES "clang")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-optimize-sibling-calls")
    ELSEIF(CMAKE_C_COMPILER MATCHES "gcc")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftrapv")
        IF(CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.8)
            SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-optimize-sibling-calls")
        ENDIF()
        IF(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 8.2)
            SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -ggdb")
            SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address -ggdb")
        ENDIF()
    ENDIF()
ELSEIF (CMAKE_BUILD_TYPE MATCHES RELEASE)
    MESSAGE(STATUS "RELEASE build")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
ENDIF()

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/include)
SET(ALL_SRC  src/BronKerboschTBB.cpp  src/UnrolledList.cpp src/Graph.cpp src/BKTask.cpp  src/main.cpp)

ADD_EXECUTABLE(mce ${ALL_SRC})

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    MESSAGE(STATUS "TBB include :" ${TBB_DIR}/../include)
    FIND_PACKAGE(TBB COMPONENTS tbbmalloc tbbmalloc_proxy REQUIRED)
    TARGET_LINK_LIBRARIES(mce tbb tbbmalloc tbbmalloc_proxy)
    INCLUDE_DIRECTORIES(${TBB_DIR}/../include)
    MESSAGE(STATUS "TBB include :" ${TBB_DIR}/../include)
ENDIF()

IF(CMAKE_BUILD_TYPE MATCHES DEBUG)
    MESSAGE(STATUS "Project dir: " ${PROJECT_SOURCE_DIR})
    MESSAGE(STATUS "Source dir : " ${PROJECT_SOURCE_DIR}/src)
    MESSAGE(STATUS "Include dir: " ${PROJECT_SOURCE_DIR}/include)
    MESSAGE(STATUS "Platform   : " ${CMAKE_SYSTEM_NAME})
    MESSAGE(STATUS "TBB include :" ${TBB_DIR}/../include)
    MESSAGE(STATUS "Compiler    :" "${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_VERSION}")
ENDIF()
