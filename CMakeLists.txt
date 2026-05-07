cmake_minimum_required(VERSION 3.16)
project(AoF2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -Wall -msse4.2)
elseif (MSVC)
    add_compile_options(/O2 /W3)
endif()

option(AOF2_STATIC "Statically link MinGW runtime to make a portable .exe" ON)
if (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND AOF2_STATIC)
    add_link_options(-static -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread)
endif()

find_package(Threads REQUIRED)

set(OMPEVAL_DIR "${CMAKE_SOURCE_DIR}/OMPEval")

set(OMPEVAL_SRC
    "${OMPEVAL_DIR}/omp/CardRange.cpp"
    "${OMPEVAL_DIR}/omp/CombinedRange.cpp"
    "${OMPEVAL_DIR}/omp/EquityCalculator.cpp"
    "${OMPEVAL_DIR}/omp/HandEvaluator.cpp"
)

add_library(ompeval STATIC ${OMPEVAL_SRC})
target_include_directories(ompeval PUBLIC "${OMPEVAL_DIR}")
target_link_libraries(ompeval PUBLIC Threads::Threads)

set(CORE_SRC
    src/core/EquityTable.cpp
    src/core/Strategy.cpp
    src/core/CfrSolver.cpp
    src/core/ThreeWayEquityTable.cpp
    src/core/ThreePlayerStrategy.cpp
    src/core/ThreePlayerCfrSolver.cpp
)

add_library(aof2_core STATIC ${CORE_SRC})
target_include_directories(aof2_core PUBLIC src)
target_link_libraries(aof2_core PUBLIC ompeval Threads::Threads)

add_executable(aof2_train src/train_main.cpp)
target_link_libraries(aof2_train PRIVATE aof2_core)

add_executable(aof2_smoke src/smoke_main.cpp)
target_link_libraries(aof2_smoke PRIVATE aof2_core)

add_executable(aof2_eval src/eval_main.cpp)
target_link_libraries(aof2_eval PRIVATE aof2_core)

add_executable(aof2_bench3w src/bench3w_main.cpp)
target_link_libraries(aof2_bench3w PRIVATE aof2_core)
