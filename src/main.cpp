#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// ============= НАСТРОЙКИ =============
const int WEB_PORT        = 80;
const int MATRIX_WIDTH    = 8;
const int MATRIX_HEIGHT   = 8;
const int NUM_TILES       = 5;
const int LED_PIN         = D4;
const uint8_t BRIGHTNESS  = 60;

const int SCREEN_WIDTH    = NUM_TILES * MATRIX_WIDTH;
const int NUM_PIXELS      = MATRIX_WIDTH * MATRIX_HEIGHT * NUM_TILES;

const int EEPROM_SIZE     = 1024;
const int MAX_TEXT_LEN    = 64;
const int NUM_LINES       = 10;

const unsigned long DEFAULT_SCROLL_DELAY = 150;
unsigned long SCROLL_DELAY = DEFAULT_SCROLL_DELAY;

// Известные сети
const char* KNOWN_SSIDS[] = { "Root_HOME", "Root_home" };
const int NUM_KNOWN_SSIDS = 2;
const char* COMMON_PASSWORD = "rootrootroot19821609";

// Точка доступа
const char* AP_SSID = "ClockAP";
const char* AP_PASS = "12345678";
const unsigned long WIFI_TIMEOUT = 10000;

// Адреса EEPROM
const int WIFI_FLAG_ADDR = 0;
const int WIFI_SSID_ADDR = 1;
const int WIFI_PASS_ADDR = 33;
const int SETTINGS_ADDR  = 97;
const int TEXT_BLOCK_SIZE = NUM_LINES * (1 + MAX_TEXT_LEN);
const int COLOR_ADDR = SETTINGS_ADDR + TEXT_BLOCK_SIZE;
const int BRIGHTNESS_ADDR = COLOR_ADDR + NUM_LINES * 3;
const int SPEED_ADDR = BRIGHTNESS_ADDR + 1;

const int MAX_SSID_LEN = 32;
const int MAX_PASS_LEN = 64;

char savedSSID[MAX_SSID_LEN];
char savedPass[MAX_PASS_LEN];

// Инициализация
Adafruit_NeoPixel strip(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(WEB_PORT);

// Данные строк
char lines[NUM_LINES][MAX_TEXT_LEN + 1];
uint8_t reds[NUM_LINES], greens[NUM_LINES], blues[NUM_LINES];
uint8_t brightness = BRIGHTNESS;

int currentLineIndex = 0;
int textOffset = 0;
unsigned long lastScroll = 0;
unsigned long lastChange = 0;

enum DisplayState {
  SCROLLING,
  FADE_OUT,
  CHANGE_LINE,
  FADE_IN
};

DisplayState displayState = SCROLLING;
uint8_t currentBrightness = BRIGHTNESS;

IPAddress localIP;
bool shouldServeWiFi = false;

// Прототипы
int XY(int x, int y);
void drawChar(char c, int x, uint32_t color);
void drawCyrillicCharByIndex(int idx, int x, uint32_t color);
int getCyrillicIndex(const char* str, size_t i);
void showTextFrame();
void handleRoot();
void handleSaveBrightness();
void handleSaveSpeed();
void handleSaveLine();
void saveTextSettings();
void loadTextSettings();
void handleWiFiSelect();
void handleSaveWiFi();
void saveWiFiSettings();
bool loadWiFiSettings();
int getTextWidth(const char* text);
void fadeOut();
void fadeIn();

// ============ Шрифт 5x8 — цифры и символы ============
const byte font5x8[][5] PROGMEM = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // пробел
  {0x00, 0x00, 0x58, 0x00, 0x00}, // !
  {0x00, 0x00, 0x00, 0x00, 0x00}, // "
  {0x00, 0x28, 0x7E, 0x28, 0x00}, // #
  {0x00, 0x24, 0x5E, 0x52, 0x00}, // $
  {0x00, 0x60, 0x50, 0x23, 0x00}, // %
  {0x00, 0x36, 0x49, 0x26, 0x00}, // &
  {0x00, 0x40, 0x40, 0x00, 0x00}, // '
  {0x00, 0x3C, 0x40, 0x3C, 0x00}, // (
  {0x00, 0x3C, 0x04, 0x3C, 0x00}, // )
  {0x00, 0x14, 0x3E, 0x14, 0x00}, // *
  {0x00, 0x08, 0x3E, 0x08, 0x00}, // +
  {0x00, 0x00, 0x00, 0x40, 0x40}, // ,
  {0x00, 0x00, 0x08, 0x08, 0x00}, // -
  {0x00, 0x00, 0x00, 0x00, 0x60}, // .
  {0x00, 0x20, 0x10, 0x08, 0x04}, // /
  {0x7e, 0x81, 0x81, 0x81, 0x7e}, // 0
  {0x00, 0x84, 0x82, 0xff, 0x80}, // 1
  {0xe2, 0x91, 0x91, 0x91, 0x8e}, // 2
  {0x42, 0x91, 0x91, 0x91, 0x6e}, // 3
  {0x0f, 0x10, 0x10, 0x10, 0xff}, // 4
  {0x4e, 0x91, 0x91, 0x91, 0x61}, // 5
  {0x7e, 0x91, 0x91, 0x91, 0x62}, // 6
  {0x06, 0x01, 0x01, 0x01, 0xff}, // 7
  {0x76, 0x89, 0x89, 0x89, 0x76}, // 8
  {0x46, 0x89, 0x89, 0x89, 0x7e}, // 9
  {0x00, 0x00, 0x36, 0x00, 0x00}  // :
};

// ============ Шрифт 5x8 — кириллица ============
const byte cyrillic_5x8[][5] PROGMEM = {
  {0xfe, 0x21, 0x21, 0x21, 0xfe}, // А  0
  {0xff, 0x89, 0x89, 0x89, 0x71}, // Б  1
  {0xff, 0x91, 0x91, 0x91, 0x76}, // В  2
  {0xff, 0x01, 0x01, 0x01, 0x03}, // Г  3
  {0xc0, 0x3e, 0x21, 0x3f, 0xc0}, // Д  4
  {0xff, 0x91, 0x91, 0x91, 0x81}, // Е  5
  {0xe3, 0x14, 0xff, 0x14, 0xe3}, // Ж  6
  {0x42, 0x89, 0x89, 0x89, 0x76}, // З  7
  {0xff, 0x30, 0x18, 0x06, 0xff}, // И  8
  {0xfe, 0x60, 0x31, 0x18, 0xfe}, // Й  9
  {0xff, 0x08, 0x08, 0x14, 0xe3}, // К  10
  {0xfc, 0x02, 0x01, 0x01, 0xff}, // Л  11
  {0xff, 0x0e, 0x70, 0x0e, 0xff}, // М  12
  {0xff, 0x10, 0x10, 0x10, 0xff}, // Н  13
  {0x7e, 0x81, 0x81, 0x81, 0x7e}, // О  14
  {0xff, 0x01, 0x01, 0x01, 0xff}, // П  15
  {0xff, 0x11, 0x11, 0x11, 0x0e}, // Р  16
  {0x7e, 0x81, 0x81, 0x81, 0xc3}, // С  17
  {0x03, 0x01, 0xff, 0x01, 0x03}, // Т  18
  {0x47, 0x88, 0x88, 0x88, 0xff}, // У  19
  {0x1e, 0x21, 0xff, 0x21, 0x1e}, // Ф  20
  {0xc7, 0x28, 0x10, 0x28, 0xc7}, // Х  21
  {0x7f, 0x40, 0x40, 0x7f, 0xc0}, // Ц  22
  {0x0f, 0x10, 0x10, 0x10, 0xff}, // Ч  23
  {0xff, 0x80, 0xff, 0x80, 0xff}, // Ш  24
  {0x7f, 0x40, 0x7f, 0x40, 0xff}, // Щ  25
  {0xff, 0x88, 0x88, 0x70, 0xff}, // Э  26
  {0xff, 0x88, 0x88, 0x70, 0xff}, // Ю  27
  {0xc6, 0x39, 0x09, 0x09, 0xff}, // Я  28
  {0xfe, 0x93, 0x92, 0x93, 0x82}, // Ё  29
  {0x42, 0x91, 0x91, 0x91, 0x7e}, // Ы  30
  {0xff, 0x90, 0x90, 0x90, 0x60}, // Ь  31
  {0xff, 0x18, 0x7e, 0x81, 0x7e}, // Ъ  32
};

// ============ XY ============
int XY(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= 8) return -1;
  int tile = x / MATRIX_WIDTH;
  int local_x = x % MATRIX_WIDTH;
  int pos = y * MATRIX_WIDTH + local_x;
  return tile * 64 + pos;
}

// ============ Отрисовка символа ============
void drawChar(char c, int x, uint32_t color) {
  if (c < 32 || c > 126) return;
  int idx = c - 32;
  for (int col = 0; col < 5; col++) {
    byte bits = pgm_read_byte(&font5x8[idx][col]);
    for (int row = 0; row < 8; row++) {
      if (bits & (1 << row)) {
        int px = x + col;
        int py = row;
        if (px >= 0 && px < SCREEN_WIDTH && py < 8) {
          strip.setPixelColor(XY(px, py), color);
        }
      }
    }
  }
}

// ============ Отрисовка кириллического символа ============
void drawCyrillicCharByIndex(int idx, int x, uint32_t color) {
  if (idx < 0 || idx >= 33) return;
  for (int col = 0; col < 5; col++) {
    byte bits = pgm_read_byte(&cyrillic_5x8[idx][col]);
    for (int row = 0; row < 8; row++) {
      if (bits & (1 << row)) {
        int px = x + col;
        int py = row;
        if (px >= 0 && px < SCREEN_WIDTH && py < 8) {
          strip.setPixelColor(XY(px, py), color);
        }
      }
    }
  }
}

// ============ Определение кириллицы ============
int getCyrillicIndex(const char* str, size_t i) {
  if (i >= strlen(str) - 1) return -1;
  unsigned char c1 = str[i];
  unsigned char c2 = str[i + 1];
  int idx = -1;

  if (c1 == 0xD0) {
    Serial.printf("UTF-8: %02X %02X → ", c1, c2);  // Добавьте это
    switch (c2) {
      case 0x90: idx = 0;  break;  // А
      case 0x91: idx = 1;  break;  // Б
      case 0x92: idx = 2;  break;  // В
      case 0x93: idx = 3;  break;  // Г
      case 0x94: idx = 4;  break;  // Д
      case 0x95: idx = 5;  break;  // Е
      case 0x96: idx = 6;  break;  // Ж
      case 0x97: idx = 7;  break;  // З
      case 0x98: idx = 8;  break;  // И
      case 0x99: idx = 9;  break;  // Й
      case 0x9A: idx = 10; break;  // К
      case 0x9B: idx = 11; break;  // Л
      case 0x9C: idx = 12; break;  // М
      case 0x9D: idx = 13; break;  // Н
      case 0x9E: idx = 14; break;  // О
      case 0x9F: idx = 15; break;  // П
      case 0xA0: idx = 16; break;  // Р
      case 0xA1: idx = 17; break;  // С
      case 0xA2: idx = 18; break;  // Т
      case 0xA3: idx = 19; break;  // У
      case 0xA4: idx = 20; break;  // Ф
      case 0xA5: idx = 21; break;  // Х
      case 0xA6: idx = 22; break;  // Ц
      case 0xA7: idx = 23; break;  // Ч
      case 0xA8: idx = 24; break;  // Ш
      case 0xA9: idx = 25; break;  // Щ
      case 0xAA: idx = 26; break;  // Э
      case 0xAB: idx = 27; break;  // Ю
      case 0xAC: idx = 31; break;  // Ь
      case 0xAD: idx = 30; break;  // Ы
      case 0xAE: idx = 32; break;  // Ъ
      case 0xAF: idx = 28; break;  // Я
      case 0x81: idx = 29; break;  // Ё
    }
    Serial.println(idx);  // И это
  }
  return idx;
}

// ============ Ширина текста ============
int getTextWidth(const char* text) {
  if (text == nullptr || strlen(text) == 0) return 0;
  int width = 0;
  size_t i = 0;
  size_t len = strlen(text);
  while (i < len) {
    int idx = getCyrillicIndex(text, i);
    if (idx != -1) { width += 6; i += 2; } else { width += 6; i++; }
  }
  return width;
}

// ============ Отображение текста ============
void showTextFrame() {
  strip.clear();
  const char* text = lines[currentLineIndex];
  uint32_t color = strip.Color(reds[currentLineIndex], greens[currentLineIndex], blues[currentLineIndex]);
  if (color == 0 && strlen(text) > 0) color = strip.Color(255, 180, 50);
  int x = -textOffset;
  size_t i = 0;
  size_t len = strlen(text);
  while (i < len) {
    int idx = getCyrillicIndex(text, i);
    if (idx != -1) {
      drawCyrillicCharByIndex(idx, x, color);
      x += 6;
      i += 2;
    } else {
      drawChar(text[i], x, color);
      x += 6;
      i++;
    }
  }
  strip.show();
}

// ============ Плавность ============
void fadeOut() {
  if (currentBrightness > 0) {
    currentBrightness--;
    strip.setBrightness(currentBrightness);
    strip.show();
  }
}
void fadeIn() {
  if (currentBrightness < brightness) {
    currentBrightness++;
    strip.setBrightness(currentBrightness);
    strip.show();
  }
}

// ============ Загрузка из EEPROM ============
void loadTextSettings() {
  Serial.println("📂 Загружаем настройки из EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  int addr = SETTINGS_ADDR;
  for (int i = 0; i < NUM_LINES; i++) {
    int len = EEPROM.read(addr++);
    if (len > MAX_TEXT_LEN) len = MAX_TEXT_LEN;
    for (int j = 0; j < len; j++) {
      lines[i][j] = EEPROM.read(addr);
      addr++;
    }
    lines[i][len] = '\0';
    Serial.printf("  строка %d: '%s'\n", i, lines[i]);
  }
  addr = COLOR_ADDR;
  for (int i = 0; i < NUM_LINES; i++) {
    reds[i] = EEPROM.read(addr++);
    greens[i] = EEPROM.read(addr++);
    blues[i] = EEPROM.read(addr++);
    Serial.printf("  цвет %d: %d, %d, %d\n", i, reds[i], greens[i], blues[i]);
  }
  brightness = EEPROM.read(BRIGHTNESS_ADDR);
  if (brightness == 0 || brightness > 255) brightness = BRIGHTNESS;
  strip.setBrightness(brightness);
  currentBrightness = brightness;
  SCROLL_DELAY = EEPROM.read(SPEED_ADDR);
  if (SCROLL_DELAY == 0 || SCROLL_DELAY > 500) SCROLL_DELAY = DEFAULT_SCROLL_DELAY;
  EEPROM.end();
  for (int i = 0; i < NUM_LINES; i++) {
    if (reds[i] == 0 && greens[i] == 0 && blues[i] == 0 && strlen(lines[i]) > 0) {
      reds[i] = 255; greens[i] = 180; blues[i] = 50;
    }
  }
  Serial.println("✅ Настройки загружены");
}

// ============ Сохранение в EEPROM ============
void saveTextSettings() {
  Serial.println("📝 Сохраняем настройки в EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  int addr = SETTINGS_ADDR;
  for (int i = 0; i < NUM_LINES; i++) {
    size_t len = strlen(lines[i]);
    EEPROM.write(addr++, len);
    Serial.printf("  строка %d: длина=%d, текст='%s'\n", i, len, lines[i]);
    for (size_t j = 0; j < len; j++) {
      EEPROM.write(addr++, lines[i][j]);
    }
  }
  addr = COLOR_ADDR;
  for (int i = 0; i < NUM_LINES; i++) {
    EEPROM.write(addr++, reds[i]);
    EEPROM.write(addr++, greens[i]);
    EEPROM.write(addr++, blues[i]);
    Serial.printf("  цвет %d: %d, %d, %d\n", i, reds[i], greens[i], blues[i]);
  }
  EEPROM.write(BRIGHTNESS_ADDR, brightness);
  EEPROM.write(SPEED_ADDR, (uint8_t)SCROLL_DELAY);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("✅ EEPROM: сохранено");
}

// ============ Wi-Fi ============
bool loadWiFiSettings() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(WIFI_FLAG_ADDR) != 1) {
    EEPROM.end();
    return false;
  }
  int len = EEPROM.read(WIFI_SSID_ADDR);
  if (len == 0 || len >= MAX_SSID_LEN) {
    EEPROM.end();
    return false;
  }
  for (int i = 0; i < len; i++) savedSSID[i] = EEPROM.read(WIFI_SSID_ADDR + 1 + i);
  savedSSID[len] = '\0';
  len = EEPROM.read(WIFI_PASS_ADDR);
  if (len > MAX_PASS_LEN) len = MAX_PASS_LEN;
  for (int i = 0; i < len; i++) savedPass[i] = EEPROM.read(WIFI_PASS_ADDR + 1 + i);
  savedPass[len] = '\0';
  EEPROM.end();
  return true;
}

void saveWiFiSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(WIFI_FLAG_ADDR, 1);
  int len = strlen(savedSSID);
  EEPROM.write(WIFI_SSID_ADDR, len);
  for (int i = 0; i < len; i++) EEPROM.write(WIFI_SSID_ADDR + 1 + i, savedSSID[i]);
  len = strlen(savedPass);
  EEPROM.write(WIFI_PASS_ADDR, len);
  for (int i = 0; i < len; i++) EEPROM.write(WIFI_PASS_ADDR + 1 + i, savedPass[i]);
  EEPROM.commit();
  EEPROM.end();
}

void handleWiFiSelect() {
  if (WiFi.scanComplete() == -2) WiFi.scanNetworks(true);
  int n = WiFi.scanComplete();
  String html = "<h2>📶 Выберите сеть</h2><form action='/save-wifi' method='post'><select name='ssid'>";
  if (n == -2) html += "<option>Сканирование...</option>";
  else if (n == 0) html += "<option>Нет сетей</option>";
  else for (int i = 0; i < n; i++) html += "<option>" + WiFi.SSID(i) + "</option>";
  html += "</select><br><input type='password' name='pass' placeholder='Пароль'><br>";
  html += "<button>Подключиться</button></form>";
  server.send(200, "text/html", html);
}

void handleSaveWiFi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    server.arg("ssid").toCharArray(savedSSID, MAX_SSID_LEN);
    server.arg("pass").toCharArray(savedPass, MAX_PASS_LEN);
    saveWiFiSettings();
    server.send(200, "text/html", "<h1>✅ Подключаемся...</h1>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(200, "text/html", "<h2>❌ Ошибка</h2>");
  }
}

// ============ Веб-интерфейс ============
void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("Content-Type", "text/html; charset=utf-8");

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Бегущая строка</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
  html += "input, button { padding: 8px; margin: 5px; }";
  html += "input[type='text'] { width: 250px; }";
  html += "input[type='range'] { width: 100px; }";
  html += "button { background: #007BFF; color: white; border: none; padding: 8px 15px; cursor: pointer; }";
  html += "button:hover { background: #0056b3; }";
  html += ".section { margin: 15px 0; padding: 15px; background: white; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }";
  html += "label { font-weight: bold; }";
  html += ".msg { color: green; font-size: 0.9em; }";
  html += "</style></head><body>";
  html += "<h1>🪧 Бегущая строка</h1>";
  html += "<p>IP: <strong>" + localIP.toString() + "</strong></p>";

  html += "<div class='section'>";
  html += "<label>💡 Яркость</label><br>";
  html += "<input type='range' id='brightness' min='0' max='255' value='" + String(brightness) + "' onchange='updateVal(\"bVal\", this.value)'>";
  html += " <span id='bVal'>" + String(brightness) + "</span><br>";
  html += "<button onclick='saveBrightness()'>Сохранить</button>";
  html += "<div id='bMsg' class='msg'></div>";
  html += "</div>";

  html += "<div class='section'>";
  html += "<label>⏱️ Скорость (мин. 5)</label><br>";
  html += "<input type='range' id='speed' min='5' max='500' value='" + String(SCROLL_DELAY) + "' onchange='updateVal(\"sVal\", this.value)'>";
  html += " <span id='sVal'>" + String(SCROLL_DELAY) + "</span><br>";
  html += "<button onclick='saveSpeed()'>Сохранить</button>";
  html += "<div id='sMsg' class='msg'></div>";
  html += "</div>";

  for (int i = 0; i < NUM_LINES; i++) {
    html += "<div class='section'>";
    html += "<label>📜 Строка " + String(i+1) + "</label><br>";
    html += "<input type='text' id='line" + String(i+1) + "' value='" + String(lines[i]) + "'><br>";
    html += "<label>🎨 Цвет (RGB)</label><br>";
    html += "R <input type='range' min='0' max='255' value='" + String(reds[i]) + "' id='r" + String(i+1) + "' onchange='updateVal(\"r" + String(i+1) + "v\", this.value)'>";
    html += " <span id='r" + String(i+1) + "v'>" + String(reds[i]) + "</span><br>";
    html += "G <input type='range' min='0' max='255' value='" + String(greens[i]) + "' id='g" + String(i+1) + "' onchange='updateVal(\"g" + String(i+1) + "v\", this.value)'>";
    html += " <span id='g" + String(i+1) + "v'>" + String(greens[i]) + "</span><br>";
    html += "B <input type='range' min='0' max='255' value='" + String(blues[i]) + "' id='b" + String(i+1) + "' onchange='updateVal(\"b" + String(i+1) + "v\", this.value)'>";
    html += " <span id='b" + String(i+1) + "v'>" + String(blues[i]) + "</span><br>";
    html += "<button onclick='saveLine(" + String(i+1) + ")'>Сохранить</button>";
    html += "<div id='msg" + String(i+1) + "' class='msg'></div>";
    html += "</div>";
  }

  html += "<script>";
  html += "function updateVal(id, val) { document.getElementById(id).textContent = val; }";
  html += "function saveBrightness() { let b = document.getElementById('brightness').value; fetch('/save-brightness?b=' + b); document.getElementById('bMsg').innerHTML = '✔️ Сохранено'; }";
  html += "function saveSpeed() { let s = document.getElementById('speed').value; fetch('/save-speed?s=' + s); document.getElementById('sMsg').innerHTML = '✔️ Сохранено'; }";
  html += "function saveLine(num) {";
  html += "  let form = new FormData();";
  html += "  form.append('num', num);";
  html += "  form.append('text', document.getElementById('line'+num).value);";
  html += "  form.append('r', document.getElementById('r'+num).value);";
  html += "  form.append('g', document.getElementById('g'+num).value);";
  html += "  form.append('b', document.getElementById('b'+num).value);";
  html += "  fetch('/save-line', { method: 'POST', body: form });";
  html += "  document.getElementById('msg'+num).innerHTML = '✔️ Сохранено';";
  html += "}";
  html += "</script>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ============ Обработчики ============
void handleSaveBrightness() {
  if (server.hasArg("b")) {
    int b = server.arg("b").toInt();
    if (b >= 0 && b <= 255) {
      brightness = b;
      strip.setBrightness(brightness);
      currentBrightness = brightness;
      saveTextSettings();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleSaveSpeed() {
  if (server.hasArg("s")) {
    int s = server.arg("s").toInt();
    if (s >= 5 && s <= 500) {
      SCROLL_DELAY = s;
      saveTextSettings();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleSaveLine() {
  Serial.println("📥 ЗАПРОС: /save-line (POST)");

  if (server.hasArg("num") && server.hasArg("text")) {
    Serial.printf("  num: %s\n", server.arg("num").c_str());
    Serial.printf("  text: '%s'\n", server.arg("text").c_str());
    Serial.printf("  r: %s, g: %s, b: %s\n",
      server.hasArg("r") ? server.arg("r").c_str() : "-",
      server.hasArg("g") ? server.arg("g").c_str() : "-",
      server.hasArg("b") ? server.arg("b").c_str() : "-"
    );

    int num = server.arg("num").toInt() - 1;
    if (num >= 0 && num < NUM_LINES) {
      String text = server.arg("text");
      if (text.length() > MAX_TEXT_LEN) text = text.substring(0, MAX_TEXT_LEN);
      text.toCharArray(lines[num], MAX_TEXT_LEN + 1);

      Serial.printf("✅ Сохраняем строку %d: '%s'\n", num, lines[num]);

      int r = server.hasArg("r") ? server.arg("r").toInt() : 0;
      int g = server.hasArg("g") ? server.arg("g").toInt() : 0;
      int b = server.hasArg("b") ? server.arg("b").toInt() : 0;

      if (r == 0 && g == 0 && b == 0 && strlen(lines[num]) > 0) {
        r = 255; g = 180; b = 50;
      }
      reds[num] = r; greens[num] = g; blues[num] = b;

      saveTextSettings();
      Serial.printf("🎨 Цвет: %d, %d, %d\n", r, g, b);
    } else {
      Serial.printf("❌ Неверный номер строки: %d\n", num + 1);
    }
  } else {
    Serial.println("❌ Аргументы num или text не получены");
    if (server.hasArg("num")) Serial.println("  num: есть");
    if (server.hasArg("text")) Serial.println("  text: есть");
    else Serial.println("  text: отсутствует!");
  }
  server.send(200, "text/plain", "OK");
}

// ============ setup и loop ============
void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.show();

  loadTextSettings();
  Serial.println("\n🌐 ИНИЦИАЛИЗАЦИЯ Wi-Fi...");
  localIP = IPAddress(0, 0, 0, 0);
  shouldServeWiFi = false;
  bool connected = false;

   // 1. Подключение к сохранённой сети
  if (loadWiFiSettings()) {
    Serial.printf("🔐 Подключаемся к сохранённой сети: %s\n", savedSSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID, savedPass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      localIP = WiFi.localIP();
      Serial.println("\n✅ Подключено: " + localIP.toString());
      connected = true;
    } else {
      Serial.println("\n❌ Не удалось подключиться: " + String(savedSSID));
    }
  }

  // 2. Подключение к известным сетям
  if (!connected) {
    Serial.println("🔍 Пытаемся подключиться к Root_HOME / Root_home");
    WiFi.mode(WIFI_STA);
    for (int i = 0; i < NUM_KNOWN_SSIDS; i++) {
      const char* ssid = KNOWN_SSIDS[i];
      Serial.printf("📡 Попытка: %s\n", ssid);
      WiFi.begin(ssid, COMMON_PASSWORD);
      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 6000) {
        delay(500);
        Serial.print(".");
      }
      if (WiFi.status() == WL_CONNECTED) {
        localIP = WiFi.localIP();
        Serial.println("\n✅ Успешно: " + String(ssid));
        connected = true;
        strcpy(savedSSID, ssid);
        strcpy(savedPass, COMMON_PASSWORD);
        saveWiFiSettings();
        break;
      } else {
        Serial.println(" — не удалось");
      }
    }
  }

  // 3. Запуск точки доступа, если ни к одной сети не подключились
  if (!connected) {
    Serial.println("🔧 Ни одна сеть не найдена. Запускаем точку доступа.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    localIP = WiFi.softAPIP();
    shouldServeWiFi = true;
    Serial.println("🎉 Точка доступа запущена: " + String(AP_SSID));
    Serial.println("🌐 IP: " + localIP.toString());
    Serial.println("📱 Подключитесь к Wi-Fi: " + String(AP_SSID));
    Serial.println("🔑 Пароль: " + String(AP_PASS));
  }

  // === Запуск веб-сервера ===
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save-brightness", HTTP_GET, handleSaveBrightness);
  server.on("/save-speed", HTTP_GET, handleSaveSpeed);
  server.on("/save-line", HTTP_POST, handleSaveLine);

  if (shouldServeWiFi) {
    server.on("/", HTTP_GET, handleWiFiSelect);
    server.on("/save-wifi", HTTP_POST, handleSaveWiFi);
  }

  server.begin();

  textOffset = -SCREEN_WIDTH;
  currentBrightness = brightness;

  Serial.println("\n🚀 Устройство готово!");
  Serial.println("🌐 Открой в браузере: http://" + localIP.toString());
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  switch (displayState) {
    case SCROLLING: {
      if (now - lastScroll >= SCROLL_DELAY) {
        showTextFrame();
        textOffset++;
        int width = getTextWidth(lines[currentLineIndex]);
        if (textOffset > width + 2) {
          displayState = FADE_OUT;
          lastChange = now;
        }
        lastScroll = now;
      }
      break;
    }

    case FADE_OUT: {
      fadeOut();
      if (currentBrightness == 0 || now - lastChange > 2) {
        displayState = CHANGE_LINE;
        lastChange = now;
      }
      delay(2);
      break;
    }

    case CHANGE_LINE: {
      currentLineIndex = (currentLineIndex + 1) % NUM_LINES;
      int attempts = 0;
      while (strlen(lines[currentLineIndex]) == 0 && attempts < NUM_LINES) {
        currentLineIndex = (currentLineIndex + 1) % NUM_LINES;
        attempts++;
      }
      textOffset = -SCREEN_WIDTH;
      displayState = FADE_IN;
      lastChange = now;
      break;
    }

    case FADE_IN: {
      fadeIn();
      if (currentBrightness >= brightness || now - lastChange > 2) {
        displayState = SCROLLING;
        lastScroll = now;
      }
      delay(2);
      break;
    }
  }
  delay(1);
}

