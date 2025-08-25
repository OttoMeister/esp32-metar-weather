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
https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display<br>

## Install PlatformIO on Linux (no IDE)
```
sudo apt install python3-venv build-essential git python3-intelhex
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo service udev restart
mkdir -p ~/.local/bin
export PATH=$PATH:$HOME/.local/bin
ln -s ~/.platformio/penv/bin/platformio ~/.local/bin/platformio
ln -s ~/.platformio/penv/bin/pio ~/.local/bin/pio
ln -s ~/.platformio/penv/bin/piodebuggdb ~/.local/bin/piodebuggdb
pio settings set enable_telemetry no
pio settings set check_platformio_interval 9999
~/.platformio/penv/bin/pip install intelhex
```
## Compile with PlatformIO on Linux
```
git clone https://github.com/OttoMeister/esp32-metar-weather
cd esp32-metar-weather/
pio pkg update -e esp32-8048S043C
pio run -t clean -e esp32-8048S043C
pio run -e esp32-8048S043C
pio run -e esp32-8048S043C --upload-port  /dev/ttyUSB0 -t upload
pio run -e esp32-8048S043C --monitor-port /dev/ttyUSB0 -t monitor
```
## Install PlatformIO on Win10-11 (no IDE)
Windows Command Prompt <br>
Install Python:
```
winget install Python.Python.3.13
REM Restart Windows Command Prompt 
python -V
python.exe -m pip install --upgrade pip
```
Install Git:
```
winget install git.git
REM restart Windows Command Prompt 
git --version
```
Install PlatformIO:
```
pip install platformio
pio --version
pio settings set enable_telemetry no
pio settings set check_platformio_interval 9999
```
Install CP210x driver:
```
cd %USERPROFILE%\Desktop
curl -L --fail -o cp210x.zip https://www.silabs.com/documents/public/software/CP210x_Windows_Drivers.zip --silent
mkdir cp210x
tar -xf cp210x.zip -C cp210x
start /wait cp210x\CP210xVCPInstaller_x64.exe /S
```
Install CH340 (WCH) driver:
```
cd %USERPROFILE%\Desktop
curl -L --fail -o CH341SER.exe https://www.wch.cn/download/file?id=65  --silent
start /wait CH341SER.exe /S
```
## Compile with PlatformIO on Win10-11
```
cd %USERPROFILE%\Desktop
git clone https://github.com/OttoMeister/esp32-metar-weather
cd esp32-metar-weather/
pio pkg update -e esp32-8048S043C
pio run -t clean -e esp32-8048S043C
pio run -e esp32-8048S043C
pio run -e esp32-8048S043C --upload-port  /dev/ttyUSB0 -t upload
pio run -e esp32-8048S043C --monitor-port /dev/ttyUSB0 -t monitor
```



