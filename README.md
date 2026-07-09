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

# Optional. Required only if the energy/power sensor feature is enabled.
# time:
#   - platform: homeassistant
#     id: hass_time

climate:
  - platform: toshiba_suzumi
    name: living-room
    id: living_room
    uart_id: uart_bus
    #time_id: hass_time  # Optional. Required only if the energy/power sensor feature is enabled.
    #time_sync_interval: 24h  # Optional. Time sync interval. Default 24h. Set to 0 to sync only at startup.
    #energy:  # Optional. Daily energy consumption sensor (Wh).
    #  name: "Daily Energy"
    #power:  # Optional. Estimated real-time power sensor (W).
    #  name: "Realtime Power"
    #indoor_temp:        # Optional. Indoor temperature sensor
    #  name: Indoor Temp
    outdoor_temp:        # Optional. Outdoor temperature sensor
      name: Outdoor Temp
    power_select:
      name: "Power level"
    #vertical_air_direction: # Optional. Fixed vertical air direction.
      # Controls the Toshiba horizontal louver, which directs air up/down.
      #name: "Vertical air direction"
    #self_clean:              # Optional. Enables self-clean detection and reports its status.
      #name: "Self Clean"
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
    #disable_wifi_led: true # Optional. Disable Wifi LED on internal unit.
    #cdu_load:                    # Optional. Compressor load (%) - useful for estimating power consumption
    #  name: "Compressor Load"
    #cdu_iac:                     # Optional. Compressor current
    #  name: "Compressor Current"
    #cdu_td_temp:                 # Optional. CDU discharge pipe temperature
    #  name: "CDU Discharge Temp"
    #cdu_ts_temp:                 # Optional. CDU suction pipe temperature
    #  name: "CDU Suction Temp"
    #cdu_te_temp:                 # Optional. CDU evaporator temperature
    #  name: "CDU Evaporator Temp"
    #fcu_tc_temp:                 # Optional. FCU heat exchanger temperature
    #  name: "FCU Heat Exchanger Temp"
    #fcu_tcj_temp:                # Optional. FCU heat exchanger junction temperature
    #  name: "FCU Junction Temp"
    #fcu_fan_rpm:                 # Optional. FCU fan speed (RPM)
    #  name: "FCU Fan RPM"
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

## Filtering incorrect values (127 / 254 / 255)

The component automatically filters out incorrect sensor readings at the code level:
- Temperature readings of `127` (which the unit transmits when it cannot measure the temperature or when it is off) are ignored.
- Compressor load and current values of `254` or `255` are also ignored.

Therefore, you do not need to add manual filters in your YAML configuration for these values. However, if you need to filter out other values, you can still add ESPHome filters like this:

```yaml
    outdoor_temp:
      name: Outdoor Temp
      filters:
        - filter_out: 127
```

### Vertical air directions

Some units support fixed vertical air directions in addition to vertical swing. ESPHome's native climate swing modes are limited to `off`, `vertical`, `horizontal`, and `both`, so fixed positions are exposed as a Home Assistant select entity:

```yaml
    vertical_air_direction:
      name: "Vertical air direction"
```

In Toshiba service documentation this corresponds to the horizontal louver, which controls vertical air direction.

The select provides `Off`, `Swing`, `Top`, `Middle Top`, `Middle`, `Middle Bottom`, and `Bottom`.

### Self-cleaning status

Some Toshiba units run a post-shutdown self-cleaning cycle. During this cycle the indoor fan can continue running even though the climate entity is off. Configure the optional `self_clean` binary sensor to enable self-clean detection and expose the cycle in Home Assistant:

```yaml
    self_clean:
      name: "Self Clean"
```

The climate entity remains OFF while self-cleaning is active. ESPHome climate entities do not have a self-cleaning mode or action, so the binary sensor reports this separate operating state. Without `self_clean`, the component does not query self-clean status and retains its existing climate behavior.

How the cycle behaves (per the Toshiba service manual):

- It runs only after cooling or dry operation, and only if that ran for at least 10 minutes; it then runs for a fixed ~30 minutes. It does not run after heating, fan-only, or a short (<10 min) cooling/dry run.
- During the cycle the indoor fan runs at low speed to dry the unit; the compressor stays off. The unit reports itself as not operating, which is why the climate entity is OFF.
- The service manual documents enabling/disabling the self-clean cycle as a unit-side procedure (the indoor unit's `RESET` button together with a remote-control diagnosis code). This component reports the cycle but does not control it.

## Outdoor/Indoor unit diagnostics (ODU/IDU sensors)

Some Toshiba AC units periodically send extended status messages from the outdoor unit (ODU) and indoor unit (IDU). These messages contain diagnostic data such as compressor load, refrigerant temperatures, and fan speed. The component can parse these and expose them as optional sensors.

**Available sensors:**

| Sensor | Description | Unit |
|--------|-------------|------|
| `cdu_load` | Compressor load (frequency) | % |
| `cdu_iac` | Compressor current / EEV actuation | A |
| `cdu_td_temp` | CDU discharge pipe temperature | °C |
| `cdu_ts_temp` | CDU suction pipe temperature | °C |
| `cdu_te_temp` | CDU evaporator temperature | °C |
| `fcu_tc_temp` | FCU heat exchanger temperature | °C |
| `fcu_tcj_temp` | FCU heat exchanger junction temperature | °C |
| `fcu_fan_rpm` | FCU fan speed | RPM |

These sensors are all optional — only add the ones you need to your YAML configuration. The data arrives automatically from the unit (no polling required).

### Native Energy and Power monitoring

If your AC unit supports it, you can now get native energy consumption data. This requires adding a `time` component to your configuration so the component can sync the current time with the AC unit.

```yaml
time:
  - platform: homeassistant
    id: hass_time

climate:
  - platform: toshiba_suzumi
    # ...
    time_id: hass_time
    energy:
      name: "Daily Energy"
    power:
      name: "Realtime Power"
```

The `power` sensor provides a real-time estimate in Watts, calculated from the rate of change in the AC's internal energy counters.

### Estimating power consumption (Fallback)

For older units that do not support the native energy registers, you can still estimate power using `cdu_load`. `cdu_load` reports the compressor load as a percentage, which correlates with power usage. You can combine it with your unit's rated power input to estimate consumption in Home Assistant using a template sensor:

```yaml
sensor:
  - platform: template
    sensors:
      ac_estimated_power:
        friendly_name: "AC Estimated Power"
        unit_of_measurement: "W"
        value_template: "{{ (states('sensor.compressor_load') | float(0)) / 100 * 880 }}"
```

Replace `880` with your unit's rated power input in watts (check the datasheet). You can then use the [HA Riemann sum integral integration](https://www.home-assistant.io/integrations/integration/) to track energy consumption over time.

## Scan for unknown sensors

The code here was developed on certain Toshiba AC unit which provides only certain set of features. Newer or different units might offer more features (ie. horizontal swing etc.). While these are not implemented, you can add a button which scans for all sensors and prints the answers from AC unit. This might help developers to identify these new features.

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

## Links

https://www.espressif.com/en/products/devkits/esp32-devkitc/

https://github.com/toremick/shorai-esp32

https://github.com/Vpowgh/TConnect

Discord channel: https://discord.gg/wYYFawvqfr
