#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_include_directories(app PRIVATE .)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules_common.c)
target_sources_ifdef(CONFIG_BLE_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ble_module.c)
target_sources_ifdef(CONFIG_QDEC_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qdec_module.c)
target_sources_ifdef(CONFIG_HID_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hid_module.c)