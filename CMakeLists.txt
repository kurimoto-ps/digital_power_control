# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(digital_power_control)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shared
  ${CMAKE_CURRENT_SOURCE_DIR}/network_core
)
target_sources(app PRIVATE
  network_core/main.c
  network_core/control_client.c
)
