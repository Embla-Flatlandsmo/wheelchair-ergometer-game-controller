#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_include_directories(app PRIVATE .)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules_common.c)
target_sources_ifdef(CONFIG_ENCODER_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/encoder_module.c)
target_sources_ifdef(CONFIG_HID_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hid_module.c)
target_sources_ifdef(CONFIG_LED_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/led_module.c)