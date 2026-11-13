# Toshiba indoor unit UART protocol

This document describes the UART protocol between a Toshiba indoor unit (FCU) and a Wi-Fi
adapter, as far as it is known from public sources:

- the ECHONET Lite specification (the property layer uses the ECHONET Lite "home air
  conditioner" object, class `0x0130`),
- captures of the UART traffic made by users of this component and shared in issues and pull
  requests,
- the owner's manuals of the indoor units.

It also lists which parts this ESPHome component implements. Meanings marked *(observed)* come
from captures and are not covered by a public specification.

## Physical layer

9600 baud, 8 data bits, even parity, 1 stop bit, 5 V TTL on CN22 (see README for the pinout).

## Frame format

```
STX | FT(u16 BE) | CN | FN | DL(u16 BE) | FD[DL] | FCC
```

| Field | Size | Description |
|-------|------|-------------|
| STX | 1 | Always `0x02` |
| FT | 2 | Frame type |
| CN | 1 | Command number within the frame type |
| FN | 1 | Frame number; a reply carries the FN of its request |
| DL | 2 | Length of FD |
| FD | DL | Payload |
| FCC | 1 | Two's complement of the sum of all bytes except STX |

| FT | Role *(observed)* |
|----|------|
| `0xFFFF` | Link discovery / sync |
| `0x0000` | Session establish |
| `0x0001` | Session binding |
| `0x0002` | Unit identity |
| `0x0003` | Property traffic (ECHONET Lite properties) |

## Handshake

Captured on the wire:

| Step | Direction | Hex (abbreviated) | Meaning |
|------|-----------|-------------------|---------|
| 1 | → FCU | `02 FF FF 00 00 00 00 02` | Discovery probe |
| 1 | ← FCU | `02 FF FF 80 … 02 02 7C` | Present |
| 2 | → FCU | `02 FF FF 01 00 00 01 02 FE` | Sync |
| 2 | ← FCU | `02 FF FF 81 00 00 00 81` | ACK |
| 3 | → FCU | `02 00 00 00 00 00 02 02 02 FA` | Enter session |
| 3 | ← FCU | `02 00 00 80 … 00 00 7E` | Session OK |
| 3b | ← FCU | `02 00 01 01 01 00 02 00 01 FA` | Ask-back |
| 4 | → FCU | `02 00 01 81 01 00 02 00 00 7B` | Answer to ask-back |
| 5 | → FCU | `02 00 01 02 00 00 02 00 00 FE` | Finish session setup |
| 6 | → FCU | `02 00 02 00 00 00 00 FE` | Request unit identity |
| 6 | ← FCU | `02 00 02 80 …` (long frame) | Identity |
| 7 | → FCU | `02 00 02 01 00 00 02 00 00 FB` | Follow-up |
| 8 | → FCU | `02 00 02 02 00 00 02 00 00 FA` | Follow-up |

### Identity reply *(observed)*

| Absolute offset | Content |
|-----------------|---------|
| 171–174 | four unit identification bytes (`FD[0xA4..0xA7]`) |
| 180 | FCU model string length |
| 181… | FCU model ASCII, followed by the serial ASCII |

Example: `0F "RAS-13PKVSG E2 " "NO.01243D"`.

## Property frames (FT = 0x0003)

The payload carries one ECHONET Lite property of object `01 30 01` (home air conditioner,
instance 1). Layouts as seen in captures:

| Direction | CN | FD layout |
|-----------|----|-----------|
| Adapter → unit, Get / Set | `0x10` | `01 30 01 \| LEN(u16) \| EPC \| EDT…` |
| Unit → adapter, reply | `0x90` | `01 30 01 \| STATUS(u16) \| LEN(u16) \| EPC \| EDT…` |
| Unit → adapter, notify | `0x11` | `01 30 01 \| LEN(u16) \| EPC \| EDT…` |
| Adapter → unit, notify ACK | `0x91` | `00 00 01 30 01`, FN copied from the notify |

- `LEN` counts the EPC plus its data: a Get is `01 30 01 00 01 <EPC>`, a one-byte Set is
  `01 30 01 00 02 <EPC> <VALUE>`.
- `STATUS = 00 00` means OK. A Set reply echoes the EPC with no data.
- Example reply from a scan (issue #55): `02 00 03 90 00 00 0A 01 30 01 00 00 00 03 DB 00 00 53`
  = EPC `0xDB`, two data bytes `00 00`.
- The notify (CN `0x11`) is how the unit reports changes made with the IR remote.
- Units do not answer a Get for a property they do not have (scan in issue #55).

## EPC reference

### Standard ECHONET Lite properties

Names and value encodings from the ECHONET Lite specification (device object super class and
the home air conditioner class).

| EPC | ECHONET name | Data | Values |
|-----|--------------|------|--------|
| `0x80` | Operation status | 1 B | `0x30` ON, `0x31` OFF |
| `0x81` | Installation location | 1 B | |
| `0x82` | Standard version information | 4 B | `00 00 <release letter> 00` |
| `0x86` | Manufacturer's fault code | var. | byte 0 = size, bytes 1–3 = manufacturer code, then the code |
| `0x87` | Current limit setting | 1 B | percent; Toshiba units use 50, 75, 100 ("power level") |
| `0x88` | Fault status | 1 B | `0x41` fault, `0x42` no fault |
| `0x89` | Fault description | 2 B | |
| `0x90` / `0x94` | ON / OFF timer reservation | 1 B | `0x41` set, `0x42` not set |
| `0x92` / `0x96` | ON / OFF timer relative time | 2 B | hours, minutes |
| `0x9D` / `0x9E` / `0x9F` | Property maps | var. | not answered by the units seen so far |
| `0xA0` | Air flow rate setting | 1 B | `0x41` auto, `0x31`… levels |
| `0xA3` | Automatic swing of air flow | 1 B | `0x31` off, `0x41` vertical, `0x42` horizontal, `0x43` both |
| `0xB0` | Operation mode setting | 1 B | `0x41` auto, `0x42` cool, `0x43` heat, `0x44` dry, `0x45` fan |
| `0xB3` | Set temperature value | 1 B | °C (see 8 °C mode below) |
| `0xBB` | Measured room temperature | 1 B signed | `0x7F` = not available |
| `0xBE` | Measured outdoor air temperature | 1 B signed | `0x7F` = not available |
| `0xC7` | Air purifier function setting | 1 B | Toshiba values are vendor specific *(observed: `0x18`)* |
| `0xCB` | Self-cleaning function setting | 1 B | Toshiba status bits: `0x18` running, `0x10` idle *(observed)* |

Toshiba specifics *(observed)*: fan levels `0x31`–`0x36` (quiet, low, low-medium, medium,
medium-high, high); fixed louver positions on `0xA3`: `0x50`–`0x54` (top … bottom).

### Fault code (`0x88`, `0x86`)

`0x88` tells whether a fault is present. `0x86` holds the manufacturer's code; on Toshiba units
it has 9 bytes and the component reports the last byte. The mapping to the codes shown on the
remote (E01, F02, …) is not known.

### Toshiba-specific properties *(observed)*

ECHONET leaves `0xF0`–`0xFF` and several other codes to the manufacturer.

| EPC | Data | Meaning |
|-----|------|---------|
| `0xF7` | 1 B | special mode (merit), see below |
| `0xF8` | 4 B | Get returns mode, setpoint, fan, special mode in one frame (scan in issue #55: `43 15 41 20`) |
| `0xEE` | 2 B | feature mask of the unit; seen values: `7C 00` (Daiseikai 9), `6C 03` (Carrier 40WHH), `7C 03` (Carrier 40WHP) |
| `0xDE` / `0xDF` | 1 B | Wi-Fi LED on the indoor unit (`0xDE`: `0x05` on, `0x00` off; `0xDF`: `0x00` on, `0x80` off) |
| `0xD8`–`0xDB` | var. | energy today / week / month / year |
| `0xE0` | var. | unit information sent by the unit on its own (models, serial numbers, firmware) |
| `0xE4` | 8 B | indoor unit diagnostics: heat exchanger temperature, junction temperature, fan speed |
| `0xE5` | 8 B | outdoor unit diagnostics: discharge, suction, evaporator temperature, compressor load, compressor current |
| `0x98` | 7 B | unit clock |
| `0x99` | `LEN = 0x00EA` | weekly schedule; writing it also sets the unit clock |

### Special modes (`0xF7`)

| Value | Mode |
|-------|------|
| `0x00` | Standard |
| `0x01` | Hi POWER |
| `0x02` | Silent#1 |
| `0x03` | ECO |
| `0x04` | 8 °C |
| `0x05` | Sleep |
| `0x06` | Floor |
| `0x07` | Comfort |
| `0x0A` | Silent#2 |
| `0x20` | Fireplace 1 |
| `0x30` | Fireplace 2 |

### 8 °C mode

"8°C operation" is frost protection (heating only), described in the owner's manuals. The
setpoint range depends on the unit: Seiya (RAS-xxE2KVG) has an 8°C button and 5–13 °C; Haori
(RAS-Bxx N4KVRG) has no button and enters the mode on its own for heat setpoints of 5–16 °C.
While special mode `0x04` is active, `0xB3` carries the temperature + 16 (5–16 °C → 21–32).

### Energy (`0xD8`)

Unit clock (6 bytes), then 24 × u16 little-endian energy per hour (Wh). The values are bucketed
by the unit clock, so they read zero until the clock is set.

### Clock (`0x98`) and weekly schedule (`0x99`)

- `0x98`: `year-1900, month (0-11), day, hour, minute, second, weekday (Sunday = 0)`.
- `0x99`: the current date and time in the same encoding plus two zero bytes, then 7 days × 4
  programs × 8 bytes. Writing it with all programs set to `0xFF` sets the clock and clears
  any schedule stored in the unit.

Which frame a unit accepts differs. Captures from pull request #70 (Carrier multi-split with a
built-in adapter):

| Unit | `0xEE` | Frame used by the adapter | Response to the other frame |
|------|--------|---------------------------|-----------------------------|
| Carrier 40WHH (≈ Toshiba Shorai Edge) | `6C 03` | `0x99` | not tested |
| Carrier 40WHP (≈ Toshiba Haori) | `7C 03` | `0x98` | `0x99` is ignored: no ACK, no NAK |

```
→ 02 00 03 10 00 00 EF 01 30 01 00 EA 99 7E 0A 09 0E 01 32 01 00 00 FF×224 56
← 02 00 03 90 00 00 08 01 30 01 00 00 00 01 99 99
→ 02 00 03 10 00 00 0D 01 30 01 00 08 98 7E 0A 09 10 02 23 01 47
← 02 00 03 90 00 00 08 01 30 01 00 00 00 01 98 9A
```

## Coverage of this ESPHome component

| Feature | EPC | Status |
|---------|-----|--------|
| Power, mode, setpoint, fan, swing / fixed louver | `80 B0 B3 A0 A3` | implemented |
| Special modes | `F7`, `F8` | implemented |
| Power level | `87` | implemented |
| Room / outdoor temperature | `BB BE` | implemented |
| Ionizer (air purifier) | `C7` | implemented (`ionizer` switch) |
| Self-clean status | `CB` | implemented (read only) |
| OFF timer state and remaining time | `94 96` | implemented (read only) |
| Fault code | `88 86` | implemented (`error_code` text sensor) |
| Indoor / outdoor diagnostics | `E4 E5` | implemented |
| Daily energy and estimated power | `D8` | implemented |
| Unit clock | `99`, `98` | implemented: tries `0x99`, then alternates with `0x98` until one is ACKed |
| Wi-Fi LED | `DE DF` | implemented |
| Unit information | identity, `E0` | implemented (text sensors) |
| Feature detection | `EE`, identity, Get replies | implemented: entities and presets follow the unit (see README) |
| ON timer, setting timers | `90 92 94 96` | not implemented |
| Weekly schedule | `99` | not implemented (used only to set the clock) |
| Weekly / monthly / yearly energy | `D9`–`DB` | not implemented |

## References

- ECHONET Lite specification and "APPENDIX Detailed Requirements for ECHONET Device objects"
  (home air conditioner class `0x0130`): https://echonet.jp/spec_g/
- Owner's manuals: Toshiba Seiya RAS-xxE2KVG, Haori RAS-Bxx N4KVRG.
- Captures: issue #55 (scan of a Daiseikai 9), pull request #70 (Carrier 40WHH / 40WHP clock sync).
