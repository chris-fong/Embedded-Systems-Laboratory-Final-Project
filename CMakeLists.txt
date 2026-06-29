#
# Mbed CE Final Project - Merged BL-IOT & MQTT
#
cmake_minimum_required(VERSION 3.19)
cmake_policy(VERSION 3.19)

#### Initialize Mbed OS build system. ####
######################################################################################################
# Set path of mbed_app.json5 (necessary at all times)
set(MBED_APP_JSON_PATH mbed_app.json5)

# include app.cmake (necessary everytime)
include(mbed-os/tools/cmake/app.cmake)

######################################################################################################
### Block of including project folders

# Add mbed-os subdirectory
add_subdirectory(mbed-os)

# Add your BBCar subdirectories
add_subdirectory(pwmin)
add_subdirectory(bbcar)

# Add the WiFi and MQTT subdirectories (from 11_2)
add_subdirectory(components/wifi-ism43362)
add_subdirectory(components/paho_mqtt/MQTTPacket)

# Manually compile the paho_mqtt library (from 11_2)
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

######################################################################################################

### Set up your project name (Retaining MbedCEHelloWorld as requested)
project(MbedCEHelloWorld)

### add executable
add_executable(${CMAKE_PROJECT_NAME} main.cpp)

######################################################################################################
### Include directories for MQTT (from 11_2)
target_include_directories(${CMAKE_PROJECT_NAME} 
    PRIVATE 
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/components/paho_mqtt
        ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTClient
        ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTClient/mbed
        ${CMAKE_SOURCE_DIR}/components/paho_mqtt/MQTTPacket
)

### Link libraries block
# Link MbedOS, BBCar libs, and all the required Network/WiFi/MQTT libs
target_link_libraries(${CMAKE_PROJECT_NAME} 
    PRIVATE
        mbed-os
        pwmin 
        bbcar
        mbed-netsocket
        mbed-mbedtls
        paho_mqtt
        wifi-ism43362
)

######################################################################################################
### Set post build
mbed_set_post_build(${CMAKE_PROJECT_NAME})

### Finalize build for VS Code / IDEs
mbed_finalize_build()
