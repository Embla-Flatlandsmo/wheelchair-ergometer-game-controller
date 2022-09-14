#
# Copyright (c) 2021 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources(app PRIVATE
	    #    ${CMAKE_CURRENT_SOURCE_DIR}/app_module_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/ble_module_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/qdec_module_event.c
		   
)
