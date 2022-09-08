# Wheelchair ergometer game controller

This is a project that aims to interface an Invictus Trainer with a computer or a phone by using encoders and an nRF52840.

## Building
To build a project, follow [the official nRF Connect SDK guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_programming.html)
## Project setup (Using existing nRF Connect SDK installation)
- Clone this project into any folder that has an [nRF Connect SDK installation](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_assistant.html). I used ncs v2.0.2, but I think anything after v1.6.0 should work (pre-v1.6.0 has no support for the IMU, MPU9250).
- Build and flash the project as you would with any sample. Make sure that the physical switch `SW10` on the DK is set to the chip you want to flash to.

## Project setup (New nRF Connect SDK installation)
- Ensure all required software for building nRF Connect SDK v2.0.2 is installed. A list of required software and appropriate versions can be found at https://developer.nordicsemi.com/nRF_Connect_SDK/doc/2.0.2/nrf/gs_recommended_versions.html.
- Create project directory
- Inside project directory, run command ```west init -m https://github.com/Embla-Flatlandsmo/wheelchair-ergometer-game-controller.git```. This will set up the west configurations for the project directory.
- In the same directory, run ```west update```. This will download the project files, nRF Connect SDK v1.9.0, and their dependencies.
- Navigate to folder containing firmware (`wheelchair-ergometer-game-controller`) and use west commands for building and flashing the firmware as described in the [developer guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/2.0.2/zephyr/guides/west/build-flash-debug.html#west-build-flash-debug).  
<!-- 
- Navigate to folder containing firmware for the microcontroller you want to work with (```./balancing_robot_firmware/<MCU NAME>```) and use west commands for building and flashing the firmware as described in the [developer guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/1.9.0/zephyr/guides/west/build-flash-debug.html#west-build-flash-debug). 
-->