/*******************************************************************************
 * Flick - Out!! for LilyGO T-Display S3 with NeoPixel Effects
 * 
 * Game Flow:
 * 1. Boot: Play sync music + gif, then move to idle
 * 2. Idle: Wait for button press, then start sound (002.mp3)
 *    - Double-click: Enter volume menu
 * 3. Boxing: Show fight.gif looping and detect FSR impact, calculate score, show result with sound
 * 4. Attract modes: Cycle between highscore display and credits
 * 5. Volume Menu: Single click to cycle volume, long press to exit
 * 
 * Hardware:
 * - LilyGO T-Display S3 (ESP32-S3)
 * - DFPlayer Mini MP3 module
 * - FSR (Force Sensitive Resistor) on GPIO 12
 * - Boot button (GPIO 0) for game start
 * - External button (GPIO 17) for game start - with internal pullup
 * - NeoPixel stick on GPIO 11 (8 LEDs)
 * - LiPo battery connected to JST connector with built-in charging
 * 
 * Files needed in LittleFS (flash memory):
 * - boot.gif (boot animation)
 * - idle.gif (idle animation) 
 * - start.gif (game start animation)
 * - fight.gif (boxing instruction animation - loops during punch waiting)
 * - highScore.gif (highscore attract animation - loops during highscore display)
 * 
 * Audio files on SD card (DFPlayer):
 * - 001.mp3 (boot music)
 * - 002.mp3 (game start sound)
 * - 003.mp3 (weak punch sound)
 * - 004.mp3 (medium punch sound)
 * - 005.mp3 (strong punch sound)
 * - 008.mp3 (new highscore victory sound)
 * - 009.mp3 (complete score animation sound - 4 seconds with rhythmic + staccato beeps)
 * - 010.mp3 (countdown music - loops during "press button to continue" phase)
 ******************************************************************************/

#include <Arduino.h>
#include <LittleFS.h>
#include <Arduino_GFX_Library.h>
#include <DFRobotDFPlayerMini.h>
#include <Preferences.h>
#include <FS.h>
#include <sys/types.h>
#include <Adafruit_NeoPixel.h>

// Hardware configuration for T-Display S3
#define GFX_EXTRA_PRE_INIT() \
  { \
    pinMode(15, OUTPUT); \
    digitalWrite(15, HIGH); \
  }
#define GFX_BL 38
#define FSR_PIN 12
#define BUTTON_PIN 0
#define EXTERNAL_BUTTON_PIN 17
#define BATTERY_PIN 4
#define RXD2 21
#define TXD2 16
#define NEOPIXEL_PIN 11
#define NEOPIXEL_COUNT 8

// NeoPixel setup
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// NeoPixel animation variables
unsigned long lastNeoPixelUpdate = 0;
int neoPixelAnimationStep = 0;
bool neoPixelDirection = true;
float neoPixelBrightness = 1.0;

// Battery monitoring constants
#define BATTERY_MAX_VOLTAGE 4.2f
#define BATTERY_MIN_VOLTAGE 3.3f
#define BATTERY_CRITICAL_VOLTAGE 3.1f
#define VOLTAGE_DIVIDER_RATIO 2.0f
#define ADC_MAX_VALUE 4095.0f
#define ADC_REFERENCE_VOLTAGE 3.3f

// Game states
enum GameState {
  STATE_BOOT,
  STATE_IDLE,
  STATE_ATTRACT_HIGHSCORE,
  STATE_ATTRACT_CREDITS,
  STATE_BOXING,
  STATE_RESULT,
  STATE_NEW_HIGHSCORE,
  STATE_LOW_BATTERY_WARNING,
  STATE_CRITICAL_BATTERY_SHUTDOWN,
  STATE_VOLUME_MENU
};

// GIF Class implementation
#ifndef MIN
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#endif

#ifndef MAX
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif

#define GIF_BUF_SIZE 1024

typedef struct gd_Palette {
  int16_t len;
  uint16_t colors[256];
} gd_Palette;

typedef struct gd_GCE {
  uint16_t delay;
  uint8_t tindex;
  uint8_t disposal;
  uint8_t input;
  uint8_t transparency;
} gd_GCE;

typedef struct gd_Entry {
  int32_t len;
  uint16_t prefix;
  uint8_t suffix;
} gd_Entry;

typedef struct gd_Table {
  int16_t bulk;
  int16_t nentries;
  gd_Entry *entries;
} gd_Table;

typedef struct gd_GIF {
  File *fd;
  off_t anim_start;
  uint16_t width, height;
  uint16_t depth;
  uint16_t loop_count;
  gd_GCE gce;
  gd_Palette *palette;
  gd_Palette lct, gct;
  void (*plain_text)(
    struct gd_GIF *gif, uint16_t tx, uint16_t ty,
    uint16_t tw, uint16_t th, uint8_t cw, uint8_t ch,
    uint8_t fg, uint8_t bg);
  void (*comment)(struct gd_GIF *gif);
  void (*application)(struct gd_GIF *gif, char id[8], char auth[3]);
  uint16_t fx, fy, fw, fh;
  uint8_t bgindex;
  gd_Table *table;
  bool processed_first_frame;
} gd_GIF;

class GifClass {
public:
  gd_GIF *gd_open_gif(File *fd);
  int32_t gd_get_frame(gd_GIF *gif, uint8_t *frame);
  void gd_rewind(gd_GIF *gif);
  void gd_close_gif(gd_GIF *gif);

private:
  bool gif_buf_seek(File *fd, int16_t len);
  int16_t gif_buf_read(File *fd, uint8_t *dest, int16_t len);
  uint8_t gif_buf_read(File *fd);
  uint16_t gif_buf_read16(File *fd);
  void read_palette(File *fd, gd_Palette *dest, int16_t num_colors);
  void discard_sub_blocks(gd_GIF *gif);
  void read_plain_text_ext(gd_GIF *gif);
  void read_graphic_control_ext(gd_GIF *gif);
  void read_comment_ext(gd_GIF *gif);
  void read_application_ext(gd_GIF *gif);
  void read_ext(gd_GIF *gif);
  gd_Table *new_table();
  void reset_table(gd_Table *table, uint16_t key_size);
  int32_t add_entry(gd_Table *table, int32_t len, uint16_t prefix, uint8_t suffix);
  uint16_t get_key(gd_GIF *gif, uint16_t key_size, uint8_t *sub_len, uint8_t *shift, uint8_t *byte);
  int16_t interlaced_line_index(int16_t h, int16_t y);
  int8_t read_image_data(gd_GIF *gif, int16_t interlace, uint8_t *frame);
  int8_t read_image(gd_GIF *gif, uint8_t *frame);
  void render_frame_rect(gd_GIF *gif, uint16_t *buffer, uint8_t *frame);

  int16_t gif_buf_last_idx, gif_buf_idx, file_pos;
  uint8_t gif_buf[GIF_BUF_SIZE];
};

// Global variables
bool waitingForPunch = true;
unsigned long continueTimer = 0;
unsigned long newHighscoreTimer = 0;
unsigned long idleTimer = 0;
unsigned long attractTimer = 0;
unsigned long batteryCheckTimer = 0;
unsigned long lowBatteryWarningTimer = 0;
int resultPhase = 0;
const unsigned long CONTINUE_TIMEOUT = 15000;
const unsigned long NEW_HIGHSCORE_DURATION = 5000;
const unsigned long IDLE_ATTRACT_DELAY = 10000;
const unsigned long ATTRACT_DISPLAY_DURATION = 15000;
const unsigned long BATTERY_CHECK_INTERVAL = 5000;
const unsigned long LOW_BATTERY_WARNING_DURATION = 5000;
const float GLOBAL_BRIGHTNESS = 0.3; // 30% brightness
// Credits scrolling variables
unsigned long creditsStartTime = 0;
int creditsScrollY = 0;
bool creditsInitialized = false;

// Battery monitoring variables
float batteryVoltage = 0.0f;
int batteryPercentage = 0;
bool lowBatteryWarning = false;
bool criticalBattery = false;

// Highscore system
Preferences preferences;
int highScore = 0;

// Reset highscore system (long press) - tracks both buttons
unsigned long buttonPressStartTime = 0;
unsigned long externalButtonPressStartTime = 0;
bool buttonHeld = false;
bool externalButtonHeld = false;
unsigned long resetMessageTimer = 0;
bool showResetMessage = false;
const unsigned long LONG_PRESS_DURATION = 3000;
const unsigned long VOLUME_MENU_LONG_PRESS_DURATION = 1500;
const unsigned long RESET_MESSAGE_DURATION = 3000;

// Attract mode cycling
bool attractShowingHighscore = true;

// Volume control variables
int currentVolume = 10;
const int VOLUME_LEVELS[] = {0, 1, 5, 10, 15, 20, 25, 30};
const int NUM_VOLUME_LEVELS = 8;
int currentVolumeIndex = 3;  // Start at volume 10 (index 3)

// Score calibration variable - adjust based on hardware differences
// 1.0 = default scale, 0.9 = easier scoring, 1.1 = harder scoring
float scoreCalibration = 1.0;

// Double-click detection variables
unsigned long lastButtonReleaseTime = 0;
unsigned long lastExternalButtonReleaseTime = 0;
bool waitingForSecondClick = false;
bool waitingForSecondExternalClick = false;
const unsigned long DOUBLE_CLICK_WINDOW = 500;

// Volume menu variables
unsigned long volumeMenuTimer = 0;
bool volumeMenuActive = false;
bool justExitedVolumeMenu = false;

// Audio state management
bool idleMusicLooping = false;
bool countdownMusicLooping = false;
unsigned long lastMusicCheck = 0;
const unsigned long MUSIC_CHECK_INTERVAL = 1000;

// Display setup
Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
  7, 6, 8, 9, 39, 40, 41, 42, 45, 46, 47, 48);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 5, 1, true, 170, 320, 35, 0, 35, 0);

// Audio setup
HardwareSerial FPSerial(2);
DFRobotDFPlayerMini myDFPlayer;

// GIF handling
static GifClass gifClass;
int16_t display_width, display_height;

// Game variables
GameState currentState = STATE_BOOT;
unsigned long stateTimer = 0;
int currentScore = 0;
bool buttonPressed = false;
bool externalButtonPressed = false;

/*******************************************************************************
 * NeoPixel Effect Functions
 ******************************************************************************/

void clearNeoPixels() {
  pixels.clear();
  pixels.show();
}

void setAllNeoPixels(uint32_t color, float brightness = GLOBAL_BRIGHTNESS) {
  for(int i = 0; i < NEOPIXEL_COUNT; i++) {
    uint8_t r = (uint8_t)((color >> 16) & 0xFF) * brightness;
    uint8_t g = (uint8_t)((color >> 8) & 0xFF) * brightness;
    uint8_t b = (uint8_t)(color & 0xFF) * brightness;
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void neoPixelBootEffect() {
  // Simple rainbow wheel that rotates
  static int wheelPos = 0;
  
  if (millis() - lastNeoPixelUpdate > 80) {
    wheelPos = (wheelPos + 4) % 256; // Increment faster to see movement
    
    for(int i = 0; i < NEOPIXEL_COUNT; i++) {
      // Smaller spacing between LEDs to see the scroll effect better
      int hue = (wheelPos + (i * 32)) % 256; // 32 instead of 256/8=32
      uint32_t color = pixels.gamma32(pixels.ColorHSV(hue * 256));
      
      // Reduce brightness
      uint8_t r = ((color >> 16) & 0xFF) * 0.3;
      uint8_t g = ((color >> 8) & 0xFF) * 0.3;
      uint8_t b = (color & 0xFF) * 0.3;
      
      pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
    
    pixels.show();
    lastNeoPixelUpdate = millis();
  }
}

void neoPixelIdleEffect() {
  // K2000-style back and forth yellow/orange effect
  if (millis() - lastNeoPixelUpdate > 100) {
    pixels.clear();
    
    // Calculate position (0 to (NEOPIXEL_COUNT-1)*2-1 for back and forth)
    int maxPos = (NEOPIXEL_COUNT - 1) * 2;
    int pos = neoPixelAnimationStep % maxPos;
    
    // Convert to actual LED position
    int ledPos;
    if (pos < NEOPIXEL_COUNT) {
      ledPos = pos; // Forward direction
    } else {
      ledPos = maxPos - pos; // Backward direction
    }
    
    // Main bright LED (yellow/orange)
    pixels.setPixelColor(ledPos, pixels.Color(255, 150, 0)); // Orange-yellow
    
    // Trailing LEDs with fading effect
    if (ledPos > 0) {
      pixels.setPixelColor(ledPos - 1, pixels.Color(120, 60, 0)); // Dimmer trail
    }
    if (ledPos > 1) {
      pixels.setPixelColor(ledPos - 2, pixels.Color(50, 25, 0)); // Very dim trail
    }
    
    // Leading LEDs with fading effect  
    if (ledPos < NEOPIXEL_COUNT - 1) {
      pixels.setPixelColor(ledPos + 1, pixels.Color(120, 60, 0)); // Dimmer trail
    }
    if (ledPos < NEOPIXEL_COUNT - 2) {
      pixels.setPixelColor(ledPos + 2, pixels.Color(50, 25, 0)); // Very dim trail
    }
    
    pixels.show();
    neoPixelAnimationStep++;
    lastNeoPixelUpdate = millis();
  }
}

void neoPixelBoxingEffect() {
  // Pulsing red effect with increasing intensity
  if (millis() - lastNeoPixelUpdate > 100) {
    float pulse = (sin(millis() * 0.01) + 1.0) * 0.5;
    pulse = pulse * 0.6 + 0.2; // Scale to 0.2 to 0.8 brightness
    
    setAllNeoPixels(pixels.Color(255, 0, 0), pulse);
    lastNeoPixelUpdate = millis();
  }
}

void neoPixelAttractEffect() {
  // K2000-style back and forth RED effect - 2x faster with NO trails
  if (millis() - lastNeoPixelUpdate > 50) { // 50ms instead of 100ms = 2x faster
    pixels.clear();
    
    // Calculate position (0 to (NEOPIXEL_COUNT-1)*2-1 for back and forth)
    int maxPos = (NEOPIXEL_COUNT - 1) * 2;
    int pos = neoPixelAnimationStep % maxPos;
    
    // Convert to actual LED position
    int ledPos;
    if (pos < NEOPIXEL_COUNT) {
      ledPos = pos; // Forward direction
    } else {
      ledPos = maxPos - pos; // Backward direction
    }
    
    // Only the main bright LED (no trails)
    pixels.setPixelColor(ledPos, pixels.Color(255, 0, 0)); // Bright red
    
    pixels.show();
    neoPixelAnimationStep++;
    lastNeoPixelUpdate = millis();
  }
}

void neoPixelResultEffect(int score) {
  // Color based on score performance
  uint32_t color;
  if (score >= 800) {
    color = pixels.Color(255, 0, 255); // Magenta for excellent
  } else if (score >= 600) {
    color = pixels.Color(0, 255, 0); // Green for good
  } else if (score >= 300) {
    color = pixels.Color(255, 255, 0); // Yellow for okay
  } else {
    color = pixels.Color(255, 100, 0); // Orange for weak
  }
  
  // Flash effect
  if (millis() - lastNeoPixelUpdate > 500) {
    static bool flashOn = false;
    if (flashOn) {
      setAllNeoPixels(color, GLOBAL_BRIGHTNESS);
    } else {
      clearNeoPixels();
    }
    flashOn = !flashOn;
    lastNeoPixelUpdate = millis();
  }
}

void neoPixelHighscoreEffect() {
  // Rainbow celebration effect
  if (millis() - lastNeoPixelUpdate > 80) {
    for(int i = 0; i < NEOPIXEL_COUNT; i++) {
      int pixelHue = ((millis() / 10 + (i * 256 / NEOPIXEL_COUNT)) % 256);
      uint32_t color = pixels.gamma32(pixels.ColorHSV(pixelHue * 256));
      pixels.setPixelColor(i, color);
    }
    pixels.show();
    lastNeoPixelUpdate = millis();
  }
}

void neoPixelVolumeEffect(int volumeLevel) {
  // Volume bar visualization
  pixels.clear();
  
  int maxVolume = VOLUME_LEVELS[NUM_VOLUME_LEVELS - 1];
  int ledCount = map(volumeLevel, 0, maxVolume, 0, NEOPIXEL_COUNT);
  
  for(int i = 0; i < ledCount; i++) {
    uint32_t color;
    if (i < NEOPIXEL_COUNT / 3) {
      color = pixels.Color(0, 255, 0); // Green for low volume
    } else if (i < (NEOPIXEL_COUNT * 2) / 3) {
      color = pixels.Color(255, 255, 0); // Yellow for medium volume
    } else {
      color = pixels.Color(255, 0, 0); // Red for high volume
    }
    pixels.setPixelColor(i, color);
  }
  
  if (volumeLevel == 0) {
    // Blink red for mute
    if ((millis() / 500) % 2) {
      setAllNeoPixels(pixels.Color(255, 0, 0), GLOBAL_BRIGHTNESS);
    }
  }
  
  pixels.show();
}

void neoPixelLowBatteryEffect() {
  // Urgent red flashing
  if (millis() - lastNeoPixelUpdate > 250) {
    static bool flashOn = false;
    if (flashOn) {
      setAllNeoPixels(pixels.Color(255, 0, 0), GLOBAL_BRIGHTNESS);
    } else {
      clearNeoPixels();
    }
    flashOn = !flashOn;
    lastNeoPixelUpdate = millis();
  }
}

void neoPixelPunchImpactEffect() {
  // Explosive white flash effect when punch detected
  setAllNeoPixels(pixels.Color(255, 255, 255), GLOBAL_BRIGHTNESS);
  delay(100);
  clearNeoPixels();
  delay(50);
  setAllNeoPixels(pixels.Color(255, 255, 255), GLOBAL_BRIGHTNESS);
  delay(100);
  clearNeoPixels();
}

void updateNeoPixelEffects() {
  switch (currentState) {
    case STATE_BOOT:
      neoPixelBootEffect();
      break;
      
    case STATE_IDLE:
      if (showResetMessage) {
        // Special effect for reset message
        if ((millis() / 200) % 2) {
          setAllNeoPixels(pixels.Color(255, 0, 0), GLOBAL_BRIGHTNESS);
        } else {
          clearNeoPixels();
        }
      } else {
        neoPixelIdleEffect();
      }
      break;
      
    case STATE_ATTRACT_HIGHSCORE:
    case STATE_ATTRACT_CREDITS:
      neoPixelAttractEffect();
      break;
      
    case STATE_BOXING:
        if (waitingForPunch) {
    // Éteindre les NeoPixels pendant l'attente du punch
    clearNeoPixels();
  } else {
    // Réactiver les effets une fois le punch détecté
    neoPixelBoxingEffect();
  }
      break;
      
    case STATE_RESULT:
      neoPixelResultEffect(currentScore);
      break;
      
    case STATE_NEW_HIGHSCORE:
      neoPixelHighscoreEffect();
      break;
      
    case STATE_VOLUME_MENU:
      neoPixelVolumeEffect(currentVolume);
      break;
      
    case STATE_LOW_BATTERY_WARNING:
    case STATE_CRITICAL_BATTERY_SHUTDOWN:
      neoPixelLowBatteryEffect();
      break;
      
    default:
      clearNeoPixels();
      break;
  }
}

/*******************************************************************************
 * GIF Class Implementation
 ******************************************************************************/

gd_GIF *GifClass::gd_open_gif(File *fd) {
  uint8_t sigver[3];
  uint16_t width, height, depth;
  uint8_t fdsz, bgidx, aspect;
  int16_t gct_sz;
  gd_GIF *gif;

  gif_buf_last_idx = GIF_BUF_SIZE;
  gif_buf_idx = gif_buf_last_idx;
  file_pos = 0;

  gif_buf_read(fd, sigver, 3);
  if (memcmp(sigver, "GIF", 3) != 0) return NULL;

  gif_buf_read(fd, sigver, 3);
  if (memcmp(sigver, "89a", 3) != 0) return NULL;

  width = gif_buf_read16(fd);
  height = gif_buf_read16(fd);
  gif_buf_read(fd, &fdsz, 1);
  if (!(fdsz & 0x80)) return NULL;

  depth = ((fdsz >> 4) & 7) + 1;
  gct_sz = 1 << ((fdsz & 0x07) + 1);
  gif_buf_read(fd, &bgidx, 1);
  gif_buf_read(fd, &aspect, 1);

  gif = (gd_GIF *)calloc(1, sizeof(*gif));
  gif->fd = fd;
  gif->width = width;
  gif->height = height;
  gif->depth = depth;
  read_palette(fd, &gif->gct, gct_sz);
  gif->palette = &gif->gct;
  gif->bgindex = bgidx;
  gif->anim_start = file_pos;
  gif->table = new_table();
  gif->processed_first_frame = false;
  return gif;
}

int32_t GifClass::gd_get_frame(gd_GIF *gif, uint8_t *frame) {
  char sep;
  while (1) {
    gif_buf_read(gif->fd, (uint8_t *)&sep, 1);
    if (sep == 0) gif_buf_read(gif->fd, (uint8_t *)&sep, 1);
    if (sep == ',') break;
    if (sep == ';') return 0;
    if (sep == '!') read_ext(gif);
    else return -1;
  }
  if (read_image(gif, frame) == -1) return -1;
  return 1;
}

void GifClass::gd_rewind(gd_GIF *gif) {
  gif->fd->seek(gif->anim_start, SeekSet);
  file_pos = gif->anim_start;
  gif_buf_idx = gif_buf_last_idx;
}

void GifClass::gd_close_gif(gd_GIF *gif) {
  gif->fd->close();
  free(gif->table);
  free(gif);
}

bool GifClass::gif_buf_seek(File *fd, int16_t len) {
  if (len > (gif_buf_last_idx - gif_buf_idx)) {
    fd->seek(file_pos + len - (gif_buf_last_idx - gif_buf_idx), SeekSet);
    gif_buf_idx = gif_buf_last_idx;
  } else {
    gif_buf_idx += len;
  }
  file_pos += len;
  return true;
}

int16_t GifClass::gif_buf_read(File *fd, uint8_t *dest, int16_t len) {
  while (len--) {
    if (gif_buf_idx == gif_buf_last_idx) {
      gif_buf_last_idx = fd->read(gif_buf, GIF_BUF_SIZE);
      gif_buf_idx = 0;
    }
    file_pos++;
    *(dest++) = gif_buf[gif_buf_idx++];
  }
  return len;
}

uint8_t GifClass::gif_buf_read(File *fd) {
  if (gif_buf_idx == gif_buf_last_idx) {
    gif_buf_last_idx = fd->read(gif_buf, GIF_BUF_SIZE);
    gif_buf_idx = 0;
  }
  file_pos++;
  return gif_buf[gif_buf_idx++];
}

uint16_t GifClass::gif_buf_read16(File *fd) {
  return gif_buf_read(fd) + (((uint16_t)gif_buf_read(fd)) << 8);
}

void GifClass::read_palette(File *fd, gd_Palette *dest, int16_t num_colors) {
  uint8_t r, g, b;
  dest->len = num_colors;
  for (int16_t i = 0; i < num_colors; i++) {
    r = gif_buf_read(fd);
    g = gif_buf_read(fd);
    b = gif_buf_read(fd);
    dest->colors[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
  }
}

void GifClass::discard_sub_blocks(gd_GIF *gif) {
  uint8_t len;
  do {
    gif_buf_read(gif->fd, &len, 1);
    gif_buf_seek(gif->fd, len);
  } while (len);
}

void GifClass::read_plain_text_ext(gd_GIF *gif) {
  if (gif->plain_text) {
    uint16_t tx, ty, tw, th;
    uint8_t cw, ch, fg, bg;
    gif_buf_seek(gif->fd, 1);
    tx = gif_buf_read16(gif->fd);
    ty = gif_buf_read16(gif->fd);
    tw = gif_buf_read16(gif->fd);
    th = gif_buf_read16(gif->fd);
    cw = gif_buf_read(gif->fd);
    ch = gif_buf_read(gif->fd);
    fg = gif_buf_read(gif->fd);
    bg = gif_buf_read(gif->fd);
    gif->plain_text(gif, tx, ty, tw, th, cw, ch, fg, bg);
  } else {
    gif_buf_seek(gif->fd, 13);
  }
  discard_sub_blocks(gif);
}

void GifClass::read_graphic_control_ext(gd_GIF *gif) {
  uint8_t rdit;
  gif_buf_seek(gif->fd, 1);
  gif_buf_read(gif->fd, &rdit, 1);
  gif->gce.disposal = (rdit >> 2) & 3;
  gif->gce.input = rdit & 2;
  gif->gce.transparency = rdit & 1;
  gif->gce.delay = gif_buf_read16(gif->fd);
  gif_buf_read(gif->fd, &gif->gce.tindex, 1);
  gif_buf_seek(gif->fd, 1);
}

void GifClass::read_comment_ext(gd_GIF *gif) {
  if (gif->comment) gif->comment(gif);
  discard_sub_blocks(gif);
}

void GifClass::read_application_ext(gd_GIF *gif) {
  char app_id[8];
  char app_auth_code[3];
  gif_buf_seek(gif->fd, 1);
  gif_buf_read(gif->fd, (uint8_t *)app_id, 8);
  gif_buf_read(gif->fd, (uint8_t *)app_auth_code, 3);
  if (!strncmp(app_id, "NETSCAPE", sizeof(app_id))) {
    gif_buf_seek(gif->fd, 2);
    gif->loop_count = gif_buf_read16(gif->fd);
    gif_buf_seek(gif->fd, 1);
  } else if (gif->application) {
    gif->application(gif, app_id, app_auth_code);
    discard_sub_blocks(gif);
  } else {
    discard_sub_blocks(gif);
  }
}

void GifClass::read_ext(gd_GIF *gif) {
  uint8_t label;
  gif_buf_read(gif->fd, &label, 1);
  switch (label) {
    case 0x01: read_plain_text_ext(gif); break;
    case 0xF9: read_graphic_control_ext(gif); break;
    case 0xFE: read_comment_ext(gif); break;
    case 0xFF: read_application_ext(gif); break;
    default: Serial.print("Unknown extension: "); Serial.println(label, HEX);
  }
}

gd_Table *GifClass::new_table() {
  int32_t s = sizeof(gd_Table) + (sizeof(gd_Entry) * 4096);
  gd_Table *table = (gd_Table *)malloc(s);
  if (table) table->entries = (gd_Entry *)&table[1];
  return table;
}

void GifClass::reset_table(gd_Table *table, uint16_t key_size) {
  table->nentries = (1 << key_size) + 2;
  for (uint16_t key = 0; key < (1 << key_size); key++) {
    table->entries[key] = (gd_Entry){ 1, 0xFFF, (uint8_t)key };
  }
}

int32_t GifClass::add_entry(gd_Table *table, int32_t len, uint16_t prefix, uint8_t suffix) {
  table->entries[table->nentries] = (gd_Entry){ len, prefix, suffix };
  table->nentries++;
  if ((table->nentries & (table->nentries - 1)) == 0) return 1;
  return 0;
}

uint16_t GifClass::get_key(gd_GIF *gif, uint16_t key_size, uint8_t *sub_len, uint8_t *shift, uint8_t *byte) {
  int16_t bits_read, rpad, frag_size;
  uint16_t key = 0;
  for (bits_read = 0; bits_read < key_size; bits_read += frag_size) {
    rpad = (*shift + bits_read) % 8;
    if (rpad == 0) {
      if (*sub_len == 0) gif_buf_read(gif->fd, sub_len, 1);
      gif_buf_read(gif->fd, byte, 1);
      (*sub_len)--;
    }
    frag_size = MIN(key_size - bits_read, 8 - rpad);
    key |= ((uint16_t)((*byte) >> rpad)) << bits_read;
  }
  key &= (1 << key_size) - 1;
  *shift = (*shift + key_size) % 8;
  return key;
}

int16_t GifClass::interlaced_line_index(int16_t h, int16_t y) {
  int16_t p = (h - 1) / 8 + 1;
  if (y < p) return y * 8;
  y -= p;
  p = (h - 5) / 8 + 1;
  if (y < p) return y * 8 + 4;
  y -= p;
  p = (h - 3) / 4 + 1;
  if (y < p) return y * 4 + 2;
  y -= p;
  return y * 2 + 1;
}

int8_t GifClass::read_image_data(gd_GIF *gif, int16_t interlace, uint8_t *frame) {
  uint8_t sub_len, shift, byte, table_is_full = 0;
  uint16_t init_key_size, key_size;
  int32_t frm_off, str_len = 0, p, x, y;
  uint16_t key, clear, stop;
  int32_t ret;
  gd_Entry entry = { 0, 0, 0 };

  gif_buf_read(gif->fd, &byte, 1);
  key_size = (uint16_t)byte;
  clear = 1 << key_size;
  stop = clear + 1;
  reset_table(gif->table, key_size);
  key_size++;
  init_key_size = key_size;
  sub_len = shift = 0;
  key = get_key(gif, key_size, &sub_len, &shift, &byte);
  frm_off = 0;
  ret = 0;

  while (1) {
    if (key == clear) {
      key_size = init_key_size;
      gif->table->nentries = (1 << (key_size - 1)) + 2;
      table_is_full = 0;
    } else if (!table_is_full) {
      ret = add_entry(gif->table, str_len + 1, key, entry.suffix);
      if (gif->table->nentries == 0x1000) {
        ret = 0;
        table_is_full = 1;
      }
    }
    key = get_key(gif, key_size, &sub_len, &shift, &byte);
    if (key == clear) continue;
    if (key == stop) break;
    if (ret == 1) key_size++;
    entry = gif->table->entries[key];
    str_len = entry.len;
    uint8_t tindex = gif->gce.tindex;

    while (1) {
      p = frm_off + entry.len - 1;
      x = p % gif->fw;
      y = p / gif->fw;
      if (interlace) {
        y = interlaced_line_index((int16_t)gif->fh, y);
      }
      if ((!gif->processed_first_frame) || (tindex != entry.suffix)) {
        frame[(gif->fy + y) * gif->width + gif->fx + x] = entry.suffix;
      }
      if (entry.prefix == 0xFFF) break;
      else entry = gif->table->entries[entry.prefix];
    }
    frm_off += str_len;
    if (key < gif->table->nentries - 1 && !table_is_full)
      gif->table->entries[gif->table->nentries - 1].suffix = entry.suffix;
  }
  gif_buf_read(gif->fd, &sub_len, 1);
  gif->processed_first_frame = true;
  return 0;
}

int8_t GifClass::read_image(gd_GIF *gif, uint8_t *frame) {
  uint8_t fisrz;
  int16_t interlace;
  gif->fx = gif_buf_read16(gif->fd);
  gif->fy = gif_buf_read16(gif->fd);
  gif->fw = gif_buf_read16(gif->fd);
  gif->fh = gif_buf_read16(gif->fd);
  gif_buf_read(gif->fd, &fisrz, 1);
  interlace = fisrz & 0x40;
  if (fisrz & 0x80) {
    read_palette(gif->fd, &gif->lct, 1 << ((fisrz & 0x07) + 1));
    gif->palette = &gif->lct;
  } else {
    gif->palette = &gif->gct;
  }
  return read_image_data(gif, interlace, frame);
}

/*******************************************************************************
 * Battery Monitoring Functions
 ******************************************************************************/

float readBatteryVoltage() {
  int adcReading = analogRead(BATTERY_PIN);
  float voltage = (adcReading / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE * VOLTAGE_DIVIDER_RATIO;
  return voltage;
}

int calculateBatteryPercentage(float voltage) {
  if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
  if (voltage <= BATTERY_MIN_VOLTAGE) return 0;

  float range = BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE;
  float current = voltage - BATTERY_MIN_VOLTAGE;
  return (int)((current / range) * 100.0f);
}

void updateBatteryStatus() {
  unsigned long currentTime = millis();

  if (currentTime - batteryCheckTimer >= BATTERY_CHECK_INTERVAL) {
    batteryVoltage = readBatteryVoltage();
    batteryPercentage = calculateBatteryPercentage(batteryVoltage);

    Serial.printf("Battery: %.2fV (%d%%)\n", batteryVoltage, batteryPercentage);

    if (batteryVoltage <= BATTERY_CRITICAL_VOLTAGE && !criticalBattery) {
      criticalBattery = true;
      Serial.println("CRITICAL BATTERY! Initiating shutdown...");
      currentState = STATE_CRITICAL_BATTERY_SHUTDOWN;
    } 
    else if (batteryVoltage > BATTERY_CRITICAL_VOLTAGE && criticalBattery) {
      criticalBattery = false;
      Serial.println("Battery recovered from critical level");
    }

    if (batteryVoltage <= BATTERY_MIN_VOLTAGE && !lowBatteryWarning && !criticalBattery) {
      lowBatteryWarning = true;
      lowBatteryWarningTimer = currentTime;
      Serial.println("LOW BATTERY WARNING!");
    }
    else if (batteryVoltage > BATTERY_MIN_VOLTAGE + 0.2f && lowBatteryWarning) {
      lowBatteryWarning = false;
      Serial.println("Battery recovered from low level");
    }

    batteryCheckTimer = currentTime;
  }
}

void drawBatteryIndicator() {
  int batteryX = display_width - 25;
  int batteryY = 5;
  int batteryWidth = 20;
  int batteryHeight = 10;

  gfx->drawRect(batteryX, batteryY, batteryWidth, batteryHeight, RGB565_WHITE);
  gfx->fillRect(batteryX + batteryWidth, batteryY + 2, 2, 6, RGB565_WHITE);

  int fillWidth = (batteryPercentage * (batteryWidth - 2)) / 100;
  uint16_t fillColor;

  if (batteryPercentage > 50) {
    fillColor = RGB565_GREEN;
  } else if (batteryPercentage > 20) {
    fillColor = RGB565_YELLOW;
  } else {
    fillColor = RGB565_RED;
  }

  if (fillWidth > 0) {
    gfx->fillRect(batteryX + 1, batteryY + 1, fillWidth, batteryHeight - 2, fillColor);
  }

  if (lowBatteryWarning && (millis() / 2000) % 2) {
    gfx->fillRect(batteryX, batteryY, batteryWidth + 2, batteryHeight, RGB565_RED);
  }
}

/*******************************************************************************
 * Volume Control Functions
 ******************************************************************************/

void loadVolume() {
  preferences.begin("arcade-game", false);
  currentVolumeIndex = preferences.getInt("volume-idx", 3);  // Default to index 3 (volume 10)
  currentVolume = VOLUME_LEVELS[currentVolumeIndex];
  preferences.end();
  
  // Only set DFPlayer volume if not muted
  if (currentVolume > 0) {
    myDFPlayer.volume(currentVolume);
    Serial.printf("Loaded volume: %d (index %d)\n", currentVolume, currentVolumeIndex);
  } else {
    Serial.printf("Loaded volume: MUTED (index %d) - no DFPlayer command sent\n", currentVolumeIndex);
  }
}

void saveVolume() {
  preferences.begin("arcade-game", false);
  preferences.putInt("volume-idx", currentVolumeIndex);
  preferences.end();
  Serial.printf("Saved volume index: %d (volume: %d)\n", currentVolumeIndex, currentVolume);
}

void cycleVolume() {
  currentVolumeIndex = (currentVolumeIndex + 1) % NUM_VOLUME_LEVELS;
  currentVolume = VOLUME_LEVELS[currentVolumeIndex];
  
  // Only send volume command to DFPlayer if not muted
  if (currentVolume > 0) {
    myDFPlayer.volume(currentVolume);
    Serial.printf("Volume changed to: %d (index %d)\n", currentVolume, currentVolumeIndex);
  } else {
    Serial.printf("Volume muted (index %d) - no DFPlayer command sent\n", currentVolumeIndex);
  }
  
  saveVolume();
}

void drawVolumeMenu(bool forceRedraw = false) {
  static int lastDisplayedVolume = -1;
  static bool menuInitialized = false;
  
  // Reset menu state when entering volume menu for the first time
  if (forceRedraw) {
    menuInitialized = false;
    lastDisplayedVolume = -1;
  }
  
  // Only redraw if volume changed or forced redraw
  if (!forceRedraw && menuInitialized && currentVolume == lastDisplayedVolume) {
    return;
  }
  
  if (!menuInitialized || forceRedraw) {
    // Full redraw - clear screen and draw static elements
    gfx->fillScreen(RGB565_BLACK);
    
    // Title
    gfx->setTextColor(RGB565_CYAN);
    gfx->setTextSize(3);
    String title = "VOLUME";
    int titleWidth = title.length() * 6 * 3;
    int titleX = (display_width - titleWidth) / 2;
    gfx->setCursor(titleX, 30);
    gfx->println("VOLUME");
    
    // Instructions (static elements)
    gfx->setTextColor(RGB565_YELLOW);
    gfx->setTextSize(2);
    String instr1 = "Click: Change";
    int instr1Width = instr1.length() * 6 * 2;
    int instr1X = (display_width - instr1Width) / 2;
    gfx->setCursor(instr1X, 190);
    gfx->println("Click: Change");
    
    String instr2 = "Hold: Exit";
    int instr2Width = instr2.length() * 6 * 2;
    int instr2X = (display_width - instr2Width) / 2;
    gfx->setCursor(instr2X, 215);
    gfx->println("Hold: Exit");
    
    menuInitialized = true;
    Serial.println("Volume menu initialized");
  }
  
  // Update volume display area only
  if (currentVolume != lastDisplayedVolume || forceRedraw) {
    // Clear volume display area (larger area to include MUTE text)
    gfx->fillRect(0, 75, display_width, 115, RGB565_BLACK);
    
    // Current volume number
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(6);
    String volumeText = String(currentVolume);
    int volumeWidth = volumeText.length() * 6 * 6;
    int volumeX = (display_width - volumeWidth) / 2;
    gfx->setCursor(volumeX, 80);
    gfx->printf("%d", currentVolume);
    
    // Volume bar
    int barWidth = 200;
    int barHeight = 20;
    int barX = (display_width - barWidth) / 2;
    int barY = 150;
    
    // Bar outline
    gfx->drawRect(barX, barY, barWidth, barHeight, RGB565_WHITE);
    
    // Fill bar based on current volume
    int maxVolume = VOLUME_LEVELS[NUM_VOLUME_LEVELS - 1];
    int fillWidth = 0;
    
    // Handle volume 0 case (no fill)
    if (currentVolume > 0) {
      fillWidth = (currentVolume * (barWidth - 4)) / maxVolume;
    }
    
    if (fillWidth > 0) {
      uint16_t fillColor = RGB565_GREEN;
      if (currentVolume >= 15) fillColor = RGB565_YELLOW;
      if (currentVolume >= 25) fillColor = RGB565_RED;
      
      gfx->fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, fillColor);
    }
    
    // Show "MUTE" text when volume is 0
    if (currentVolume == 0) {
      gfx->setTextColor(RGB565_RED);
      gfx->setTextSize(2);
      String muteText = "MUTE";
      int muteWidth = muteText.length() * 6 * 2;
      int muteX = (display_width - muteWidth) / 2;
      gfx->setCursor(muteX, barY + 25);
      gfx->println("MUTE");
    }
    
    lastDisplayedVolume = currentVolume;
    Serial.printf("Volume display updated: %d\n", currentVolume);
  }
}

// Reset all button states - fixes volume menu access issue
void resetAllButtonStates() {
  buttonHeld = false;
  externalButtonHeld = false;
  waitingForSecondClick = false;
  waitingForSecondExternalClick = false;
  justExitedVolumeMenu = false;
  buttonPressed = false;
  externalButtonPressed = false;
  lastButtonReleaseTime = 0;
  lastExternalButtonReleaseTime = 0;
  Serial.println("All button states reset");
}

// Reset volume menu display state
void resetVolumeMenuDisplay() {
  // This will force a full redraw on next drawVolumeMenu call
  static bool resetRequested = true;
  resetRequested = true;
}

bool checkDoubleClick(unsigned long currentTime, bool bootButtonActive, bool externalButtonActive) {
  static bool bootButtonJustReleased = false;
  static bool externalButtonJustReleased = false;
  
  // Check boot button release
  if (digitalRead(BUTTON_PIN) == HIGH && buttonHeld) {
    bootButtonJustReleased = true;
    buttonHeld = false;
    Serial.println("Boot button released, checking for double-click");
  }
  
  // Check external button release
  if (digitalRead(EXTERNAL_BUTTON_PIN) == HIGH && externalButtonHeld) {
    externalButtonJustReleased = true;
    externalButtonHeld = false;
    Serial.println("External button released, checking for double-click");
  }
  
  // Handle boot button double-click logic
  if (bootButtonJustReleased) {
    bootButtonJustReleased = false;
    
    if (waitingForSecondClick && (currentTime - lastButtonReleaseTime <= DOUBLE_CLICK_WINDOW)) {
      waitingForSecondClick = false;
      Serial.println("Boot button double-click detected!");
      return true;
    } else {
      waitingForSecondClick = true;
      lastButtonReleaseTime = currentTime;
      Serial.println("Boot button first click - waiting for second click");
    }
  }
  
  // Handle external button double-click logic
  if (externalButtonJustReleased) {
    externalButtonJustReleased = false;
    
    if (waitingForSecondExternalClick && (currentTime - lastExternalButtonReleaseTime <= DOUBLE_CLICK_WINDOW)) {
      waitingForSecondExternalClick = false;
      Serial.println("External button double-click detected!");
      return true;
    } else {
      waitingForSecondExternalClick = true;
      lastExternalButtonReleaseTime = currentTime;
      Serial.println("External button first click - waiting for second click");
    }
  }
  
  return false;
}

/*******************************************************************************
 * Button Helper Functions
 ******************************************************************************/

bool anyButtonPressed() {
  return (digitalRead(BUTTON_PIN) == LOW) || (digitalRead(EXTERNAL_BUTTON_PIN) == LOW);
}

bool checkButtonRelease(unsigned long currentTime, unsigned long *holdDuration) {
  bool wasPressed = false;
  *holdDuration = 0;

  if (digitalRead(BUTTON_PIN) == HIGH && buttonHeld) {
    *holdDuration = currentTime - buttonPressStartTime;
    buttonHeld = false;
    wasPressed = true;
    Serial.printf("Boot button released after %.1fs\n", *holdDuration / 1000.0);
  }

  if (digitalRead(EXTERNAL_BUTTON_PIN) == HIGH && externalButtonHeld) {
    *holdDuration = currentTime - externalButtonPressStartTime;
    externalButtonHeld = false;
    wasPressed = true;
    Serial.printf("External button released after %.1fs\n", *holdDuration / 1000.0);
  }

  return wasPressed;
}

void handleButtonPress(unsigned long currentTime) {
  if (digitalRead(BUTTON_PIN) == LOW && !buttonHeld) {
    buttonHeld = true;
    buttonPressStartTime = currentTime;
    Serial.println("Boot button pressed");
  }

  if (digitalRead(EXTERNAL_BUTTON_PIN) == LOW && !externalButtonHeld) {
    externalButtonHeld = true;
    externalButtonPressStartTime = currentTime;
    Serial.println("External button pressed");
  }

  // Only allow highscore reset in IDLE state
  if (currentState == STATE_IDLE) {
    if (buttonHeld && (currentTime - buttonPressStartTime >= LONG_PRESS_DURATION)) {
      resetHighScore();
      showResetMessage = true;
      resetMessageTimer = currentTime;
      buttonHeld = false;
      Serial.println("Boot button long press (3s) - Highscore reset activated!");
    }

    if (externalButtonHeld && (currentTime - externalButtonPressStartTime >= LONG_PRESS_DURATION)) {
      resetHighScore();
      showResetMessage = true;
      resetMessageTimer = currentTime;
      externalButtonHeld = false;
      Serial.println("External button long press (3s) - Highscore reset activated!");
    }
  }
}

/*******************************************************************************
 * Highscore Functions
 ******************************************************************************/

void loadHighScore() {
  preferences.begin("arcade-game", false);
  highScore = preferences.getInt("highscore", 0);
  preferences.end();
  Serial.printf("Loaded highscore: %d\n", highScore);
}

void saveHighScore(int newScore) {
  preferences.begin("arcade-game", false);
  preferences.putInt("highscore", newScore);
  preferences.end();
  highScore = newScore;
  Serial.printf("Saved new highscore: %d\n", newScore);
}

bool isNewHighScore(int score) {
  return score > highScore;
}

void resetHighScore() {
  preferences.begin("arcade-game", false);
  preferences.putInt("highscore", 0);
  preferences.end();
  highScore = 0;
  Serial.println("HIGHSCORE RESET TO 0!");
}

void displayHighScore(int x, int y, int textSize) {
  gfx->setTextSize(textSize);
  gfx->setTextColor(RGB565_YELLOW);
  gfx->setCursor(x, y);
  gfx->printf("HI:%d", highScore);
}

/*******************************************************************************
 * Credits Display Functions
 ******************************************************************************/

void drawScrollingCredits() {
  static unsigned long lastUpdate = 0;
  static int lastScrollY = -999;
  static bool screenCleared = false;

  if (!creditsInitialized) {
    creditsStartTime = millis();
    creditsScrollY = display_height;
    creditsInitialized = true;
    lastScrollY = -999;
    screenCleared = false;
    gfx->fillScreen(RGB565_BLACK);
  }

  unsigned long currentTime = millis();

  if (currentTime - lastUpdate < 16) {
    return;
  }

  unsigned long elapsed = currentTime - creditsStartTime;
  creditsScrollY = display_height - (elapsed / 25);

  int pixelsMoved = abs(creditsScrollY - lastScrollY);
  if (pixelsMoved < 1 && screenCleared) {
    return;
  }

  lastUpdate = currentTime;
  lastScrollY = creditsScrollY;

  gfx->fillRect(0, 0, display_width, display_height, RGB565_BLACK);
  screenCleared = true;

  const char *creditsLines[] = {
    "FLICK - OUT!!",
    "",
    "Made with <3 by",
    "Guillaume Loquin",
    "(Guybrush)",
    "",
    "Music:",
    "Battle Music",
    "by Dragon-Studio",
    "",
    "Hardware:",
    "LilyGO T-Display S3",
    "DFPlayer Mini",
    "NeoPixel Effects",
    "",
    "Thanks for playing!",
    "",
    "Press button",
    "to start game!",
    ""
  };

  int numLines = sizeof(creditsLines) / sizeof(creditsLines[0]);
  int lineHeight = 30;
  int startY = creditsScrollY;

  for (int i = 0; i < numLines; i++) {
    int lineY = startY + (i * lineHeight);

    if (lineY > -50 && lineY < display_height + 30) {
      String line = String(creditsLines[i]);

      if (line.length() == 0) {
        continue;
      }

      int currentTextSize = 2;
      uint16_t textColor = RGB565_WHITE;

      if (i == 0) {
        currentTextSize = 3;
        textColor = RGB565_YELLOW;
      } else if (line.indexOf("Guillaume") >= 0 || line.indexOf("Guybrush") >= 0) {
        textColor = RGB565_YELLOW;
      } else if (line.indexOf("Made with") >= 0 || line.indexOf("Music:") >= 0 || line.indexOf("Hardware:") >= 0) {
        textColor = RGB565_GREEN;
      } else if (line.indexOf("Battle Music") >= 0 || line.indexOf("Dragon-Studio") >= 0) {
        textColor = RGB565_MAGENTA;
      } else if (line.indexOf("NeoPixel") >= 0) {
        textColor = RGB565_CYAN;
      } else if (line.indexOf("Thanks") >= 0 || line.indexOf("Press button") >= 0 || line.indexOf("to start") >= 0) {
        textColor = RGB565_RED;
      }

      gfx->setTextSize(currentTextSize);
      gfx->setTextColor(textColor);

      int textWidth = line.length() * 6 * currentTextSize;
      int x = (display_width - textWidth) / 2;

      gfx->setCursor(x, lineY);
      gfx->print(line);
    }
  }

  int totalHeight = numLines * lineHeight;
  if (creditsScrollY < -totalHeight - 100) {
    creditsInitialized = false;
  }
}

/*******************************************************************************
 * Game Functions
 ******************************************************************************/

bool playGif(const char *filename) {
  File gifFile = LittleFS.open(filename, "r");

  if (!gifFile || gifFile.isDirectory()) {
    Serial.printf("Failed to open GIF: %s\n", filename);
    return false;
  }

  gd_GIF *gif = gifClass.gd_open_gif(&gifFile);
  if (!gif) {
    Serial.println("gd_open_gif() failed!");
    gifFile.close();
    return false;
  }

  uint8_t *buf = (uint8_t *)malloc(gif->width * gif->height);
  if (!buf) {
    Serial.println("buf malloc failed!");
    gifClass.gd_close_gif(gif);
    return false;
  }

  int16_t x = (display_width - gif->width) / 2;
  int16_t y = (display_height - gif->height) / 2;

  int32_t res = 1;
  while (res > 0) {
    int32_t t_delay = gif->gce.delay * 10;
    res = gifClass.gd_get_frame(gif, buf);
    if (res > 0) {
      gfx->drawIndexedBitmap(x, y, buf, gif->palette->colors, gif->width, gif->height);
      delay(t_delay);
    }
  }

  free(buf);
  gifClass.gd_close_gif(gif);
  return true;
}

void playLoopingGif(const char *filename, bool drawTextOverlay = false, int overlayScore = 0) {
  static File gifFile;
  static gd_GIF *gif = nullptr;
  static uint8_t *buf = nullptr;
  static bool isInitialized = false;
  static unsigned long lastFrameTime = 0;
  static const char *currentFilename = nullptr;

  if (!isInitialized || (currentFilename && strcmp(currentFilename, filename) != 0)) {
    if (gif != nullptr) {
      free(buf);
      gifClass.gd_close_gif(gif);
      gif = nullptr;
      buf = nullptr;
    }
    isInitialized = false;
    currentFilename = filename;
  }

  if (!isInitialized) {
    gifFile = LittleFS.open(filename, "r");

    if (!gifFile || gifFile.isDirectory()) {
      Serial.printf("Failed to open %s\n", filename);
      return;
    }

    gif = gifClass.gd_open_gif(&gifFile);
    if (!gif) {
      Serial.printf("gd_open_gif() failed for %s!\n", filename);
      gifFile.close();
      return;
    }

    buf = (uint8_t *)malloc(gif->width * gif->height);
    if (!buf) {
      Serial.printf("buf malloc failed for %s!\n", filename);
      gifClass.gd_close_gif(gif);
      return;
    }

    isInitialized = true;
    lastFrameTime = millis();
    Serial.printf("%s initialized successfully\n", filename);
  }

  unsigned long currentTime = millis();
  if (currentTime - lastFrameTime >= (gif->gce.delay * 10)) {
    int32_t res = gifClass.gd_get_frame(gif, buf);
    if (res > 0) {
      int16_t x = (display_width - gif->width) / 2;
      int16_t y = (display_height - gif->height) / 2;
      gfx->drawIndexedBitmap(x, y, buf, gif->palette->colors, gif->width, gif->height);

      if (strcmp(filename, "/idle.gif") == 0) {
        drawBatteryIndicator();
      }

      if (drawTextOverlay) {
        gfx->setTextColor(RGB565_WHITE);
        gfx->setTextSize(8);

        String scoreText = String(overlayScore);
        int scoreWidth = scoreText.length() * 6 * 8;
        int scoreX = (display_width - scoreWidth) / 2;
        int scoreY = (display_height / 2) + 10;

        gfx->setTextColor(RGB565_BLACK);
        for (int dx = -2; dx <= 2; dx += 2) {
          for (int dy = -2; dy <= 2; dy += 2) {
            if (dx != 0 || dy != 0) {
              gfx->setCursor(scoreX + dx, scoreY + dy);
              gfx->printf("%d", overlayScore);
            }
          }
        }

        gfx->setTextColor(RGB565_WHITE);
        gfx->setCursor(scoreX, scoreY);
        gfx->printf("%d", overlayScore);
      }
    } else {
      gifClass.gd_rewind(gif);
    }
    lastFrameTime = currentTime;
  }
}

void resetLoopingGif() {
  Serial.println("Resetting looping GIF for next use");
}

void playSound(int trackNumber, bool random = false, bool looping = false) {
  // Skip DFPlayer commands if volume is muted (0)
  if (currentVolume == 0) {
    Serial.printf("Skipping sound track %d - volume is muted\n", trackNumber);
    return;
  }
  
  if (looping) {
    idleMusicLooping = true;
    myDFPlayer.play(trackNumber);
    Serial.printf("Starting idle music loop: %d\n", trackNumber);
  } else {
    idleMusicLooping = false;
    
    if (random) {
      if (currentScore < 150) {
        myDFPlayer.play(3);
      } else if (currentScore < 400) {
        myDFPlayer.play(4);
      } else {
        myDFPlayer.play(5);
      }
      Serial.printf("Playing random sound based on score: %d\n", currentScore);
    } else {
      myDFPlayer.play(trackNumber);
      Serial.printf("Playing sound track: %d\n", trackNumber);
    }
  }
}

void checkIdleMusic() {
  // Skip music check if volume is muted
  if (currentVolume == 0) {
    idleMusicLooping = false; // Stop tracking idle music when muted
    countdownMusicLooping = false; // Stop tracking countdown music when muted
    return;
  }
  
  // Check idle music loop
  if (idleMusicLooping && currentState == STATE_IDLE) {
    unsigned long currentTime = millis();
    if (currentTime - lastMusicCheck >= MUSIC_CHECK_INTERVAL) {
      if (!myDFPlayer.available()) {
        myDFPlayer.play(1);
        Serial.println("Restarting idle music loop");
      }
      lastMusicCheck = currentTime;
    }
  }
  
  // Check countdown music loop
  if (countdownMusicLooping && currentState == STATE_RESULT && resultPhase == 2) {
    unsigned long currentTime = millis();
    if (currentTime - lastMusicCheck >= MUSIC_CHECK_INTERVAL) {
      if (!myDFPlayer.available()) {
        myDFPlayer.play(10);
        Serial.println("Restarting countdown music loop");
      }
      lastMusicCheck = currentTime;
    }
  }
}

int calculateScore(int fsrValue) {
  int baseScore;

  if (fsrValue >= 3900) {
    baseScore = 0;
  } else if (fsrValue >= 3000) {
    baseScore = map(fsrValue, 3900, 3000, 0, 100);
  } else if (fsrValue >= 2000) {
    baseScore = map(fsrValue, 3000, 2000, 100, 300);
  } else if (fsrValue >= 1000) {
    baseScore = map(fsrValue, 2000, 1000, 300, 600);
  } else if (fsrValue >= 500) {
    baseScore = map(fsrValue, 1000, 500, 600, 850);
  } else {
    baseScore = map(fsrValue, 500, 150, 850, 999);
    baseScore = constrain(baseScore, 850, 999);
  }

  // Apply calibration factor and ensure score stays within valid range
  int calibratedScore = (int)(baseScore * scoreCalibration);
  
  Serial.printf("FSR: %d -> Base: %d -> Calibrated: %d (factor: %.2f)\n", 
                fsrValue, baseScore, calibratedScore, scoreCalibration);
  
  return constrain(calibratedScore, 0, 999);
}

void animateScoreCount(int targetScore) {
  gfx->fillScreen(RGB565_BLACK);
  displayHighScore(5, 5, 2);

  gfx->setTextColor(RGB565_RED);
  gfx->setTextSize(8);

  myDFPlayer.play(9);
  Serial.println("Starting score animation - playing rhythmic beeps (009.mp3)");

  delay(200);

  float currentScore = 0.0;
  float progress = 0.0;
  unsigned long animationStart = millis();
  const unsigned long totalAnimationTime = 4000;

  int lastDisplayedScore = -1;

  while (currentScore < targetScore) {
    unsigned long elapsed = millis() - animationStart;
    progress = (float)elapsed / totalAnimationTime;

    if (progress >= 1.0) {
      currentScore = targetScore;
    } else {
      float easedProgress;
      if (progress < 0.7) {
        easedProgress = pow(progress / 0.7, 0.3) * 0.85;
      } else {
        float finalPhase = (progress - 0.7) / 0.3;
        easedProgress = 0.85 + (0.15 * pow(finalPhase, 3));
      }

      currentScore = targetScore * easedProgress;
    }

    int displayScore = (int)currentScore;

    // Red flashing NeoPixels during score counting
    if ((millis() / 100) % 2) { // Flash every 100ms
      setAllNeoPixels(pixels.Color(255, 0, 0), GLOBAL_BRIGHTNESS); // Bright red flash
    } else {
      clearNeoPixels();
    }

    if (displayScore != lastDisplayedScore) {
      int textWidth = String(targetScore).length() * 6 * 8;
      int x = (display_width - textWidth) / 2;
      int y = (display_height - 64) / 2 + 20;

      gfx->fillRect(x - 10, y - 10, textWidth + 20, 64 + 20, RGB565_BLACK);
      displayHighScore(5, 5, 2);

      String scoreText = String(displayScore);
      int actualWidth = scoreText.length() * 6 * 8;
      int actualX = (display_width - actualWidth) / 2;

      gfx->setTextColor(RGB565_RED);
      gfx->setTextSize(8);
      gfx->setCursor(actualX, y);
      gfx->printf("%d", displayScore);

      lastDisplayedScore = displayScore;
    }

    if (progress < 0.7) {
      delay(8);
    } else if (progress < 0.9) {
      delay(50);
    } else {
      delay(150);
    }
  }

  // Final flashing sequence - faster red flashing
  for (int flash = 0; flash < 10; flash++) { // More flashes
    gfx->fillScreen(RGB565_BLACK);
    displayHighScore(5, 5, 2);

    gfx->setTextSize(10);

    if (flash % 2 == 0) {
      gfx->setTextColor(RGB565_WHITE);
      setAllNeoPixels(pixels.Color(255, 0, 0), GLOBAL_BRIGHTNESS); // Bright red
    } else {
      gfx->setTextColor(RGB565_RED);
      clearNeoPixels(); // Off
    }

    String scoreText = String(targetScore);
    int textWidth = scoreText.length() * 6 * 10;
    int x = (display_width - textWidth) / 2;
    int y = (display_height - 80) / 2 + 20;

    gfx->setCursor(x, y);
    gfx->printf("%d", targetScore);
    delay(150); // Fast flashing
  }

  gfx->fillScreen(RGB565_BLACK);
  displayHighScore(5, 5, 2);
  gfx->setTextSize(10);
  gfx->setTextColor(RGB565_RED);

  String finalScoreText = String(targetScore);
  int finalTextWidth = finalScoreText.length() * 6 * 10;
  int finalX = (display_width - finalTextWidth) / 2;
  int finalY = (display_height - 80) / 2 + 20;

  gfx->setCursor(finalX, finalY);
  gfx->printf("%d", targetScore);

  // Final NeoPixel effect - solid red for 1 second then off
  setAllNeoPixels(pixels.Color(255, 0, 0), GLOBAL_BRIGHTNESS);
  delay(1000);
  clearNeoPixels();
  delay(1000);

  Serial.println("Score animation completed - only used 009.mp3");
}

/*******************************************************************************
 * Setup and Main Loop
 ******************************************************************************/

void setup() {
  Serial.begin(115200);
  if (!myDFPlayer.begin(FPSerial, true, true)) {
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    //while (true) delay(0);
  }
  Serial.println(F("DFPlayer Mini online."));


  myDFPlayer.volume(1);
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);

  // Initialize NeoPixels with medium brightness
  pixels.begin();
  pixels.clear();
  pixels.show();
  pixels.setBrightness(128); // 50% brightness (128/255)
  Serial.println("NeoPixel initialized with 50% brightness");

  batteryCheckTimer = millis();
  updateBatteryStatus();

  if (!gfx->begin()) {
    Serial.println("Display initialization failed!");
  }
  gfx->fillScreen(RGB565_BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  display_width = gfx->width();
  display_height = gfx->height();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS initialization failed!");
    gfx->println("LittleFS Failed!");
    while (1);
  }
  Serial.println("LittleFS initialized successfully!");

  FPSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);

  Serial.println();
  Serial.println(F("DFRobot DFPlayer Mini Demo"));
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));



  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(EXTERNAL_BUTTON_PIN, INPUT_PULLUP);

  loadHighScore();
  loadVolume();

  Serial.println("Flick - Out!! Started with NeoPixel Effects!");
  Serial.printf("Initial battery: %.2fV (%d%%)\n", batteryVoltage, batteryPercentage);

  myDFPlayer.play(1);
  Serial.println("Playing boot sound (track 1)");
  currentState = STATE_BOOT;
}

void loop() {
  unsigned long currentTime = millis();

  updateBatteryStatus();
  checkIdleMusic();
  updateNeoPixelEffects(); // Update NeoPixel effects every loop

  
  if (lowBatteryWarning && currentState != STATE_LOW_BATTERY_WARNING && currentState != STATE_CRITICAL_BATTERY_SHUTDOWN) {
    static GameState previousState = currentState;
    previousState = currentState;
    currentState = STATE_LOW_BATTERY_WARNING;
    lowBatteryWarningTimer = currentTime;
    Serial.println("Entering low battery warning state");
  }

  switch (currentState) {
    case STATE_BOOT:
      if (playGif("/boot.gif")) {
        currentState = STATE_IDLE;
        idleTimer = millis();
        idleMusicLooping = true;
        lastMusicCheck = millis();
        resetAllButtonStates(); // Reset all button states when entering idle
        Serial.println("Boot GIF completed, entering idle state - idle music will loop");
      }
      break;

    case STATE_LOW_BATTERY_WARNING:
      {
        if ((millis() / 250) % 2) {
          gfx->fillScreen(RGB565_RED);
        } else {
          gfx->fillScreen(RGB565_BLACK);
        }

        drawBatteryIndicator();

        gfx->setTextColor(RGB565_WHITE);
        gfx->setTextSize(3);

        String text1 = "LOW BATTERY!";
        int textWidth1 = text1.length() * 6 * 3;
        int x1 = (display_width - textWidth1) / 2;
        int y1 = (display_height / 2) - 40;

        gfx->setCursor(x1, y1);
        gfx->println("LOW BATTERY!");

        gfx->setTextSize(2);
        String text2 = String(batteryPercentage) + "%";
        int textWidth2 = text2.length() * 6 * 2;
        int x2 = (display_width - textWidth2) / 2;
        int y2 = (display_height / 2) + 10;

        gfx->setCursor(x2, y2);
        gfx->printf("%d%%", batteryPercentage);

        if (millis() - lowBatteryWarningTimer >= LOW_BATTERY_WARNING_DURATION) {
          if (!criticalBattery) {
            currentState = STATE_IDLE;
            idleTimer = millis();
            Serial.println("Low battery warning finished, returning to idle");
          }
        }

        if (anyButtonPressed()) {
          currentState = STATE_IDLE;
          idleTimer = millis();
          Serial.println("Low battery warning dismissed by button press");
        }
      }
      break;

    case STATE_CRITICAL_BATTERY_SHUTDOWN:
      {
        gfx->fillScreen(RGB565_RED);
        gfx->setTextColor(RGB565_WHITE);
        gfx->setTextSize(2);

        String text1 = "CRITICAL BATTERY";
        int textWidth1 = text1.length() * 6 * 2;
        int x1 = (display_width - textWidth1) / 2;
        int y1 = (display_height / 2) - 30;

        String text2 = "SHUTTING DOWN";
        int textWidth2 = text2.length() * 6 * 2;
        int x2 = (display_width - textWidth2) / 2;
        int y2 = (display_height / 2) + 10;

        gfx->setCursor(x1, y1);
        gfx->println("CRITICAL BATTERY");
        gfx->setCursor(x2, y2);
        gfx->println("SHUTTING DOWN");

        delay(3000);

        clearNeoPixels();
        digitalWrite(GFX_BL, LOW);
        Serial.println("Entering deep sleep due to critical battery");
        esp_deep_sleep_start();
      }
      break;

    case STATE_NEW_HIGHSCORE:
      {
        static bool screenInitialized = false;
        static uint16_t lastTextColor = RGB565_BLACK;
        
        unsigned long elapsed = millis() - newHighscoreTimer;
        uint16_t textColor;
        if ((elapsed / 500) % 2 == 0) {
          textColor = RGB565_YELLOW;
        } else {
          textColor = RGB565_RED;
        }

        // Only redraw if color changed or first time
        if (!screenInitialized || textColor != lastTextColor) {
          if (!screenInitialized) {
            gfx->fillScreen(RGB565_BLACK);
            
            // Draw the score (constant white text)
            gfx->setTextColor(RGB565_WHITE);
            gfx->setTextSize(6);
            String scoreText = String(currentScore);
            int textWidth2 = scoreText.length() * 6 * 6;
            int x2 = (display_width - textWidth2) / 2;
            int y2 = (display_height / 2) + 10;
            gfx->setCursor(x2, y2);
            gfx->printf("%d", currentScore);
            
            screenInitialized = true;
          }
          
          // Only redraw the "NEW HIGHSCORE!" text area
          String text1 = "NEW HIGHSCORE!";
          int textWidth1 = text1.length() * 6 * 3;
          int x1 = (display_width - textWidth1) / 2;
          int y1 = (display_height / 2) - 40;
          
          // Clear only the text area
          gfx->fillRect(x1 - 5, y1 - 5, textWidth1 + 10, 24 + 10, RGB565_BLACK);
          
          // Draw the new text
          gfx->setTextColor(textColor);
          gfx->setTextSize(3);
          gfx->setCursor(x1, y1);
          gfx->println("NEW HIGHSCORE!");
          
          lastTextColor = textColor;
        }

        if (elapsed >= NEW_HIGHSCORE_DURATION) {
          // Reset static variables for next use
          screenInitialized = false;
          lastTextColor = RGB565_BLACK;
          
          currentState = STATE_RESULT;
          resultPhase = 2;
        }
      }
      break;

    case STATE_VOLUME_MENU:
      {
        // Draw volume menu only when needed
        drawVolumeMenu();
        
        if (anyButtonPressed()) {
          if (digitalRead(BUTTON_PIN) == LOW && !buttonHeld) {
            buttonHeld = true;
            buttonPressStartTime = currentTime;
            Serial.println("Boot button pressed in volume menu");
          }

          if (digitalRead(EXTERNAL_BUTTON_PIN) == LOW && !externalButtonHeld) {
            externalButtonHeld = true;
            externalButtonPressStartTime = currentTime;
            Serial.println("External button pressed in volume menu");
          }

          if (buttonHeld && (currentTime - buttonPressStartTime >= VOLUME_MENU_LONG_PRESS_DURATION)) {
            currentState = STATE_IDLE;
            volumeMenuActive = false;
            idleTimer = millis();
            justExitedVolumeMenu = true;
            resetAllButtonStates();
            Serial.println("Boot button long press - exiting volume menu");
          }

          if (externalButtonHeld && (currentTime - externalButtonPressStartTime >= VOLUME_MENU_LONG_PRESS_DURATION)) {
            currentState = STATE_IDLE;
            volumeMenuActive = false;
            idleTimer = millis();
            justExitedVolumeMenu = true;
            resetAllButtonStates();
            Serial.println("External button long press - exiting volume menu");
          }
        } else {
          unsigned long holdDuration;
          if (checkButtonRelease(currentTime, &holdDuration)) {
            if (holdDuration < VOLUME_MENU_LONG_PRESS_DURATION) {
              cycleVolume();
              // Volume menu will automatically redraw only the changed parts
              Serial.println("Volume changed via short press");
            }
          }
        }
      }
      break;

    case STATE_IDLE:
      {
        if (showResetMessage) {
          static bool resetMessageDisplayed = false;

          if (!resetMessageDisplayed) {
            gfx->fillScreen(RGB565_BLACK);
            gfx->setTextColor(RGB565_RED);
            gfx->setTextSize(3);

            String text1 = "HIGHSCORE";
            int textWidth1 = text1.length() * 6 * 3;
            int x1 = (display_width - textWidth1) / 2;
            int y1 = (display_height / 2) - 20;

            String text2 = "RESET!";
            int textWidth2 = text2.length() * 6 * 3;
            int x2 = (display_width - textWidth2) / 2;
            int y2 = (display_height / 2) + 20;

            gfx->setCursor(x1, y1);
            gfx->println("HIGHSCORE");
            gfx->setCursor(x2, y2);
            gfx->println("RESET!");

            resetMessageDisplayed = true;
            Serial.println("Reset message displayed");
          }

          if (millis() - resetMessageTimer >= RESET_MESSAGE_DURATION) {
            showResetMessage = false;
            resetMessageDisplayed = false;
            resetAllButtonStates();
            idleTimer = millis();
            gfx->fillScreen(RGB565_BLACK);
            Serial.println("Reset message finished, returning to idle gif");
          }

          return;
        }

        playLoopingGif("/idle.gif");

        if (millis() - idleTimer >= IDLE_ATTRACT_DELAY) {
          if (attractShowingHighscore) {
            currentState = STATE_ATTRACT_HIGHSCORE;
            Serial.println("Entering attract mode - showing highscore");
          } else {
            currentState = STATE_ATTRACT_CREDITS;
            creditsInitialized = false;
            Serial.println("Entering attract mode - showing credits");
          }
          attractTimer = millis();
          break;
        }

        // Check for double-click to enter volume menu
        if (checkDoubleClick(currentTime, digitalRead(BUTTON_PIN) == LOW, digitalRead(EXTERNAL_BUTTON_PIN) == LOW)) {
          currentState = STATE_VOLUME_MENU;
          volumeMenuActive = true;
          volumeMenuTimer = currentTime;
          drawVolumeMenu(true); // Force initial full redraw
          Serial.println("Double-click detected - entering volume menu");
          return;
        }

        if (anyButtonPressed()) {
          handleButtonPress(currentTime);
          idleTimer = currentTime;

          if (showResetMessage || justExitedVolumeMenu) {
            return;
          }
        } else {
          if (justExitedVolumeMenu && !buttonHeld && !externalButtonHeld) {
            justExitedVolumeMenu = false;
            Serial.println("Volume menu exit flag reset - ready for new inputs");
          }

          unsigned long holdDuration;
          if (checkButtonRelease(currentTime, &holdDuration)) {
            if (justExitedVolumeMenu) {
              justExitedVolumeMenu = false;
              Serial.println("Ignoring button release after volume menu exit");
              return;
            }
            
            if (holdDuration < LONG_PRESS_DURATION) {
              if (waitingForSecondClick || waitingForSecondExternalClick) {
                Serial.println("First click detected, waiting for potential second click...");
                return;
              } else {
                resetLoopingGif();
                Serial.printf("Button short press detected (%.1fs) - starting game\n", holdDuration / 1000.0);
                playSound(2);
                playGif("/start.gif");
                currentState = STATE_BOXING;
                stateTimer = millis();
                Serial.println("Game started! Punch the sensor!");
              }
            }
          }
          
          if ((waitingForSecondClick && (currentTime - lastButtonReleaseTime > DOUBLE_CLICK_WINDOW)) ||
              (waitingForSecondExternalClick && (currentTime - lastExternalButtonReleaseTime > DOUBLE_CLICK_WINDOW))) {
            waitingForSecondClick = false;
            waitingForSecondExternalClick = false;
            resetLoopingGif();
            Serial.println("Double-click window expired - treating as single click, starting game");
            playSound(2);
            playGif("/start.gif");
            currentState = STATE_BOXING;
            stateTimer = millis();
            Serial.println("Game started! Punch the sensor!");
          }
        }
      }
      break;

    case STATE_ATTRACT_HIGHSCORE:
      {
        playLoopingGif("/highScore.gif", true, highScore);

        unsigned long elapsed = currentTime - attractTimer;

        if (anyButtonPressed()) {
          handleButtonPress(currentTime);

          if (showResetMessage) {
            resetLoopingGif();
            currentState = STATE_IDLE;
            return;
          }
        } else {
          unsigned long holdDuration;
          if (checkButtonRelease(currentTime, &holdDuration)) {
            if (holdDuration < LONG_PRESS_DURATION) {
              resetLoopingGif();
              Serial.printf("Button short press during attract (%.1fs) - starting game\n", holdDuration / 1000.0);
              playSound(2);
              playGif("/start.gif");
              currentState = STATE_BOXING;
              stateTimer = millis();
              Serial.println("Game started! Punch the sensor!");
            }
          }
        }

        if (elapsed >= ATTRACT_DISPLAY_DURATION) {
          attractShowingHighscore = false;
          currentState = STATE_IDLE;
          idleTimer = millis();
          resetLoopingGif();
          Serial.println("Highscore attract mode finished, returning to idle");
          gfx->fillScreen(RGB565_BLACK);
        }
      }
      break;

    case STATE_ATTRACT_CREDITS:
      {
        drawScrollingCredits();

        unsigned long elapsed = currentTime - attractTimer;

        if (anyButtonPressed()) {
          handleButtonPress(currentTime);

          if (showResetMessage) {
            currentState = STATE_IDLE;
            return;
          }
        } else {
          unsigned long holdDuration;
          if (checkButtonRelease(currentTime, &holdDuration)) {
            if (holdDuration < LONG_PRESS_DURATION) {
              Serial.printf("Button short press during credits (%.1fs) - starting game\n", holdDuration / 1000.0);
              playSound(2);
              playGif("/start.gif");
              currentState = STATE_BOXING;
              stateTimer = millis();
              Serial.println("Game started! Punch the sensor!");
            }
          }
        }

        if (elapsed >= ATTRACT_DISPLAY_DURATION) {
          attractShowingHighscore = true;
          currentState = STATE_IDLE;
          idleTimer = millis();
          Serial.println("Credits attract mode finished, returning to idle");
          gfx->fillScreen(RGB565_BLACK);
        }
      }
      break;

 case STATE_BOXING:
{
  static int continuousMinFSR = 4096;
  static unsigned long lastResetTime = 0;
  static bool punchInProgress = false;
  static unsigned long punchDetectedTime = 0;

  if (waitingForPunch) {
    playLoopingGif("/fight.gif");

    // Show pulsing red effect while waiting for punch
    if (!punchInProgress) {
      neoPixelBoxingEffect();
    }

    int currentFSRReading = analogRead(FSR_PIN);

    if (millis() - lastResetTime > 2000 && !punchInProgress) {
      continuousMinFSR = 4096;
      lastResetTime = millis();
    }

    if (currentFSRReading < continuousMinFSR) {
      continuousMinFSR = currentFSRReading;

      if (currentFSRReading < 3800 && !punchInProgress) {
        Serial.printf("PUNCH START detected! Initial FSR: %d\n", currentFSRReading);
        punchInProgress = true;
        punchDetectedTime = millis();
        // Flash blanc will trigger AFTER sampling is complete
      }
    }

    if (punchInProgress) {
      unsigned long punchDuration = millis() - punchDetectedTime;

      if (punchDuration >= 500) {
        Serial.printf("PUNCH COMPLETE! Absolute minimum FSR captured: %d\n", continuousMinFSR);

        // White flash effect AFTER FSR sampling is complete
        neoPixelPunchImpactEffect();

        currentScore = calculateScore(continuousMinFSR);
        Serial.printf("Final Score: %d (from FSR: %d)\n", currentScore, continuousMinFSR);

        waitingForPunch = false;
        punchInProgress = false;
        continuousMinFSR = 4096;
        resetLoopingGif();
        currentState = STATE_RESULT;
      }
    }
  }
}
break;

    case STATE_RESULT:
      {
        if (resultPhase == 0) {
          animateScoreCount(currentScore);

          if (isNewHighScore(currentScore)) {
            saveHighScore(currentScore);
            playSound(8);
            currentState = STATE_NEW_HIGHSCORE;
            newHighscoreTimer = millis();
            resultPhase = 2;
            Serial.printf("NEW HIGHSCORE! Score: %d, Previous: %d, Playing victory sound (008.mp3)\n", currentScore, highScore);
            return;
          }

          gfx->fillScreen(RGB565_BLACK);
          displayHighScore(5, 5, 2);
          
          gfx->setTextColor(RGB565_RED);
          gfx->setTextSize(10);
          String finalScoreText = String(currentScore);
          int finalTextWidth = finalScoreText.length() * 6 * 10;
          int finalX = (display_width - finalTextWidth) / 2;
          int finalY = (display_height - 80) / 2 + 20;
          gfx->setCursor(finalX, finalY);
          gfx->printf("%d", currentScore);

          playSound(0, true);
          Serial.println("Playing result sound with score displayed");

          delay(3000);
          resultPhase = 2;
        } else if (resultPhase == 2) {
          static bool timerInitialized = false;
          static bool continueTextDisplayed = false;
          static bool countdownMusicStarted = false;

          if (!timerInitialized) {
            continueTimer = millis();
            timerInitialized = true;
            continueTextDisplayed = false;
            countdownMusicStarted = false;
          }
          
          // Start countdown music on first entry to this phase
          if (!countdownMusicStarted) {
            if (currentVolume > 0) {
              myDFPlayer.play(10);
              countdownMusicLooping = true;
              lastMusicCheck = millis();
              Serial.println("Starting countdown music loop (010.mp3)");
            }
            countdownMusicStarted = true;
          }

          unsigned long elapsed = millis() - continueTimer;
          unsigned long remaining = (elapsed < CONTINUE_TIMEOUT) ? (CONTINUE_TIMEOUT - elapsed) / 1000 : 0;

          static unsigned long lastRemaining = 999;
          if (!continueTextDisplayed || remaining != lastRemaining) {
            gfx->fillScreen(RGB565_BLACK);
            gfx->setTextColor(RGB565_WHITE);
            gfx->setTextSize(3);

            String text1 = "Press button";
            int textWidth1 = text1.length() * 6 * 3;
            int x1 = (display_width - textWidth1) / 2;
            int y1 = (display_height / 2) - 40;

            String text2 = "to continue";
            int textWidth2 = text2.length() * 6 * 3;
            int x2 = (display_width - textWidth2) / 2;
            int y2 = (display_height / 2) - 10;

            String text3 = "Time: " + String(remaining) + "s";
            int textWidth3 = text3.length() * 6 * 2;
            int x3 = (display_width - textWidth3) / 2;
            int y3 = (display_height / 2) + 30;

            gfx->setCursor(x1, y1);
            gfx->println("Press button");
            gfx->setCursor(x2, y2);
            gfx->println("to continue");

            gfx->setTextSize(2);
            gfx->setCursor(x3, y3);
            gfx->printf("Time: %ds", remaining);

            continueTextDisplayed = true;
            lastRemaining = remaining;
          }

          if (elapsed >= CONTINUE_TIMEOUT) {
            Serial.println("Continue timeout reached, returning to idle state");

            // Stop countdown music and reset flags
            countdownMusicLooping = false;
            
            waitingForPunch = true;
            resultPhase = 0;
            timerInitialized = false;
            continueTextDisplayed = false;

            resetAllButtonStates(); // Complete reset when returning to idle

            gfx->fillScreen(RGB565_BLACK);

            currentState = STATE_IDLE;
            idleTimer = millis();

            playSound(1, false, true);
            Serial.println("Successfully returned to IDLE state - playing looping idle music");
            break;
          }

          if (anyButtonPressed() && !buttonPressed && !externalButtonPressed) {
            buttonPressed = true;
            externalButtonPressed = true;

            // Stop countdown music when starting new game
            countdownMusicLooping = false;

            waitingForPunch = true;
            resultPhase = 0;
            timerInitialized = false;
            continueTextDisplayed = false;

            resetAllButtonStates(); // Reset when starting new game

            playSound(2);
            playGif("/start.gif");
            currentState = STATE_BOXING;
            Serial.println("Button released - New round started!");
          } else if (!anyButtonPressed()) {
            buttonPressed = false;
            externalButtonPressed = false;
          }
        }
      }
      break;
  }
}