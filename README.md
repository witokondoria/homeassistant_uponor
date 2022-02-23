This repo contains an Arduino sketch that takes care of polling an Uponor Modbus setup (such as the SMatrix Wave), gathering each thermostat:

* setpoint temperature
* measured temperature

Each thermostat is exposed so that a homeassistant setup detects them when querying an MQTT topic. The discovered elements are named after climate.uponorX (being X a number starting on 0)

Several attributes can be customised at the secrets.h file, for the discovery to be succesful:

* SECRET_SSID: WIFI network name.
* SECRET_WIFI_PASSWORD: WIFI password.
* SECRET_MQTT_PASSWORD: MQTT password. The username is expected to be uponor.
* MQTT_SERVER: hostname of your MQTT broker.
* thermoCodes: being a list of thermostats HEX ids (second byte on each RS485 stream). Ech position on the list will be used s thee previous X value (on climate.uponorX)

Multiple retained messages are delivered to homeassistant/climate/uponor/X/config, for the autodiscovery to kick.
For each declared thermostat, a retained message is delivered to the uponor/availability topic announcing each thermostat onlineavailability. The current version does not check that messages are arriving for the thermostat. The LTW message lets the availability to become offline.

1 in 4 streams for each thermostat are selected to update the setpoint and current temperatures on the appropriate MQTT topic.

Wether the room is heating or not is guessed after substracting the current temperature from the setpoint one.

This sketch has been tested on a Wemos D1 mini connected to a MAX485. Thermostats features such as cooling or using a remote temperature sensor has been ommited.

Future plans include behaving as man in the middle, overwriting the setpoint temperatures defined at each unit in order to force the heating to start on a room (unless info on the Uponor programmer becomes available)