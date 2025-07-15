# Flick-Out!! 🥊

[![Arduino](https://img.shields.io/badge/Arduino-IDE-blue.svg)](https://www.arduino.cc/)
[![ESP32](https://img.shields.io/badge/ESP32-S3-green.svg)](https://www.espressif.com/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[![Flick-Out Game](images/Render_01.jpg)](images/Render_01.jpg)

| ![Turn Animation](images/Turn.gif) | ![Ball Animation](images/Ball.gif) | ![Flick Animation](images/Flick.gif) |
|:---:|:---:|:---:|

## 🎥 Demo Video

[![Watch the demo](https://img.youtube.com/vi/mopjpVoV8_0/maxresdefault.jpg)](https://www.youtube.com/watch?v=mopjpVoV8_0)

*Click to watch the full gameplay demonstration*

> A retro-style arcade punching game for the LilyGO T-Display S3, featuring animated GIFs, sound effects, and force-sensitive gameplay.

## 🎮 Features

- 🎬 **Animated GIF Support** - Boot animations, idle loops, and game sequences
- 👊 **Force-Sensitive Gameplay** - Punch strength detection using FSR sensor
- 🔊 **Sound System** - MP3 audio with DFPlayer Mini module
- 💡 **NeoPixel Effects** - Dynamic LED lighting synchronized with game states
- 🔋 **Battery Monitoring** - Real-time voltage tracking with low battery warnings
- 🏆 **Highscore System** - Persistent storage with reset functionality
- 🎵 **Volume Control** - Adjustable audio levels with visual menu
- ✨ **Attract Modes** - Cycling highscore display and scrolling credits


## 📋 Hardware Requirements

### Main Components

| Component | Description |
|-----------|-------------|
| **LilyGO T-Display S3** | ESP32-S3 development board with integrated display |
| **DFPlayer Mini** | MP3 module for audio playback |
| **Force Sensitive Resistor (FSR)** | Punch detection sensor |
| **NeoPixel Stick** | 8 LEDs for visual effects |
| **MicroSD Card** | Storage for audio files (32GB max) |
| **LiPo Battery** | Power source with JST PH 1mm connector (JST plug included with T-Display S3) |

### 🔌 Pin Configuration

```
FSR Sensor:        GPIO 12
Boot Button:       GPIO 0 (built-in)
External Button:   GPIO 17 (with internal pullup)
Battery Monitor:   GPIO 4
DFPlayer RX:       GPIO 21
DFPlayer TX:       GPIO 16
NeoPixel Stick:    GPIO 11 (8 LEDs)
Display Backlight: GPIO 38
Display Power:     GPIO 15
```

## ⚡ Circuit Assembly

[![Circuit Wiring](images/Circuit.jpg)](images/Circuit.jpg)

Follow the pin configuration above to connect all components. The circuit shows the complete wiring setup for the T-Display S3, DFPlayer Mini, FSR sensor, and NeoPixel stick.

## 🖨️ 3D Printed Parts

The game includes a custom 3D printed enclosure and mounting system. All STL files and printing instructions are available on MakerWorld:

**[Download 3D Files on MakerWorld](https://makerworld.com/your-project-link)**

The 3D printed parts include:
- Main enclosure for T-Display S3
- Mounting bracket for FSR sensor
- NeoPixel stick holder
- Battery compartment

> 💡 **Printing Tips**: Use PLA+ filament for best results. No supports needed for most parts.

## 🛠️ Installation

### Board Settings
[![Circuit Wiring](images/BoardSettings.png)](images/BoardSettings.png)

### Prerequisites

- Arduino IDE 2.x (required)
- ESP32 Board Package
- [T-Display S3 Environment Setup](https://github.com/Xinyuan-LilyGO/T-Display-S3) - Follow the installation guide
- Required libraries (see below)

### Required Libraries

Install through Arduino IDE Library Manager:

- **[Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX)** - Display graphics
- **[DFRobotDFPlayerMini](https://github.com/DFRobot/DFRobotDFPlayerMini)** - Audio control  
- **[Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)** - NeoPixel LED control
- **[Preferences](https://github.com/vshymanskyy/Preferences)** - EEPROM storage (built-in)

### Steps

1. **Setup T-Display S3 environment**
   - Follow the [T-Display S3 installation guide](https://github.com/Xinyuan-LilyGO/T-Display-S3)
   - Ensure your T-Display S3 is properly configured and working with Arduino IDE

2. **Install libraries**
   - Open Arduino IDE
   - Go to `Tools > Manage Libraries`
   - Install the required libraries listed above

3. **Upload the code**
   - Open `flick-out.ino` in Arduino IDE
   - Select board: `LilyGo T3-S3`
   - Upload the sketch

4. **Upload GIF files to LittleFS**
   - Install [LittleFS Upload Tool](https://github.com/earlephilhower/arduino-littlefs-upload)
   - Place GIF files in `data/` folder
   - Use keyboard shortcut: `[Ctrl] + [Shift] + [P]`, then "Upload LittleFS to Pico/ESP8266/ESP32"

5. **Prepare SD card**
   - Use microSD card 32GB or smaller (DFPlayer Mini limitation)
   - Format microSD card as FAT32
   - ⚠️ **Important**: Copy MP3 files one by one in numerical order (001.mp3 first, then 002.mp3, etc.)
   - Insert into DFPlayer Mini

## 📁 File Structure

### Flash Memory (LittleFS) - GIF Files
```
data/
├── boot.gif         # Boot animation
├── idle.gif         # Idle state animation (loops)
├── start.gif        # Game start animation
├── fight.gif        # Boxing instruction animation
└── highScore.gif    # Highscore attract mode animation
```

### SD Card - Audio Files
```
sdcard/
├── 001.mp3         # Boot music
├── 002.mp3         # Game start sound
├── 003.mp3         # Weak punch sound
├── 004.mp3         # Medium punch sound
├── 005.mp3         # Strong punch sound
├── 008.mp3         # New highscore victory sound
├── 009.mp3         # Score animation sound
└── 010.mp3         # Countdown music (loops during continue screen)
```

## 🎮 Controls

| Action | Control |
|--------|---------|
| **Start Game** | Single click button |
| **Volume Menu** | Double-click button |
| **Reset Highscore** | Long press (3s) in idle mode |
| **Exit Volume Menu** | Long press (1.5s) in volume menu |
| **Cycle Volume** | Single click in volume menu |

## 🔧 Configuration

### Score Calibration
```cpp
float scoreCalibration = 1.0;    // Global calibration factor
                                // 1.0 = default scale
                                // 0.9 = easier scoring  
                                // 1.1 = harder scoring
```

### Volume Levels
```cpp
const int VOLUME_LEVELS[] = {0, 1, 5, 10, 15, 20, 25, 30};
```

### Timing Constants
```cpp
const unsigned long CONTINUE_TIMEOUT = 15000;          // Continue screen timeout
const unsigned long IDLE_ATTRACT_DELAY = 10000;        // Time before attract mode
const unsigned long ATTRACT_DISPLAY_DURATION = 15000;  // Attract mode duration
```

### Battery Thresholds
```cpp
#define BATTERY_MAX_VOLTAGE 4.2f        // Full battery
#define BATTERY_MIN_VOLTAGE 3.3f        // Low battery warning
#define BATTERY_CRITICAL_VOLTAGE 3.1f   // Critical shutdown
```

## 💡 NeoPixel Effects

The game features dynamic LED lighting that responds to different game states:

- **Boot**: Rainbow sweep animation
- **Idle**: Soft breathing blue effect
- **Boxing**: Pulsing red during punch detection
- **Results**: Color-coded feedback based on score performance
- **Highscore**: Rainbow celebration effect
- **Volume Menu**: Visual volume level indicator
- **Battery Warning**: Urgent red flashing for low battery
- **Punch Impact**: Quick white flash effect on successful punch

> ⚠️ **Note**: NeoPixel brightness is automatically limited to protect the ESP32 from excessive current draw.

## 🔋 Battery Management

- **Real-time monitoring**: Displays battery percentage indicator
- **Low battery warning**: Visual alert when battery drops below 3.3V
- **Critical shutdown**: Automatic deep sleep at 3.1V to prevent damage
- **Charging support**: Built-in LiPo charging via USB-C

### 🔌 Battery Charging

⚠️ **Important**: The T-Display S3 must be powered ON to charge the battery via USB-C. If the device is turned off, the charging circuit will not work at all. Simply connect a USB-C cable while the game is running to charge the LiPo battery.

## 🐛 Troubleshooting

### 🔇 No audio output

- Check SD card is inserted in DFPlayer Mini
- Verify MP3 files are numbered correctly (001.mp3, 002.mp3, etc.)
- Check DFPlayer wiring (RX to GPIO 21, TX to GPIO 16)
- Ensure volume is not set to 0

### 📺 Display issues

- Ensure display power pin (GPIO 15) is high
- Verify GIF files are uploaded to LittleFS
- Check backlight pin (GPIO 38)

### 👊 FSR not responding

- Check FSR connection to GPIO 12
- Ensure FSR has proper pull-up resistor
- Monitor serial output for FSR readings
- Test with different FSR values

## 📊 Technical Specifications

| Specification | Value |
|---------------|-------|
| **Display** | 170x320 pixel ST7789 TFT |
| **Scoring Range** | 0-999 points |
| **FSR Input Range** | 0-4095 (12-bit ADC) |
| **Audio Formats** | MP3 |
| **Storage** | LittleFS (Flash) + SD Card |
| **Power** | 3.7V LiPo battery |

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Credits

- **Music**: Battle Music by Dragon-Studio
- **Development**: Guillaume Loquin (Guybrush)

---

**Punch your way to the top score! 🥊**

*Made with ❤️ by Guillaume Loquin*