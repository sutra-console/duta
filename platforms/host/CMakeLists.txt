cmake_minimum_required(VERSION 3.13)
project(duta_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The WebSocket server (built on the shared core).
add_executable(duta-host src/main.c)
target_include_directories(duta-host PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol)
if(MSVC OR WIN32)
  target_link_libraries(duta-host PRIVATE ws2_32)
endif()

# Hardware-free unit test for the WebSocket codec (CI).
add_executable(duta-host-test-ws test_ws.c)
target_include_directories(duta-host-test-ws PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
enable_testing()
add_test(NAME ws_codec COMMAND duta-host-test-ws)
