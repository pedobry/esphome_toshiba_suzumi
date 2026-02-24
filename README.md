# ESPHome component for Toshiba AC

Should be compatible with models Toshiba Suzumi/Shorai/Seiya and other models using the same protocol.

Toshiba air conditioner has an option for connecting remote module purchased separately. This project aims to replace this module with more affordable and universal ESP module and to allow integration to home automation systems like HomeAssistent.

This component use ESPHome UART to connect with Toshiba AC and communicates directly with Home Assistant.

<a href="https://www.buymeacoffee.com/pedobryk" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>

## ESPHome versions
Current main branch supports latest ESPHome **2026.1**. There have been some internal changes in climate entity in ESPHome which makes it incompatible with previous ESPHome versions.

If you use ESPHome **2025.10 or earlier**, use tag `2025.10` like this:

```
external_components:
  - source: 
      type: git
      url: https://github.com/pedobry/esphome_toshiba_suzumi
      ref: "2025.10"
    components: [toshiba_suzumi]
    refresh: 1min
```
If you use ESPHome **2025.11 or 2025.12**, use tag `2025.12` like this:

```
external_components:
  - source: 
      type: git
      url: https://github.com/pedobry/esphome_toshiba_suzumi
      ref: "2025.12"
    components: [toshiba_suzumi]
    refresh: 1min
```

These older version won't be maintained, I'd recommend to upgrade to ESPHome 2026.1

## Supported Toshiba units

Any unit which have an option to purchase a WiFi adapter RB-N105S-G/RB-N106S-G:

* Seiya RAS-B24 J2KVG-E
* Suzumi Plus RAS-B18, B22 and B24 PKVSG-E
* Shorai Premium RAS-B18, B22 and B24 J2KVRG-E
* Daiseikai 9 RAS-B10, B13 and B16 PKVPG-E
* Shorai Edge RAS-B07, B10, B13, B16, B18, B22 and B24 J2KVSG-E
* Seiya RAS-B10, B13, B16 and B18 J2KVG-E
* Suzumi Plus RAS-B10, B13 and B16 PKVSG-E
* Shorai Premium RAS-B10, B13 and B16 J2KVRG-E

## Hardware

* tested with ESP32 (WROOM 32D)
* tested with ESP8266 (Lolin D1 mini)
* connection adapter can be used the same as with this solution (https://github.com/toremick/shorai-esp32).
* Alternatively, you can use only a level shifter between 5V (AC unit) and 3.3V (ESP32) (search Aliexpress for "level shifter"). That's what I use and it works fine.

### ESP32 (WROOM 32D)

   ![schema](/images/schema.jpg)

   Please pay attention to correctly wire the level shifter - it must have both High Voltage (5V and GND from the AC unit) and Low Voltage (3.3V and GND from ESP) connected.
  

   ![adapter](/images/adapter.jpg)

### ESP 8266EX (Lolin D1 mini)

   ![d1_mini](/images/D1_mini.jpg)

### Pinout

AC unit has a wifi connector CN22 with an extension cable, usually with pink and blue colors (see the schema above).

<p align="center">

|pin number| color | ESP32 pin  |ESP8266 pin|
|----------|-------|------------|-----------|
|    1     |🟦 blue  | 33 (TX)    | 13 (TX)   |
|    2     |🟪 pink  | GND        | GND       |
|    3     |⬛️ black | \+5V (Vin) | \+5V (Vin)|
|    4     |⬜️ white | 32 (RX)    | 12 (RX)   |
|    5     |🟪 pink  | **do NOT use** | **do NOT use** |

</p>

The matching connector is JST PA2.0 ([Aliexpress](https://www.aliexpress.com/item/1005007176563512.html))

‼️‼️<br>
**WARNING: Do NOT connect PIN 5 (the outermost pink wire) to anything. Double check that you have wired the ESP device correctly. Only connect and disconnect the ESP while the AC unit is disconnected from main power. Shorts or wrong wiring will damage AC unit board.**<br>
‼️‼️

## Installation

1. install Home Assistant and [ESPHome Addon](https://esphome.io/guides/getting_started_hassio.html)

2. add new ESP32 device to ESPHome Dashboard. This will flash the device and create a basic configuration of the node.

   ![ESPHome entity](/images/HA_ESPHome.png)

3. edit code configuration and setup UART and Climate modules. See [example.yaml](https://github.com/pedobry/esphome_toshiba_suzumi/blob/main/example.yaml) for configuration details.

```yaml
...
external_components:
  - source: 
      type: git
      url: https://github.com/pedobry/esphome_toshiba_suzumi
    components: [toshiba_suzumi]

uart:
  id: uart_bus
  tx_pin: 33
  rx_pin: 32
  parity: EVEN
  baud_rate: 9600

climate:
  - platform: toshiba_suzumi
    name: living-room
    id: living_room
    uart_id: uart_bus
    outdoor_temp:        # Optional. Outdoor temperature sensor
      name: Outdoor Temp
      filters:
        # Filter out value 127 as that's what unit sends when it can's measure the outside temp.
        - filter_out: 127 
    power_select:
      name: "Power level"
    #horizontal_swing: true # Optional. Uncomment if your HVAC supports also horizontal swing
    #supported_presets:          # Optional. Enable only the features your HVAC
      #- "Standard"
      #- "Hi POWER"
      #- "ECO"
      #- "Fireplace 1"
      #- "Fireplace 2"
      #- "8 degrees"             # When enabling this mode, the temp range is set to 5-30°C
      #- "Silent#1"
      #- "Silent#2"
      #- "Sleep"
      #- "Floor"
      #- "Comfort"
switch:
  - platform: toshiba_suzumi
    climate_id: living_room
    name: "Wifi Led" # Optional. Control LED on internal unit.
  - platform: toshiba_suzumi
    type: debug
    climate_id: living_room
    name: "Debug" # Optional diagnostic switch.
    #poll_interval: 30s     # Optional. Re-poll interval for discovered IDs.
    #batch_size: 1          # Optional. IDs queried per pass (can be 10 or 255).
    #initial_from_id: 128   # Optional. Initial scan start ID (default matches scan button behavior).
    #initial_to_id: 254     # Optional. Initial scan end ID.
...
```

The component is automatically installed directly from Github during compilation.

When configured correctly, new ESPHome device will appear in Home Assistant integrations and you'll be asked to provide encryption key (it's in the node configuration from step 2.). All entities then populate automatically.

![HomeAssistant ESPHome entity](/images/HA_entity.png)

You can then create a Thermostat card on the dashboard.

![HomeAssistant card](/images/HA_card.png)

### Temperature range (FrostGuard)
Homeassistant thermostat component is by default set with a range of 17-30°C.
If your unit is equipped with "8 degress" aka FrostGuard, you can enable that in YAML configuration (Supported presets). The range is then automatically set to 5-30°C and when you set target temp above 17°C, it will switch to Standard mode, when you set target temp below 17°C, it will switch automatically to FrostGuard.

## Filter some oustide temp value

It has been reported that some AC units send temp value 127 when the unit does not know the temp or using only Fan mode without external unit running. This mess up graphs in HomeAssistant.

You can filter unwanted values by adding a filter to Outside temp sensor:

```yaml
    outdoor_temp:
      name: Outdoor Temp
      filters:
        - filter_out: 127 
```

## Scan for unknown sensors

The code here was developed on certain Toshiba AC unit which provides only certain set of features. Newer or different units might offer more features (ie. reporting of power usage, horizontal swing etc.). While these are not implemented, you can add a button which scans for all sensors and prints the answers from AC unit. This might help developers to identify these new features.

You can enable this by adding a new button:

```yaml
button:
  - platform: template
    name: "Scan for unknown sensors"
    icon: "mdi:reload"
    on_press:
      then:
        - lambda: |-
            auto* controller = static_cast<toshiba_suzumi::ToshibaClimateUart*>(id(living_room));
            controller->scan();
```

and then watching ESPHome logs for data:

![ESPHome log](/images/scan_log.png)
    ```

`type: debug` behavior:
- `Debug=ON`: runs one initial scan (`initial_from_id..initial_to_id`) and tracks payloads of responding IDs.
- When a tracked payload changes, `Debug txt` publishes: `id:<id> old:<last_hex> new:<new_hex>`.
- While ON: every `poll_interval`, it starts a full poll cycle over all discovered IDs, processed in loop chunks of `batch_size`.
- `Debug=OFF`: stops debug polling and keeps last values.

## Links

https://www.espressif.com/en/products/devkits/esp32-devkitc/

https://github.com/toremick/shorai-esp32

https://github.com/Vpowgh/TConnect

Discord channel: https://discord.gg/wYYFawvqfr
