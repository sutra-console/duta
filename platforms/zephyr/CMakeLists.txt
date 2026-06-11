cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(duta_zephyr)

# The skrit contract + the shared portable core (header-only).
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol
  ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_DUTA_SNIFF app PRIVATE src/duta_ble_sniff.c)
