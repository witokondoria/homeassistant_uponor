#define SECRET_SSID          "homewifi";
#define SECRET_WIFI_PASSWORD "homewifipassword";
#define SECRET_MQTT_PASSWORD "uponormqttpassword";
#define MQTT_SERVER           "core-moquitto";

#define myArgs(...) __VA_ARGS__
#define INIT_ARR(VAR_NAME,ARR_DATA) int VAR_NAME[] = {myArgs ARR_DATA}

INIT_ARR(thermoCodes, (0xCD, 0x9F, 0x8B, 0x72, 0x26));
