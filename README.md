# Dexcom Monitor

This is a simple monitor to display the current glucose level and trend through an ESP32 and a 2.8" TFT LCD display.

## Features

-   Display the current glucose level in mg/dL and mmol/L
-   Display the change from the previous reading
-   Display the trend (up, down, flat)
-   Display the time and date

## Requirements

-   ESP32
-   2.8" TFT LCD display
-   Jumper wires
-   Have an account on Dexcom Share

## Configuration

-   Dexcom Share Username and Password
-   WiFi SSID and Password

## Setup

1. Connect the ESP32 to the TFT LCD display
2. Connect the ESP32 to your computer via USB
3. Replace the `xxxx` in the code with your Dexcom Share Username and Password and WiFi SSID and Password
4. Upload the code to the ESP32
5. Open the Serial Monitor at 115200 baud to see the output

## Notes

-   The code is designed to run on an ESP32 microcontroller
-   The code is designed to display on an 2.8" TFT LCD display
