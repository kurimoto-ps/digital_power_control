# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(REV58_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
list(APPEND BOARD_ROOT "${REV58_ROOT}")
list(APPEND DTS_ROOT "${REV58_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(jparc_rev58_bringup)
if(NOT CONFIG_BOARD_JPARC_REV58)
  message(FATAL_ERROR "This bring-up application requires jparc_rev58")
endif()
target_sources(app PRIVATE src/main.c src/peripheral_checks.c)

if(CONFIG_JPARC_SPI4_REGS_TEST)
  target_sources(app PRIVATE src/spi4_test_server.c
    ${REV58_ROOT}/lib/fpga_link/spi4_protocol.c)
  target_include_directories(app PRIVATE ${REV58_ROOT}/lib/fpga_link)
endif()
