#
# Copyright (c) 2022 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf_cloud_mqtt_multi_service)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/application.c)
target_sources(app PRIVATE src/connection.c)
target_sources(app PRIVATE src/led_control.c)
target_sources(app PRIVATE src/vibration.c)
target_sources(app PRIVATE src/tb_coap.c)

zephyr_include_directories(src)