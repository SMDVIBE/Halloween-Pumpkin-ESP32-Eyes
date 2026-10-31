# GC9A01 pinout

| GC9A01 | ESP32-C3 Super Mini |
|---|---|
| BLK | 3V3 |
| CS | GPIO 7 |
| DC | GPIO 2 |
| RES / RST | GPIO 3 |
| SDA / MOSI | GPIO 6 |
| SCL / SCK | GPIO 4 |
| VCC | 3V3 |
| GND | GND |

Use the same wiring on both the MASTER and SLAVE boards. `SDA` is the SPI MOSI signal, not I2C SDA.
