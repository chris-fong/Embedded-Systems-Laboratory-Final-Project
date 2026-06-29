# Mbed CE Final Project Remote
cmake_minimum_required(VERSION 3.19)
cmake_policy(VERSION 3.19...3.22)

set(MBED_APP_JSON_PATH mbed_app.json)

include(mbed-os/tools/cmake/mbed_toolchain_setup.cmake)
project(F769_Remote LANGUAGES C CXX ASM)
include(mbed_project_setup)

# OS and Main GUI Library
add_subdirectory(mbed-os)
add_subdirectory(lvgl)

# ---------------------------------------------------------
# 1. LVGL & Touchscreen Hardware Config (From Lab 9.3)
# ---------------------------------------------------------
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/lvgl
    ${CMAKE_SOURCE_DIR}/Utilities/STM32F769I-Discovery
    ${CMAKE_SOURCE_DIR}/Utilities/Components/ft6x06/
)

FILE(
    GLOB_RECURSE HAL_Sources 
    Utilities/Components/ft6x06/ft6x06.c
    Utilities/Components/otm8009a/otm8009a.c
    Utilities/STM32F769I-Discovery/stm32f769i_discovery.c
    Utilities/STM32F769I-Discovery/stm32f769i_discovery_ts.c
    Utilities/STM32F769I-Discovery/stm32f769i_discovery_lcd.c
    Utilities/STM32F769I-Discovery/stm32f769i_discovery_sdram.c
)

FILE(GLOB_RECURSE LVGL_Sources CONFIGURE_DEPENDS 
     hal_stm_lvgl/tft/tft.cpp
     hal_stm_lvgl/touchpad/touchpad.cpp
)

add_definitions(
    -DSTM32
    -DSTM32F7
    -DSTM32F769xx
    -DSTM32F769I_DISCO
    -DDEBUG
    -DUSE_HAL_DRIVER
    -DDMA2_Stream0=0
)

# ---------------------------------------------------------
# 2. MQTT Library Config (From Lab 11.4)
# ---------------------------------------------------------
add_library(paho_mqtt STATIC
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTConnectClient.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTConnectServer.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTDeserializePublish.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTSerializePublish.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTFormat.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTPacket.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTSubscribeClient.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTSubscribeServer.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTUnsubscribeClient.c
    ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket/MQTTUnsubscribeServer.c
)

# ---------------------------------------------------------
# 3. Main Executable & Linking
# ---------------------------------------------------------
add_executable(${PROJECT_NAME} 
    main.cpp
    ${HAL_Sources}
    ${LVGL_Sources}
)

target_include_directories(${PROJECT_NAME} 
    PRIVATE 
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/wifi_mqtt
        ${CMAKE_SOURCE_DIR}/components/paho_mqtt
        ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTClient
        ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTClient/mbed
        ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket
)

if("MBED_CONF_TARGET_APPLICATION_PROFILE=bare-metal" IN_LIST MBED_CONFIG_DEFINITIONS)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        m
        lvgl
        lvgl_demos
        paho_mqtt
        mbed-netsocket
        mbed-mbedtls
        mbed-baremetal 
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        m
        lvgl
        lvgl_demos
        paho_mqtt
        mbed-netsocket
        mbed-mbedtls
        mbed-os 
    )
endif()

mbed_set_post_build(${PROJECT_NAME})
