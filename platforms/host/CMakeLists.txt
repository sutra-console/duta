cmake_minimum_required(VERSION 3.13)
project(Duta_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(Duta-host src/main.c)
target_include_directories(Duta-host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol)
if(MSVC)
  target_link_libraries(Duta-host PRIVATE ws2_32)
endif()
