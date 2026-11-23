# DeepRoot

[![Latest release](https://img.shields.io/github/v/release/MacDumi/DeepRoot?label=release)](https://github.com/MacDumi/DeepRoot/releases/latest)

Replacement firmware for the [Seeed XIAO Soil Moisture Sensor](https://www.seeedstudio.com/XIAO-Soil-Sensor-p-6452.html)
(XIAO ESP32-C6). It turns the stock ESPHome / Wi-Fi sensor into a
**Matter soil sensor that talks over Thread** and appears directly in a Matter
controller such as Home Assistant, with no ESPHome integration and no Wi-Fi.

Thread is a low-power mesh radio, so the sensor can sleep between readings and
stay connected, which Wi-Fi can't do on a battery.

> **Before you start:** you need a **Thread border router** and a controller
> that supports the **Matter 1.5 Soil Sensor** device type. Read
> [Requirements](#requirements) and [Limitations](#limitations) first.
>
> **Tested with:** Home Assistant (Matter integration) and the OpenThread Border
> Router only. Other controllers and border routers should work in principle,
> but they have not been tried.

**Download the firmware:** [GitHub Releases](https://github.com/MacDumi/DeepRoot/releases)

---

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Limitations](#limitations)
- [Getting the firmware](#getting-the-firmware)
- [Flashing (converting from ESPHome)](#flashing-converting-from-esphome)
- [Commissioning (adding it to your controller)](#commissioning-adding-it-to-your-controller)
- [Using the sensor](#using-the-sensor)
- [Updating the firmware](#updating-the-firmware)
- [Going back to ESPHome](#going-back-to-esphome)
- [Troubleshooting](#troubleshooting)
- [Building from source and customising](#building-from-source-and-customising)

---

## Features

**What a Matter controller sees**

| Item | Details |
|---|---|
| Device type | Matter 1.5 **Soil Sensor** (0x0045) |
| Soil moisture | 0-100 %, or *unknown* when the probe reading can't be trusted (probe disconnected, reading far outside the calibrated range) |
| Battery | Percentage remaining, plus a charge level (*OK* / *Warning* at 35 % or below / *Critical* at 20 % or below) and a *replacement needed* flag at Critical |
| Identify | The LEDs show a distinctive pattern so you can tell which sensor is which |
| Device info | Vendor `DIY`, product `Soil Sensor`, hardware `XIAO ESP32-C6`, serial number `SOIL-` + the chip's factory MAC (unique per board) |
| Firmware updates | Matter OTA (needs an OTA provider, see [Updating](#updating-the-firmware)) |

**On the device**

- **Adaptive measurement schedule:** measures less often when the soil is wet
  and more often as it dries out (details under [Readings](#readings)).
- **Only reports real changes:** small fluctuations are not sent, which saves
  radio time.
- **Button:** check moisture on the spot, calibrate the probe, or factory reset.
- **Three LEDs (red, yellow, green):** show moisture, calibration progress and
  Identify.
- **On-device calibration:** no tools or apps needed, and the calibration
  survives a factory reset.
- **Low-power operation:** the sensor registers as a Matter *Intermittently
  Connected Device* (ICD, Long Idle Time) and a Thread *sleepy end device*. The
  radio wakes briefly to poll its Thread parent, and the chip light-sleeps the
  rest of the time.
- **Bluetooth only for pairing:** once the sensor is commissioned, Bluetooth
  stays off on every boot.

---

## Requirements

1. **The sensor:** a Seeed XIAO Soil Moisture Sensor (XIAO ESP32-C6). The firmware
   assumes this pin-out:

   | Function | Pin |
   |---|---|
   | Battery sense | GPIO0 (A0/D0) |
   | Soil probe output | GPIO1 (A1/D1) |
   | User button | GPIO2 (A2/D2) |
   | Probe excitation | GPIO21 (D3) |
   | Red / green / yellow LED | GPIO20 / GPIO19 / GPIO18 |
   | RF switch enable / antenna select | GPIO3 / GPIO14 |

2. **A Thread border router** on the same home network as your controller.
   **Tested:** Home Assistant with the OpenThread Border Router. Untested but
   expected to work: other Thread border routers such as an Apple TV 4K /
   HomePod mini or a Google Nest Hub (2nd gen) / Nest Wifi Pro. The sensor also
   needs a Thread router within radio range. That can be the border router
   itself or any mains-powered Thread device.

3. **A Matter controller that supports the Soil Sensor device type**
   (introduced in Matter 1.5). **Tested:** Home Assistant's Matter integration.
   Older controllers may refuse the device or show it without a moisture value.

4. **A phone with Bluetooth** for commissioning (or a bluetooth-enabled
   Home Assistant instance). The phone must have your Thread network's credentials.
   In Home Assistant, sync them from the Companion app. Apple and Google ecosystems
   sync them on their own.

5. **For flashing:** a USB-C data cable and either
   [`esptool`](https://docs.espressif.com/projects/esptool/) (`pip install esptool`)
   or a Chromium-based browser (Chrome, Edge) for the web flasher.

---

## Limitations

Please read these before converting a sensor.

- **No Wi-Fi, no ESPHome features.** There is no web page, no ESPHome API, no
  ESPHome OTA and no Wi-Fi fallback. Everything goes through Matter over Thread.
  To go back you must reflash over USB.
- **Thread border router required.** Without one the sensor can't reach your
  network.
- **Only tested with Home Assistant and the OpenThread Border Router.** Apple
  Home, Google Home, SmartThings and their border routers have not been tried.
- **Development certificates and a shared pairing code.** Every unit uses the
  Matter test vendor ID (0xFFF1 / 0x8000) and the **same** setup code
  (`20202021`, discriminator `3840`). Controllers will warn about an
  *uncertified* device, and some need developer options enabled
  (see [Commissioning](#commissioning-adding-it-to-your-controller)). Because
  every unit shares one code, **commission units one at a time**, and be aware
  that anyone nearby could pair a sensor that hasn't been paired yet.
- **Controller support for soil sensors is new.** Matter 1.5 is recent. Check
  that your controller lists *Soil Sensor* / soil moisture support.
- **Commands from the controller are delayed.** To save power the sensor only
  checks for incoming messages every **2 minutes**, or every **15 s** if your
  controller doesn't register for Long Idle Time check-ins. Identify, reads and
  other controller-initiated actions can take that long to arrive. **Press the
  button once** to make the sensor responsive for a few seconds.
- **Readings are infrequent by design:** every 8 hours while the soil is wet
  (see [Readings](#readings)). Press the button for an immediate reading.
- **An unpaired sensor drains its battery.** Until it is commissioned it keeps
  Bluetooth advertising running indefinitely, reopening the pairing window
  every 15 minutes. Commission it soon after inserting batteries, or leave the
  batteries out until you're ready.
- **The battery percentage assumes alkaline AA cells.** It is based on an
  alkaline discharge curve (1.55 V = 100 %, 1.10 V = 0 %). Rechargeable NiMH
  cells (about 1.2 V) will read low.
- **OTA rollback needs a recent USB flash.** A failed OTA update rolls back
  automatically (see [Updating](#over-the-air-matter-ota-keeps-pairing-and-calibration)),
  but only on sensors whose bootloader supports it. The bootloader is never
  replaced by OTA, so a sensor flashed with an older release needs one USB
  flash of a current `_full.bin` to get it.

---

## Getting the firmware

Download the latest release from
**[GitHub Releases](https://github.com/MacDumi/DeepRoot/releases)**.
Each release has two files (`<N>` is the firmware version number):

| File | Use |
|---|---|
| `matter_soil_sensor_v<N>_full.bin` | Complete flash image. Use this to convert a sensor from ESPHome over USB. |
| `matter_soil_sensor_v<N>.ota` | Matter OTA update image for a sensor already running this firmware |

To change settings, see
[Building from source](#building-from-source-and-customising).

---

## Flashing (converting from ESPHome)

There are two ways to flash: [esptool](#option-a-esptool) on the command line,
or the [web flasher](#option-b-web-flasher) in a browser. Both need the board
in download mode first.

### 1. Put the board in download mode

Connect the XIAO to your computer with a USB-C data cable. The stock ESPHome
firmware may be in deep sleep, in which case the USB port won't appear. In
that case:

1. Hold the **BOOT** button on the XIAO module.
2. While holding it, plug in USB (or press and release **RESET**).
3. Release **BOOT**.

### Option A: esptool

Install it with `pip install esptool`. The commands use **esptool 5.x**. On
esptool 4.x replace the dashes with underscores (`chip_id`, `read_flash`,
`erase_flash`, `write_flash`). The port is `/dev/ttyACM0` on Linux,
`/dev/cu.usbmodem*` on macOS and `COMx` on Windows.

1. **Check the connection:**

   ```bash
   esptool -p /dev/ttyACM0 chip-id
   ```

2. **Back up the ESPHome firmware (recommended).** Keep this file to
   [go back](#going-back-to-esphome) later.

   ```bash
   esptool --chip esp32c6 -p /dev/ttyACM0 read-flash 0 0x400000 esphome-backup.bin
   ```

3. **Erase and flash:**

   ```bash
   esptool --chip esp32c6 -p /dev/ttyACM0 erase-flash
   esptool --chip esp32c6 -p /dev/ttyACM0 write-flash 0x0 matter_soil_sensor_v<N>_full.bin
   ```

### Option B: web flasher

Espressif's browser-based flasher, [esptool-js](https://espressif.github.io/esptool-js/),
needs Chrome, Edge or another Chromium-based browser (it uses Web Serial).
It cannot make a backup, so use esptool for that if you want one.

1. Open <https://espressif.github.io/esptool-js/> and click **Connect**. Pick
   the XIAO's serial port.
2. Click **Erase Flash** and wait for it to finish.
3. In the programming section set the **Flash Address** to `0x0`, choose
   `matter_soil_sensor_v<N>_full.bin`, and click **Program**.
4. Click **Disconnect**.

Erasing (with either option) removes ESPHome's stored settings, such as Wi-Fi
credentials, so the new firmware starts clean.

### 2. Reset and (optionally) watch the log

Press **RESET** or unplug and replug. To see the boot log and the pairing codes,
open a serial monitor at 115200 baud, for example:

```bash
python -m serial.tools.miniterm /dev/ttyACM0 115200    # pyserial comes with esptool
```

or use the **Console** section of the web flasher.

The log over USB may stop or garble once the sensor starts light-sleeping.
That is normal and doesn't affect operation. The pairing codes are printed at
boot.

---

## Commissioning (adding it to your controller)

### Pairing codes

All units share the Matter test credentials:

| | |
|---|---|
| QR code payload | `MT:Y.K9042C00KA0648G00` |
| Manual pairing code | `3497-011-2332` |
| Setup passcode / discriminator | `20202021` / `3840` |

The boot log prints the same codes, with a link that renders the QR code. You
can also turn the `MT:` string into a QR code with any QR generator.

### Steps

#### Home Assistant (tested)

Tested with Home Assistant's **Matter** integration and the **OpenThread Border
Router**.

1. Make sure the Matter integration and the OpenThread Border Router are set up
   and running.
2. The Matter Server must accept **development certificates**, because this
   firmware uses the Matter test vendor ID. Enable the test-net DCL option in
   the Matter Server's configuration.
3. On your phone, sync the Thread credentials from the Home Assistant Companion
   app, so the phone can hand them to the sensor. Keep Bluetooth on.
4. Power the sensor (batteries or USB). An unpaired sensor starts Bluetooth
   advertising on its own. No button press is needed.
5. In the Companion app go to **Settings → Devices & services → Add device →
   Add Matter device**, then enter the manual code. Keep the phone close to the sensor.
   Alternatively, when using a bluetooth-enabled Home Assistant instance, go to
   **Matter Server → Commision Nonde → Thread device** and follow the instructions.
6. The sensor receives the Thread credentials, joins Thread, and is added. When
   commissioning succeeds, Bluetooth switches off and stays off on later boots.

#### Other controllers (untested)

The same flow should work with other Matter controllers that support Matter
1.5 soil sensors: choose *Add device → Matter* and scan the code. Expect an
**uncertified device** warning. Reportedly:

- **Apple Home** shows an "Uncertified Accessory" prompt with an *Add Anyway*
  option.
- **Google Home** needs a developer project for VID `0xFFF1` / PID `0x8000` in
  the Google Home Developer Console.

These notes are untested; check the controller's documentation if pairing
stops at the certificate check.

The first moisture and battery values are the ones measured at boot, so they
appear straight away.

### Adding it to a second controller (multi-admin)

Use the first controller's *share* / *pair with another app* feature. That
opens a pairing window over the network (Bluetooth is off once paired), and
the second controller pairs using the code it shows you. The sensor supports
one Long Idle Time check-in client per controller.

### Removing it

- **Remove it from the controller.** When the last controller removes it, the
  sensor restarts on its own and goes back into pairing mode.
- **Or factory reset** with the button (see [Button](#button)).

Both keep the probe calibration.

---

## Using the sensor

### Readings

The sensor measures on a schedule that depends on how wet the soil is:

| Soil moisture | Measured every |
|---|---|
| above 40 % | 8 hours |
| 40 % or below (*almost dry*) | 2 hours |
| 25 % or below (*dry*) | 30 minutes |
| unknown | 2 hours |

- It moves to a faster schedule as soon as a reading crosses a threshold.
  Moving back to a slower one needs the reading to be 3 points clear of the
  threshold, so the schedule doesn't flip-flop.
- Every reading restarts the clock, including one triggered by the button. So
  after watering, a button press moves it straight back to the 8-hour schedule.
- A new moisture value is only sent if it differs from the last reported one by
  **3 points or more**, or changes to or from *unknown*.
- Battery is checked at boot and then about every 8 hours. A new value is sent
  if it changes by **5 points or more** or crosses the Warning or Critical level.
- The controller also receives a keep-alive report from the sensor about once an hour.

### Button

| Gesture | Action |
|---|---|
| **Single short press** | Takes a moisture reading immediately and reports it. Shows the result on the LEDs for 1 s. Wakes the sensor for a few seconds so pending controller actions arrive quickly. |
| **Three short presses** (each under 1 s, gaps under 0.5 s) | Starts [probe calibration](#calibrating-the-probe) |
| **Hold for 5 s** | **Factory reset:** removes all controllers and Thread credentials, then restarts into pairing mode. The probe calibration is kept. |

A single press is recognised half a second after release (once it's clear no
second press follows), and the reading then takes about a second.

### LEDs

| Pattern | Meaning |
|---|---|
| Red, solid 1 s | Moisture reading: **dry** (25 % or below) |
| Yellow, solid 1 s | Moisture reading: **almost dry** (26-40 %) |
| Green, solid 1 s | Moisture reading: **OK** (above 40 %) |
| *(nothing after a press)* | Reading unknown. Check the probe or calibrate. |
| Red flashing | Calibration: insert probe in **dry** soil |
| Green flashing | Calibration: insert probe in **wet** soil |
| Two quick green flashes | Calibration saved |
| Two quick red flashes | Calibration rejected; previous one kept |
| Red → yellow → green cycling | **Identify** requested by a controller |

The LEDs are otherwise off to save power. There is no LED indication for
pairing, connection state or factory reset.

### Calibrating the probe

The firmware ships with generic values (dry 2600 mV, wet 1100 mV). Calibrate
each sensor in the soil you actually use for accurate percentages:

1. Prepare some **completely dry** soil and some **fully wet** soil.
2. **Press the button three times** quickly.
3. **Red flashes for 10 s:** put the probe into the dry soil up to its depth
   mark. Keep it still.
4. Red stops. After 3 s of settling the dry reading is taken.
5. **Green flashes for 10 s:** move the probe into the wet soil.
6. Green stops. After 3 s the wet reading is taken.
7. **Two green flashes:** calibration saved, and a fresh reading is reported
   straight away with it. **Two red flashes:** rejected, usually because the
   steps were swapped or both readings were too similar (they must differ by
   at least 200 mV). The previous calibration stays in use.

Calibration is stored on the sensor and survives factory resets, OTA updates
and application-only USB updates. Flashing the `_full.bin` image or erasing the
flash removes it. Readings more than 400 mV outside
the calibrated range are reported as *unknown*.

### Identify

When a controller asks the sensor to identify itself (usually during setup), the
LEDs cycle red → yellow → green → off. Identify takes priority over the
moisture display, and calibration takes priority over both. To save battery the
LEDs stop after 30 s however long the controller asked for.

If the sensor is idle, the request can take up to 2 minutes to arrive (see
[Limitations](#limitations)). Press the button once first, then use Identify in
the app.

### Battery life tips

- Commission the sensor soon after powering it, since an unpaired sensor keeps
  Bluetooth running.
- Battery use is much lower if your controller registers for **Long Idle Time
  check-ins**. The sensor then polls every 2 minutes instead of every 15 s.
  Whether this happens depends on the controller, not on the sensor. You can see
  the current mode in the ICD Management cluster (`OperatingMode`, endpoint 0)
  using a Matter developer tool.
- Keep the sensor within good range of a Thread router. Weak links cost
  retransmissions.
- Don't judge battery life while USB is connected.

---

## Updating the firmware

### Over the air (Matter OTA), keeps pairing and calibration

The sensor checks for updates through Matter. Download the `.ota` file from
[Releases](https://github.com/MacDumi/DeepRoot/releases); you also need an
**OTA provider** to serve it.

An update is only offered if its version number is **higher** than the one on
the sensor. The sensor restarts into the new firmware and keeps its pairing and
calibration.

**Automatic rollback.** New firmware starts on trial:

- It is kept once the sensor has stayed connected to Thread for 1 minute.
- If it crashes or restarts before then, or isn't connected within
  15 minutes, the sensor goes back to the previous firmware on its own.
- If your Thread network happens to be down during those 15 minutes, a good
  update is rolled back too. Your OTA provider will offer it again later.

Rollback needs a bootloader that supports it; see [Limitations](#limitations).

### Over USB, starts fresh

Flash the new `_full.bin` exactly as in
[Flashing](#flashing-converting-from-esphome). The full image overwrites the
stored data, so pairing **and calibration** are lost: remove the sensor from
your controller, then commission and calibrate it again.

Because the sensor sleeps, the USB port may not respond. Use the BOOT/RESET
sequence from [step 1](#1-put-the-board-in-download-mode).

---

## Going back to ESPHome

1. Remove the sensor from your Matter controller.
2. Put the board in [download mode](#1-put-the-board-in-download-mode).
3. Restore your backup, either with esptool:

   ```bash
   esptool --chip esp32c6 -p /dev/ttyACM0 write-flash 0x0 esphome-backup.bin
   ```

   or with the [web flasher](https://espressif.github.io/esptool-js/): Flash
   Address `0x0`, file `esphome-backup.bin`. Without a backup, install a fresh
   ESPHome build with the ESPHome tools.

---

## Troubleshooting

| Symptom | Things to check |
|---|---|
| USB port doesn't appear | Use a data cable (not charge-only). Use the BOOT/RESET sequence to force download mode. |
| Web flasher shows no port | Use Chrome or Edge. Close any other program (serial monitor, esptool) that has the port open. |
| Controller can't find the sensor | It must be unpaired. Paired sensors keep Bluetooth off; factory reset it (hold button 5 s). Keep the phone close. Make sure no other unpaired unit is advertising, since they share one code. |
| Pairing stops at the certificate check | Enable development/test certificates in your controller (see [Steps](#steps)). |
| Pairing stops while joining the network | Check the Thread border router is online and the phone has the Thread credentials. |
| Moisture shows *unknown* | Check the probe is fully inserted and connected. Calibrate it. |
| Moisture value seems wrong or never reaches 0 / 100 % | Calibrate it in your own soil. |
| Battery % low with fresh rechargeable cells | Expected: the curve is for alkaline cells. |
| Controller actions are slow | Normal for a sleepy device; press the button once first. |
| Device drops off the network | Check distance to the nearest Thread router and which antenna is selected. |
| Serial log stops after a while | Normal: the USB log pauses while the chip sleeps. |
| Sensor keeps resetting / loses pairing | The button may be stuck or wet. |

---

## Building from source and customising

Only needed if you want to change a setting. The release images use the
defaults below.

### Settings

Most settings live in [`main/app_config.h`](main/app_config.h).

| Setting | Default | What it does |
|---|---|---|
| `main/antenna.cpp`, level written to `kAntennaSelectGpio` | `1`: external (u.FL) | Write `0` to use the on-board antenna |
| `radio::kThreadTxPowerDbm` | 8 dBm | Thread transmit power |
| `measure::kDryPercent` / `kAlmostDryPercent` | 25 / 40 % | Thresholds for the measurement schedule and LED colours |
| `measure::kIntervalNormal` / `AlmostDry` / `Dry` | 8 h / 2 h / 30 min | Measurement intervals |
| `measure::kMoistureDeadbandPercent` | 3 | Minimum change before a new moisture value is reported |
| `icd::kIcdSlowPollMs` | 120 s | How often the sensor checks for messages when idle (longer saves battery but delays controller actions) |
| `icd::kIcdIdleModeSec` | 1 h | Check-in / keep-alive interval |
| `battery::kCurve`, `kBatteryWarningPercent`, `kBatteryCriticalPercent` | alkaline AA, 35 / 20 % | Battery percentage curve and alert levels |
| `button::kLongHoldMs` | 5 s | Factory-reset hold time |
| `display::kIdentifyMaxMs` | 30 s | Maximum Identify LED time |

The firmware version is set in `sdkconfig.defaults`
(`CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER`, `CONFIG_APP_PROJECT_VER`). Raise
both for an OTA update to be offered.

### Building

You need Python 3 and either [PlatformIO](https://platformio.org/) or ESP-IDF
**5.4.x or 5.5.x** (6.0 does **not** work with esp-matter).

```bash
git clone https://github.com/MacDumi/DeepRoot.git && cd DeepRoot
make full                  # builds with PlatformIO, then writes the images to dist/
# or
make full BACKEND=idf      # builds with ESP-IDF (sources ~/esp/esp-idf-v5.4.1/export.sh,
                           # override with IDF_EXPORT=/path/to/export.sh)
```

The first build downloads the Matter SDK and takes a while. The output in
`dist/` has the same two files as a release, and the OTA image's vendor ID,
product ID and version are taken from the build automatically.

- If you change `sdkconfig.defaults`, **delete `sdkconfig.xiao_soil_sensor`**
  (PlatformIO) or `sdkconfig` (ESP-IDF) before rebuilding. Otherwise the old
  generated configuration silently wins. `make` warns if the version numbers
  disagree.
- To update a sensor over USB **without** losing pairing and calibration, use
  `pio run -t upload` (or `idf.py flash`). These write the bootloader, partition
  table and application but leave the stored data alone.
