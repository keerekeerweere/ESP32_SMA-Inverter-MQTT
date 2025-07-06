# ESP32_SMA-Inverter-MQTT
Arduino Project to read SMA Inverter data via ESP32 bluetooth and post to MQTT for consumption by Home Assistant.

It is tested with my 2x SB3000TL-20 and 1x SB1600TL-10 with a plugin SMA bluetooth module.
Please let me know when you have tested the software on other SMA Inverters.

The starting point for this project was the code posted by "ESP32_SMA-Inverter-MQTT" by  and "SBFspot" and "ESP32_to_SMA" on github.
Forked from the great work of Darryl Bond and of Lupo135.
Many thanks for the work on these projects!


### COMPILE:
A working VSCode installation must be installed with the PlatformIO plugin installed

### SETUP:
To first configure:
  - Find the Bluetooth address of your Inverter ( Connect to SMAxxx with smart phone and note the displayed device bluetooth address)
  - Plug the device in, If possible connect the Serial Monitor to check on output
  - Use ESP Touch App found in Apple Store, or Google Play to configure the IP Address, Note the IP Address given
  - Browse to the IP address.
  - Fill out the form with the Bluetooth address and configured inverter password
  - Enter the MQTT broker details
  - The topic preamble defaults to "SMA" 
  - If using Home Assistant, enable auto discovery
  - Save the settings
  - Device will reboot and should display successful connects to the inverter and mqtt server on the serial monitor
  - In Home Assistant add an entities card and search for entities starting with SMA-XXXXXXXXXXX

### Home Assistant
The device performs Home Assistant auto-discovery via MQTT.  
The inverter is added as a device, while all the inverter parameters are defined in Home Assistant entities. The entity label begins with the topic preamble and inverter serial.  
  - Device name: SMA-21005XXXXX
  - Entity label: sma_21005XXXXX_grid_relay_status


### NOTES:

### TODO:
  - Read month and year History
  - refactor code a bit more for more clarity
  - should be possible to merge/fork with the sbfspot project, or at least re-use more code from it without needing to port.
  - should also be possible to directly integrate into Home Assistant through esphome without the going through mqtt. Also this allows OTA update and (re)configuration through yaml files. see my first attempt here: https://github.com/keerekeerweere/esphome_smabluetooth

