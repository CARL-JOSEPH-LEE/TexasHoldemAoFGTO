cmake_minimum_required(VERSION 3.16)
project(AoF2 VERSION 3.1.0 LANGUAGES CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(AOF_SANITIZERS "Enable address/undefined behavior sanitizers on Unix" OFF)
if (AOF_SANITIZERS AND NOT WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")

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
target_include_directories(ompeval SYSTEM PUBLIC "${OMPEVAL_DIR}")
target_link_libraries(ompeval PUBLIC Threads::Threads)

set(CORE_SRC
    src/core/EquityTable.cpp
    src/core/Strategy.cpp
    src/core/CfrSolver.cpp
    src/core/ThreeWayEquityTable.cpp
    src/core/ThreePlayerStrategy.cpp
    src/core/ThreePlayerCfrSolver.cpp
    src/core/SampledSolver.cpp
    src/core/StratifiedSolver.cpp
    src/core/SampledStrategy.cpp
)

add_library(aof2_core STATIC ${CORE_SRC})
target_include_directories(aof2_core PUBLIC src)
target_link_libraries(aof2_core PUBLIC ompeval Threads::Threads)
target_compile_definitions(aof2_core PUBLIC AOF_VERSION_STRING="${PROJECT_VERSION}")
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aof2_core PRIVATE -Wall -Wextra)
elseif (MSVC)
    target_compile_options(aof2_core PRIVATE /W4)
endif()

add_executable(aof2_train src/train_main.cpp)
target_link_libraries(aof2_train PRIVATE aof2_core)

add_executable(aof2_smoke src/smoke_main.cpp)
target_link_libraries(aof2_smoke PRIVATE aof2_core)

add_executable(aof2_eval src/eval_main.cpp)
target_link_libraries(aof2_eval PRIVATE aof2_core)
if (WIN32)
    target_link_libraries(aof2_train PRIVATE shell32)
    target_link_libraries(aof2_eval PRIVATE shell32)
endif()

add_executable(aof2_bench3w src/bench3w_main.cpp)
target_link_libraries(aof2_bench3w PRIVATE aof2_core)

if (BUILD_TESTING)
    add_executable(aof2_reference_tests tests/reference_tests.cpp)
    target_link_libraries(aof2_reference_tests PRIVATE aof2_core)
    add_test(NAME independent_reference_oracles COMMAND aof2_reference_tests)
    set_tests_properties(independent_reference_oracles PROPERTIES TIMEOUT 180)
    add_executable(aof2_tests tests/core_tests.cpp)
    target_link_libraries(aof2_tests PRIVATE aof2_core)
    add_test(NAME core_rules_and_solver COMMAND aof2_tests)
    add_test(NAME evaluator_smoke COMMAND aof2_smoke)
    add_executable(aof2_stratified_tests tests/stratified_tests.cpp)
    target_link_libraries(aof2_stratified_tests PRIVATE aof2_core)
    add_test(NAME stratified_solver_and_sampling COMMAND aof2_stratified_tests)
    set_tests_properties(stratified_solver_and_sampling PROPERTIES TIMEOUT 120)
    set_tests_properties(core_rules_and_solver evaluator_smoke PROPERTIES TIMEOUT 120)
endif()
