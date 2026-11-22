# Test-001 Teensy2 - answers Teensy1's pings over Ethernet (Teensy 4.1, Zephyr RTOS, C++20)
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test001_teensy2)

target_sources(app PRIVATE src/main.cpp)
# common/comms.hpp: the IPs, port and packet both boards share
target_include_directories(app PRIVATE ../common)
