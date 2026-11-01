cmake_minimum_required(VERSION 3.15)
project(SlimeOptimizer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# CLion 里的运行目标名固定为 SlimeOptimizer
add_executable(SlimeOptimizer
    main.cpp
    src/java_random.cpp
    src/slime_check.cpp
    src/csv_reader.cpp
    src/geometry.cpp
    src/optimizer.cpp
    src/visualizer.cpp
    src/config.cpp
)

target_include_directories(SlimeOptimizer PRIVATE include)
target_link_libraries(SlimeOptimizer PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(SlimeOptimizer PRIVATE /O2 /W3)
    # MSVC:静态链接运行库,产物不依赖 vcruntime DLL。
    set_property(TARGET SlimeOptimizer PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(SlimeOptimizer PRIVATE -O2 -Wall -Wextra)
    # GCC/MinGW:静态链接 libgcc/libstdc++/libwinpthread,
    # 生成的 exe 可脱离编译环境,直接发给别人运行。
    target_link_options(SlimeOptimizer PRIVATE
        -static -static-libgcc -static-libstdc++)
endif()
