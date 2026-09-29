# Conception-d-une-Systeme-Protype-du-Securite-de-Transport-de-Gaz
Bachelors degree Final Project

# Intelligent Gas Transport Safety Monitoring System

An embedded safety and monitoring system designed for dangerous gas transport vehicles. The system uses an Arduino Uno to continuously monitor environmental and operational parameters, detect potentially dangerous conditions, track the vehicle's geographical position, and activate local safety mechanisms when a critical situation is detected.

## Project Overview

The system combines multiple sensors and modules to provide real-time monitoring of a gas transport environment.

It monitors:

* Temperature
* Humidity
* Smoke/gas presence
* Flame/fire detection
* Pressure/load measurement
* Tank level estimation
* GPS position
* Vehicle speed
* System date and time

When dangerous conditions are detected, the system can activate:

* Audible alarm
* Warning LED
* Emergency warning indicators
* Pump/actuator
* Critical lockdown state

The system also provides local information through a 16×2 I2C LCD and diagnostic information through the Arduino Serial Monitor.

---

## System Architecture

```text
                         +----------------------+
                         |      Arduino Uno     |
                         |    Main Controller   |
                         +----------+-----------+
                                    |
          +-------------------------+-------------------------+
          |                         |                         |
          v                         v                         v
   Environmental               GPS Module              Pressure/Level
      Sensors                    Module                    Sensor
          |                         |                         |
    +-----+-----+                   |                         |
    |     |     |                   |                         |
    v     v     v                   v                         v
  DHT11  MQ-2  Flame              GPS                   HX711 + Load Cell
    |     |     |                   |                         |
    +-----+-----+-------------------+-------------------------+
                                    |
                                    v
                         +----------------------+
                         |   Safety Decision    |
                         |        Logic         |
                         +----------+-----------+
                                    |
                  +-----------------+-----------------+
                  |                 |                 |
                  v                 v                 v
               Buzzer        Warning LED           Pump
                  |                 |                 |
                  +-----------------+-----------------+
                                    |
                                    v
                           +----------------+
                           |  LCD Display   |
                           |  + Serial     |
                           |  Diagnostics   |
                           +----------------+
```

---

## Hardware Components

| Component               | Function                                 |
| ----------------------- | ---------------------------------------- |
| Arduino Uno             | Main microcontroller                     |
| DHT11                   | Temperature and humidity measurement     |
| MQ-2                    | Smoke/gas detection                      |
| Flame Sensor            | Fire/flame detection                     |
| HX711                   | Load-cell/pressure measurement interface |
| Load Cell / MPS20N0040D | Pressure/load measurement                |
| GPS Module              | Position, speed and time information     |
| 16×2 I2C LCD            | Local system display                     |
| Buzzer                  | Audible alarm                            |
| Warning LED             | Visual warning                           |
| Status LED              | System status indication                 |
| Pump/Actuator           | Emergency response mechanism             |

---

## Pin Configuration

The current implementation uses the following Arduino Uno pin assignments:

| Arduino Pin | Component    | Purpose               |
| ----------- | ------------ | --------------------- |
| D2          | Status LED   | System status         |
| D3          | HX711 DOUT   | Pressure/load data    |
| D4          | DHT11        | Temperature/humidity  |
| D5          | HX711 CLK    | Pressure/load clock   |
| D6          | Pump         | Emergency actuator    |
| D10         | GPS RX       | GPS communication     |
| D11         | GPS TX       | GPS communication     |
| D12         | Warning LED  | Emergency warning     |
| D13         | Buzzer       | Audible alarm         |
| A0          | MQ-2         | Smoke/gas measurement |
| A1          | Flame Sensor | Flame detection       |
| A4          | I2C SDA      | LCD communication     |
| A5          | I2C SCL      | LCD communication     |

The GPS module communicates using `SoftwareSerial` at 9600 baud.

The main Serial Monitor communication operates at 115200 baud.

---

## Software Libraries

The project uses the following Arduino libraries:

```cpp
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <DHT.h>
#include "HX711.h"
#include "GPS.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
```

### Required Libraries

Install the following libraries through the Arduino IDE Library Manager or provide them manually in the project:

* DHT sensor library
* HX711
* LiquidCrystal I2C
* SoftwareSerial
* Custom GPS library

---

## Configuration

### Sensor Thresholds

The current program defines the following safety thresholds:

```cpp
#define FACTEUR_CALIBRATION -7050.0

const float SEUIL_NIVEAU_NORMAL = 15.0;
const float SEUIL_NIVEAU_CRITIQUE = 40.0;
const int SEUIL_FUMEE_ACCIDENT = 750;
const float SEUIL_TEMP_ALERT = 45.0;
```

These values determine when the system transitions from normal monitoring to warning or critical operation.

> Important: Threshold values should be calibrated and validated against the actual sensors, vehicle installation, gas type, operating pressure and applicable safety requirements before deployment.

---

## Safety Logic

The system separates abnormal conditions into warning and critical states.

### Critical Condition

A critical incident is triggered when:

```cpp
if (zone < 2 || fumee > SEUIL_FUMEE_ACCIDENT)
```

When a critical condition occurs, the system:

1. Activates the pump.
2. Activates the buzzer.
3. Activates the warning LED intermittently.
4. Displays:

```text
!! LOCKDOWN !!
```

The system then enters a continuous critical state.

### Warning Condition

A system warning is generated when one of the following conditions is exceeded:

```cpp
temperature > SEUIL_TEMP_ALERT
```

or

```cpp
pression > SEUIL_NIVEAU_NORMAL
```

or

```cpp
niveau > SEUIL_NIVEAU_CRITIQUE
```

During a warning condition:

* The pump is switched off.
* The buzzer is switched off.
* The warning LED is activated.
* The LCD displays:

```text
SYSTEM WARNING!
```

---

## GPS Tracking

The GPS subsystem provides:

* Latitude
* Longitude
* Speed
* Distance travelled
* Date and time

GPS coordinates are converted into decimal degrees.

For example:

```text
Latitude:  -12.3456
Longitude:  28.1234
```

The program also generates a Google Maps-compatible location:

```text
maps.google.com/?q=latitude,longitude
```

If a valid GPS fix is unavailable, the system reports:

```text
No Fix
```

---

## Time Management

The system maintains an internal software clock using the Arduino `millis()` function.

When valid GPS date/time information becomes available, the system updates its internal clock.

The code also applies a +2 hour adjustment to the GPS time:

```cpp
gpsHeure += 2;
```

The displayed date/time follows the format:

```text
MM-DD HH:MM:SS
```

---

## LCD Display

The project uses a 16×2 I2C LCD with address:

```text
0x27
```

The display cycles through several pages.

### Page 1 — Date/Time

```text
MM-DD HH:MM:SS
```

### Page 2 — Environmental Data

```text
T:25.0 H:60
```

Where:

* `T` = Temperature
* `H` = Humidity

### Page 3 — GPS Position

```text
Lat:XX.XXXX
```

The display changes page approximately every 3 seconds.

---

## Serial Monitor

The Arduino sends diagnostic information through the Serial Monitor at:

```text
115200 baud
```

Example output:

```text
--- DIAGNOSTIC ARDUINO UNO ---
Heure : 06-23 21:52:00
Lat :   XX.XXXX
Lon :   XX.XXXX
Temp :  25.0
Fumee : 120
------------------------------
```

This is useful during:

* Sensor testing
* GPS debugging
* Calibration
* Hardware integration
* Troubleshooting

---

## Main Program Cycle

The main program continuously performs the following operations:

```text
Start
  |
  v
Initialize hardware
  |
  v
Initialize GPS
  |
  v
Initialize sensors
  |
  v
Initialize LCD
  |
  v
Read flame/smoke
  |
  v
Check critical conditions
  |
  +---- Critical ----> Emergency response / Lockdown
  |
  +---- Normal
          |
          v
      Read GPS
          |
          v
   Read temperature
   and humidity
          |
          v
   Read pressure/level
          |
          v
    Update LCD
          |
          v
   Serial diagnostics
          |
          v
        Repeat
```

Sensor data other than flame/smoke is processed approximately every 2 seconds.

---

## Status Indicators

### Normal Operation

The status LED continuously blinks at approximately 500 ms intervals.

### Warning

The warning LED remains activated.

### Critical Incident

The system activates:

* Pump
* Buzzer
* Warning LED

and displays the lockdown message on the LCD.

---

## Installation

### 1. Install Arduino IDE

Install the Arduino IDE and configure it for:

```text
Board: Arduino Uno
```

### 2. Install Required Libraries

Install:

```text
DHT sensor library
HX711
LiquidCrystal I2C
```

and add the project's custom GPS library.

### 3. Connect the Hardware

Connect the sensors according to the pin configuration table.

### 4. Configure Calibration

Adjust:

```cpp
#define FACTEUR_CALIBRATION -7050.0
```

according to the calibration of the pressure/load measurement system.

### 5. Upload the Program

Open the main `.ino` file in Arduino IDE and select:

```text
Tools → Board → Arduino Uno
Tools → Port → [Arduino Port]
```

Then click:

```text
Upload
```

### 6. Open Serial Monitor

Set the Serial Monitor to:

```text
115200 baud
```

---

## Testing

Testing should be performed progressively.

### Sensor Testing

Verify individually:

* DHT11 temperature/humidity
* MQ-2 smoke readings
* Flame sensor readings
* HX711 measurements
* GPS coordinates
* LCD output

### Alarm Testing

Test the warning and critical thresholds using controlled inputs.

For example:

```text
Normal
   |
   v
Temperature increase
   |
   v
Warning
   |
   v
Smoke detection
   |
   v
Critical condition
   |
   v
Alarm + actuator
```

### GPS Testing

Perform the GPS test outdoors or in an environment where the GPS antenna has a clear view of the sky.

---

## Important Safety Considerations

This project is an embedded-system prototype and should not be considered a certified safety system without appropriate engineering validation.

For deployment on a real gas transport vehicle, the following should be addressed:

* Sensor calibration
* Sensor redundancy
* Fail-safe design
* Electrical isolation
* Intrinsically safe equipment where required
* Explosion-proof equipment where applicable
* Emergency-stop design
* Power-failure behavior
* Watchdog and fault recovery
* Environmental protection
* Electromagnetic compatibility
* Gas-specific detection thresholds
* Applicable transportation and hazardous-area regulations

The pump, buzzer and other actuators should not be connected directly to Arduino GPIO pins if their electrical characteristics exceed the Arduino's output limits. Appropriate driver circuitry, relays, MOSFETs or isolation should be used.

---

## Suggested Project Structure

```text
Gas-Transport-Safety-System/
|
├── README.md
├── Gas_Transport_Safety/
│   ├── Gas_Transport_Safety.ino
│   ├── GPS.cpp
│   └── GPS.h
|
├── docs/
│   ├── system_architecture.png
│   ├── wiring_diagram.png
│   └── project_report.pdf
|
├── hardware/
│   ├── schematic/
│   └── pcb/
|
└── tests/
    └── sensor_tests/
```

---

## Project Objectives

The main objectives of the project are to:

1. Monitor the operating environment of a gas transport system.
2. Detect abnormal temperature, smoke, flame and pressure conditions.
3. Track the geographical position of the vehicle.
4. Provide immediate local warnings.
5. Activate an emergency actuator during critical conditions.
6. Provide diagnostic information for operators and developers.
7. Establish a foundation for a more advanced connected vehicle safety system.

---

## Possible Future Improvements

The system can be extended with:

* ESP32-based connectivity
* GSM/4G communication
* Wi-Fi telemetry
* Cloud monitoring
* Remote emergency notifications
* Telegram/SMS alerts
* SD-card data logging
* Real-time web dashboard
* Camera integration
* Multiple gas sensors
* Pressure sensor redundancy
* Battery monitoring
* CAN bus integration
* Remote emergency shutdown
* OTA firmware updates
* Real-time GPS tracking
* Historical incident analysis

---

## Project Context

This project was developed as an embedded intelligent safety system for dangerous-gas transportation.

The design combines embedded programming, sensor acquisition, GPS tracking, real-time decision logic and actuator control to create a prototype safety-monitoring platform.

---

## License

Add an appropriate license before publishing the project publicly.

For example:

```text
MIT License
```

or another license appropriate for your academic or research project.

---

## Contributing

Contributions and improvements are welcome.

For major changes, please document:

* Hardware modifications
* New sensors
* Changes to safety thresholds
* Changes to the alarm logic
* Software dependencies
* Testing procedures

---

## Acknowledgements

Developed as an embedded systems and electrical engineering project focused on improving safety monitoring for dangerous-gas transportation.
