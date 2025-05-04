# Platformio ESP32S3 8048S043C
Platformio esp32 Sunton 8048S043C ESP32S3 ST7262 800x400 Ak: cheap yellow display.
ESP32 development board-8M PSRAM 16M Flash, standard 4.3-inch TFT screen and with Capacitive touch.
<br><br>
Thanks to the efforts of these individuals and many others, programming on Suntown displays has become very easy. Here, I used the 8048S043C to create a small demo project featuring lvgl, touch, WiFi, METAR (Meteorological Aerodrome Report) weather data, and NTP time. The configuration is done via the touchscreen and stored in non-volatile memory.
<br><br>
https://github.com/rzeldent/esp32-smartdisplay<br>
https://github.com/rzeldent/platformio-espressif32-sunton<br>
https://github.com/lvgl/lv_port_esp32<br>
https://github.com/platformio/platformio-core<br>


## Install PlatformIO on Linux (no IDE)
```
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo service udev restart
export PATH=$PATH:$HOME/.local/bin
ln -s ~/.platformio/penv/bin/platformio ~/.local/bin/platformio
ln -s ~/.platformio/penv/bin/pio ~/.local/bin/pio
ln -s ~/.platformio/penv/bin/piodebuggdb ~/.local/bin/piodebuggdb
pio settings set enable_telemetry no
pio settings set check_platformio_interval 9999
```
## Compile with PlatformIO on Linux
```
git clone https://github.com/OttoMeister/esp32-metar-weather
cd esp32-metar-weather/
cd Platformio-ESP32-8048S043C
pluma src/main.cpp platformio.ini include/lv_conf.h esp32-8048S043C.json README.md &
platformio run -e esp32-8048S043C 
platformio run -e esp32-8048S043C --upload-port  /dev/ttyUSB0 -t upload
platformio run -e esp32-8048S043C --monitor-port /dev/ttyUSB0 -t monitor
```


