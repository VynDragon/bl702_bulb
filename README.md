## OpenThread Firmware for BL70C10 Aliexpress bulb

It is based on the lighting-app matter example, as BL702 is incapable of supporting the all-devices-app firmware.

[image]: https://github.com/VynDragon/bl702_bulb/raw/master/image.png "Image"

### Requirements

- A Thread border router on your network.
- A Matter controller. The firmware uses Matter test credentials (vendor `0xFFF1`), so controllers treat it as uncertified:

| Controller | What to do |
|---|---|
| Home Assistant | Enable "Test DCL" in the Matter server settings |
| Apple Home | Accept the "uncertified accessory" prompt |
| Google Home | Needs a developer project for the vendor and product ID; pairing through Home Assistant is easier |

### Flashing

```bflb-mcu-tool-uart --chipname bl702 --firmware <file>.bin```

### Building

It needs ZAP_INSTALL_PATH set to the ZAP install, otherwise it requires only standard `west` usage.

```west build -p -c -b bl702_bulb bl702_bulb/app```

### Pairing

Power the bulb on and add it in your controller within 15 minutes, using Bluetooth.

- Manual pairing code: `34970112332`
- The QR code ID can be used to generate a QR code: `6FCJ142C00KA0648G00`

Both pairing identifiers are fixed, and bluetooth turns off once the bulb has joined Thread.
It will breath Green before pairing has started, and breath Red when connected to Thread
but uncomissioned, then take the color settings.

To factory Reset it, switch on and off 3 times, being careful to let it boot (turn on) 
while waiting less than 5 seconds to turn it off.

### Limits

- Up to 3 controllers (fabrics) at once.
- No over-the-air updates: new firmware is flashed over UART.

### Development

To use zap editor:

```$ZAP_INSTALL_PATH/zap --zcl modules/lib/matter/src/app/zap-templates/zcl/zcl.json --gen modules/lib/matter/src/app/zap-templates/app-templates.json```

**THEN** open `bl702_bulb/app/lighting-app.zap`


Regenerating matter file:

```./modules/lib/matter/scripts/tools/zap/generate.py bl702_bulb/app/lighting-app.zap```
