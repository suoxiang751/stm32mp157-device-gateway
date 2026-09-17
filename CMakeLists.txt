cmake_minimum_required(VERSION 3.16)
project(stm32mp157_device_gateway VERSION 1.0.0 LANGUAGES CXX)

option(GATEWAY_BUILD_TESTS "Build protocol and state tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(gateway_core
    src/config.cpp
    src/crc16.cpp
    src/modbus_rtu.cpp
    src/modbus_tcp.cpp
    src/serial_port.cpp
    src/mqtt_client.cpp
    src/device_state.cpp
    src/gateway.cpp
)

target_include_directories(gateway_core PUBLIC include)
target_link_libraries(gateway_core PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(gateway_core PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(gateway_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(gateway_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_executable(device_gateway src/main.cpp)
target_link_libraries(device_gateway PRIVATE gateway_core)

install(TARGETS device_gateway RUNTIME DESTINATION bin)
install(FILES config/gateway.conf DESTINATION etc/stm32mp157-device-gateway)
install(FILES deploy/stm32mp157-device-gateway.service DESTINATION lib/systemd/system)

if(GATEWAY_BUILD_TESTS)
    enable_testing()
    add_executable(gateway_tests tests/test_main.cpp)
    target_link_libraries(gateway_tests PRIVATE gateway_core)
    add_test(NAME gateway_tests COMMAND gateway_tests)
endif()

