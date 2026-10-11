cmake_minimum_required(VERSION 3.20.0)

ExternalZephyrProject_Add(
  APPLICATION control_core_m4
  SOURCE_DIR ${APP_DIR}/control_core
  BOARD nucleo_h755zi_q/stm32h755xx/m4
)

add_dependencies(${DEFAULT_IMAGE} control_core_m4)
sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} control_core_m4)
