# ESPHome component for Toshiba AC

Should be compatible with models Toshiba Suzumi/Shorai/Seiya.

Toshiba air conditioner has an option for connecting remote module purchased separately. This project aims to replace this module with more affordable and universal ESP module and to allow integration to home automation systems like HomeAssistent.

This ESPHome component use GPIO PINs 32(RX)/33(TX) to connect with Toshiba AC and communicates via MQTT.

# Hardware
* tested with ESP32 (WROOM 32D), but might work with ESP8266 too (not tested)
* connection adapter can be used the same as in original module (https://github.com/toremick/shorai-esp32).
* Alternatively, you can use only a level shifter between 5V (AC unit) and 3.3V (ESP32) (search Aliexpress for "level shifter"). That's what I use and it works fine.

# Installation
Installation is via ESPHome external component feature (see https://esphome.io/components/external_components.html).

The component can be installed locally by downloading to `components` directory or directly from Github.

see [example.yaml](https://github.com/pedobry/esphome_toshiba_suzumi/blob/main/example.yaml) for configuration details.

```
esphome run example.yaml
```

# Integration with HA
Once the ESP device is flashed with ESPHome using this configuration, it automatically populates to Home Assistant via MQTT Discovery.
![MQTT Discovery entity](/images/HA_mqtt_entity.png)

You can then create a Thermostat card on the dashboard.

![HomeAssistant card](/images/HA_card.png)

Outdoor temperature is populated as temp sensor. 

# Links
https://www.espressif.com/en/products/devkits/esp32-devkitc/

https://github.com/toremick/shorai-esp32

https://github.com/Vpowgh/TConnect

