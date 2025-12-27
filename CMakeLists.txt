# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称和语言（显式启用 C 和 CXX）
project(ServerApp LANGUAGES C CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找线程库（pthread）
find_package(Threads REQUIRED)

# 列出所有源文件
set(SOURCES
    server.cpp
    cam.cpp
    serial.c
)

# 添加可执行文件
add_executable(server ${SOURCES})

# 链接 pthread
target_link_libraries(server PRIVATE Threads::Threads)

# 可选：设置编译选项（如警告、优化）
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(server PRIVATE -Wall -Wextra -O2)
endif()

# 确保头文件能被找到（当前目录）
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 安装规则（可选）
install(TARGETS server DESTINATION bin)