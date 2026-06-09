cmake_minimum_required(VERSION 3.13)
project(duta_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(duta-host src/main.c)
target_include_directories(duta-host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol)
if(MSVC)
  target_link_libraries(duta-host PRIVATE ws2_32)
endif()
