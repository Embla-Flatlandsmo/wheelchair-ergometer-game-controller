#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

set(BOARD adafruit_itsybitsy_nrf52840)
set(APPLICATION_CONFIG_DIR "configuration/\${BOARD}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
# find_package(Zephyr)
project(wheelchair_ergometer_game_controller)

# The application uses the configuration/<board> scheme for configuration files.


# NORDIC SDK APP START
target_sources(app PRIVATE 
  src/main.c
  configuration/common/hid_report_desc.c)
# NORDIC SDK APP END


# Include application events and configuration headers
zephyr_library_include_directories(
  src/events
  src/util
  )

zephyr_include_directories(
  configuration/common
  configuration/${BOARD}
  )

# Include source directory
target_include_directories(app PRIVATE src)

# Application directories
add_subdirectory(src/modules)
add_subdirectory(src/events)
add_subdirectory(drivers)