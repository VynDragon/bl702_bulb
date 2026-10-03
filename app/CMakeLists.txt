cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(bl702_bulb)

get_filename_component(CHIP_ROOT ${ZEPHYR_BASE}/../modules/lib/matter REALPATH)

include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_include_directories(app PRIVATE
    include
    ${CHIP_ROOT}/zzz_generated/app-common)

target_sources(app PRIVATE
    src/main.cpp
    src/ZclCallbacks.cpp
    src/LightDriver.cpp
    src/atomic_helpers.c)

# Generates and compiles the endpoint/cluster code
chip_configure_data_model(app ZAP_FILE ${CMAKE_CURRENT_SOURCE_DIR}/lighting-app.zap)

if(NOT DEFINED ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR)
    message(FATAL_ERROR "Matter is not registered as a Zephyr module. Add it to your west manifest.")
endif()
set(CHIP_ROOT ${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR})

# ot-config re-lists zephyr_interface's INTERFACE_COMPILE_DEFINITIONS, including
# $<COMPILE_LANGUAGE:CXX> generator expressions. openthread/CMakeLists.txt uses
# ot-config's defines to build a plain custom target, where COMPILE_LANGUAGE is
# invalid. Strip it so LLVM/Clang builds configure cleanly.
if(CONFIG_OPENTHREAD AND TARGET ot-config AND TARGET zephyr_interface)
    get_target_property(OT_CONFIG_DEFS ot-config INTERFACE_COMPILE_DEFINITIONS)
    if(OT_CONFIG_DEFS)
        list(FILTER OT_CONFIG_DEFS EXCLUDE REGEX "TARGET_PROPERTY:zephyr_interface,INTERFACE_COMPILE_DEFINITIONS")
    else()
        set(OT_CONFIG_DEFS "")
    endif()
    get_target_property(ZEPHYR_INTERFACE_DEFS zephyr_interface INTERFACE_COMPILE_DEFINITIONS)
    if(ZEPHYR_INTERFACE_DEFS)
        list(FILTER ZEPHYR_INTERFACE_DEFS EXCLUDE REGEX "COMPILE_LANGUAGE:CXX")
    else()
        set(ZEPHYR_INTERFACE_DEFS "")
    endif()
    set_target_properties(ot-config PROPERTIES
        INTERFACE_COMPILE_DEFINITIONS "${OT_CONFIG_DEFS};${ZEPHYR_INTERFACE_DEFS}")
endif()

include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_compile_options(app PRIVATE -Werror -Wno-error=format)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(app PRIVATE -Werror -Wno-error=maybe-uninitialized)
endif()

if(NOT APP_TASK_IMPL_HEADER STREQUAL "")
    target_compile_definitions(app PRIVATE APP_TASK_IMPL_HEADER="${APP_TASK_IMPL_HEADER}")
endif()

zephyr_linker_sources(RAM_SECTIONS reloc.ld)
zephyr_code_relocate(FILES
  ${ZEPHYR_BASE}/subsys/net/ip/net_if.c
  ${ZEPHYR_BASE}/subsys/net/ip/connection.c
  ${ZEPHYR_BASE}/subsys/net/ip/net_context.c
  ${ZEPHYR_BASE}/modules/mbedtls/zephyr_init.c
  ${ZEPHYR_BASE}/subsys/settings/src/settings_nvs.c
  LOCATION RETRAM_BSS)
zephyr_code_relocate(FILES
  src/main.cpp
  LOCATION ITCM_BSS)
