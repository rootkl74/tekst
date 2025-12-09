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

const char* AP_SSID = "ClockAP";
const char* AP_PASS = "12345678";
const unsigned long WIFI_TIMEOUT = 10000;

// Адреса EEPROM
const int WIFI_FLAG_ADDR = 0;
const int WIFI_SSID_ADDR = 1;
const int WIFI_PASS_ADDR = 33;
const int SETTINGS_ADDR  = 97;

int COLOR_ADDR = SETTINGS_ADDR + 4 + NUM_LINES * (1 + MAX_TEXT_LEN);
int SPEED_ADDR   = COLOR_ADDR + NUM_LINES * 3;

const int MAX_SSID_LEN = 32;
const int MAX_PASS_LEN = 64;

char savedSSID[MAX_SSID_LEN];
char savedPass[MAX_PASS_LEN];
// =====================================

// Инициализация
Adafruit_NeoPixel strip(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(WEB_PORT);

// Переменные строк
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
void handleSettings();
void saveTextSettings();
void loadTextSettings();
void handleWiFiSelect();
void handleSaveWiFi();
void saveWiFiSettings();
bool loadWiFiSettings();
int getTextWidth(const char* text);
void fadeOut();
void fadeIn();

// ============ Шрифт 5x8 — цифры и символы (32–126) ============
const byte font5x8[][5] PROGMEM = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // пробел  (32)
  {0x00, 0x00, 0x58, 0x00, 0x00}, // ! (33)
  {0x00, 0x00, 0x00, 0x00, 0x00}, // " (34)
  {0x00, 0x28, 0x7E, 0x28, 0x00}, // # (35)
  {0x00, 0x24, 0x5E, 0x52, 0x00}, // $ (36)
  {0x00, 0x60, 0x50, 0x23, 0x00}, // % (37)
  {0x00, 0x36, 0x49, 0x26, 0x00}, // & (38)
  {0x00, 0x40, 0x40, 0x00, 0x00}, // ' (39)
  {0x00, 0x00, 0x3C, 0x40, 0x3C}, // ( (40)
  {0x00, 0x00, 0x3C, 0x04, 0x3C}, // ) (41)
  {0x00, 0x14, 0x3E, 0x14, 0x00}, // * (42)
  {0x00, 0x08, 0x3E, 0x08, 0x00}, // + (43)
  {0x00, 0x00, 0x00, 0x40, 0x40}, // , (44)
  {0x00, 0x00, 0x08, 0x08, 0x00}, // - (45)
  {0x00, 0x00, 0x00, 0x00, 0x60}, // . (46)
  {0x00, 0x20, 0x10, 0x08, 0x04}, // / (47)
  {0xff, 0x81, 0x81, 0x81, 0xff}, // 0 (48)
  {0x84, 0x82, 0xff, 0x80, 0x80}, // 1 (49)
  {0xf1, 0x91, 0x91, 0x91, 0x9f}, // 2 (50)
  {0x89, 0x89, 0x89, 0x89, 0xff}, // 3 (51)
  {0x1c, 0x12, 0x11, 0xff, 0x10}, // 4 (52)
  {0x9f, 0x91, 0x91, 0x91, 0xf1}, // 5 (53)
  {0xff, 0x91, 0x91, 0x91, 0xf1}, // 6 (54)
  {0xc1, 0x21, 0x11, 0x09, 0x07}, // 7 (55)
  {0xff, 0x89, 0x89, 0x89, 0xff}, // 8 (56)
  {0x8f, 0x89, 0x89, 0x89, 0xff}, // 9 (57)
  {0x00, 0x00, 0x36, 0x00, 0x00}  // : (58)
};

// ============ Шрифт 5x8 — кириллица ============
const byte cyrillic_5x8[][5] PROGMEM = {
  {0xfe, 0x21, 0x21, 0x21, 0xfe}, // А
  {0xff, 0x89, 0x89, 0x89, 0x71}, // Б
  {0xff, 0x89, 0x89, 0x89, 0x76}, // В
  {0xff, 0x01, 0x01, 0x01, 0x03}, // Г
  {0xc0, 0x3e, 0x21, 0x3f, 0xc0}, // Д
  {0xff, 0x91, 0x91, 0x91, 0x81}, // Е
  {0xe3, 0x14, 0xff, 0x14, 0xe3}, // Ж
  {0x42, 0x89, 0x89, 0x89, 0x76}, // З
  {0xff, 0x30, 0x18, 0x06, 0xff}, // И
  {0xfe, 0x60, 0x31, 0x18, 0xfe}, // Й
  {0xff, 0x08, 0x08, 0x14, 0xe3}, // К
  {0xfc, 0x02, 0x01, 0x01, 0xff}, // Л
  {0xff, 0x0e, 0x70, 0x0e, 0xff}, // М
  {0xff, 0x10, 0x10, 0x10, 0xff}, // Н
  {0x7e, 0x81, 0x81, 0x81, 0x7e}, // О
  {0xff, 0x01, 0x01, 0x01, 0xff}, // П
  {0xff, 0x11, 0x11, 0x11, 0x0e}, // Р
  {0x7e, 0x81, 0x81, 0x81, 0xc3}, // С
  {0x03, 0x01, 0xff, 0x01, 0x03}, // Т
  {0x47, 0x88, 0x88, 0x88, 0xff}, // У
  {0x1e, 0x21, 0xff, 0x21, 0x1e}, // Ф
  {0xc7, 0x28, 0x10, 0x28, 0xc7}, // Х
  {0x7f, 0x40, 0x40, 0x7f, 0xc0}, // Ц
  {0x0f, 0x10, 0x10, 0x10, 0xff}, // Ч
  {0xff, 0x80, 0xff, 0x80, 0xff}, // Ш
  {0x7f, 0x40, 0x7f, 0x40, 0xff}, // Щ
  {0x42, 0x91, 0x91, 0x91, 0xff}, // Э
  {0xff, 0x10, 0x7e, 0x81, 0x7e}, // Ю
  {0xc6, 0x39, 0x09, 0x09, 0xff}, // Я
  {0xfe, 0x93, 0x92, 0x93, 0x82}, // Ё
  {0x01, 0xff, 0x88, 0x88, 0x70}, // Ъ
  {0xff, 0x88, 0x88, 0x88, 0x70}, // Ь
  {0xff, 0x88, 0x70, 0x00, 0xff}, // ы
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
  int idx = (c - 32);
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

void drawCyrillicCharByIndex(int idx, int x, uint32_t color) {
  if (idx < 0 || idx >= 32) return;
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
  unsigned char c = str[i];
  unsigned char c2 = str[i + 1];

  if (c == 0xD0) {
    if (c2 == 0x81) return 28; // Ё
    if (c2 >= 0x90 && c2 <= 0x9F) return c2 - 0x90;
    if (c2 >= 0xA0 && c2 <= 0xBF) {
      if (c2 == 0xAC) return 29; // Ъ
      if (c2 == 0x9C) return 30; // Ь
      return c2 - 0xA0 + 16;
    }
  }
  if (c == 0xD1) {
    if (c2 == 0x91) return 28; // ё
    if (c2 >= 0x80 && c2 <= 0x8F) return c2 - 0x80 + 32;
    if (c2 == 0x8B) return 31; // ы
  }
  return -1;
}

// ============ Ширина текста ============
int getTextWidth(const char* text) {
  int width = 0;
  size_t len = strlen(text);
  for (size_t i = 0; i < len; i++) {
    int idx = getCyrillicIndex(text, i);
    if (idx != -1) {
      width += 6;
      i++;
    } else {
      width += 6;
    }
  }
  return width;
}

// ============ Показ текста ============
void showTextFrame() {
  strip.clear();
  const char* text = lines[currentLineIndex];
  uint32_t color = strip.Color(reds[currentLineIndex], greens[currentLineIndex], blues[currentLineIndex]);
  int x = -textOffset;

  size_t len = strlen(text);
  for (size_t i = 0; i < len; i++) {
    int idx = getCyrillicIndex(text, i);
    if (idx != -1) {
      drawCyrillicCharByIndex(idx, x, color);
      x += 6;
      i++;
    } else {
      drawChar(text[i], x, color);
      x += 6;
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

// ============ Загрузка настроек из EEPROM ============
void loadTextSettings() {
  int addr = SETTINGS_ADDR;
  brightness = EEPROM.read(addr + 3);
  if (brightness == 0) brightness = BRIGHTNESS;
  strip.setBrightness(brightness);
  currentBrightness = brightness;

  for (int i = 0; i < NUM_LINES; i++) {
    int len = EEPROM.read(addr + 4 + i * (1 + MAX_TEXT_LEN));
    for (int j = 0; j < len; j++) {
      lines[i][j] = EEPROM.read(addr + 4 + i * (1 + MAX_TEXT_LEN) + 1 + j);
    }
    lines[i][len] = '\0';
  }

  addr = COLOR_ADDR;
  for (int i = 0; i < NUM_LINES; i++) {
    reds[i] = EEPROM.read(addr + 3*i);
    greens[i] = EEPROM.read(addr + 3*i + 1);
    blues[i] = EEPROM.read(addr + 3*i + 2);
  }

  SCROLL_DELAY = EEPROM.read(SPEED_ADDR);
  if (SCROLL_DELAY == 0 || SCROLL_DELAY > 500) SCROLL_DELAY = DEFAULT_SCROLL_DELAY;

  if (strlen(lines[0]) == 0) {
    strcpy(lines[0], "Привет!");
    strcpy(lines[1], "Тест 123");
    saveTextSettings();
  }

  Serial.println("📂 Настройки загружены из EEPROM");
}

// ============ Сохранение настроек в EEPROM ============
void saveTextSettings() {
  Serial.println("💾 Начало сохранения в EEPROM...");

  int addr = SETTINGS_ADDR;

  // Первые три байта — не используются, но оставим для совместимости
  EEPROM.write(addr++, reds[0]);
  EEPROM.write(addr++, greens[0]);
  EEPROM.write(addr++, blues[0]);
  EEPROM.write(addr++, brightness);

  for (int i = 0; i < NUM_LINES; i++) {
    size_t len = strlen(lines[i]);
    EEPROM.write(addr++, len);
    for (size_t j = 0; j < len; j++) {
      EEPROM.write(addr++, lines[i][j]);
    }
  }

  addr = COLOR_ADDR;
  for (int i = 0; i < NUM_LINES; i++) {
    EEPROM.write(addr++, reds[i]);
    EEPROM.write(addr++, greens[i]);
    EEPROM.write(addr++, blues[i]);
  }

  EEPROM.write(SPEED_ADDR, (uint8_t)SCROLL_DELAY);

  bool result = EEPROM.commit();
  if (result) {
    Serial.println("✅ EEPROM: Сохранено успешно!");
  } else {
    Serial.println("❌ EEPROM: Ошибка записи!");
  }
}

// ============ Wi-Fi ============
bool loadWiFiSettings() {
  if (EEPROM.read(WIFI_FLAG_ADDR) != 1) return false;
  int len = EEPROM.read(WIFI_SSID_ADDR);
  if (len == 0 || len >= MAX_SSID_LEN) return false;
  for (int i = 0; i < len; i++) {
    savedSSID[i] = EEPROM.read(WIFI_SSID_ADDR + 1 + i);
  }
  savedSSID[len] = '\0';

  len = EEPROM.read(WIFI_PASS_ADDR);
  if (len > MAX_PASS_LEN) len = MAX_PASS_LEN;
  for (int i = 0; i < len; i++) {
    savedPass[i] = EEPROM.read(WIFI_PASS_ADDR + 1 + i);
  }
  savedPass[len] = '\0';
  return true;
}

void saveWiFiSettings() {
  EEPROM.write(WIFI_FLAG_ADDR, 1);
  int len = strlen(savedSSID);
  EEPROM.write(WIFI_SSID_ADDR, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(WIFI_SSID_ADDR + 1 + i, savedSSID[i]);
  }
  len = strlen(savedPass);
  EEPROM.write(WIFI_PASS_ADDR, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(WIFI_PASS_ADDR + 1 + i, savedPass[i]);
  }
  EEPROM.commit();
}

// ============ Веб-интерфейс с кнопкой "Сохранить" ============
void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Бегущая строка</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
  html += "input, select, button { padding: 8px; margin: 5px; }";
  html += "input[type='text'] { width: 250px; }";
  html += "input[type='number'], input[type='range'] { width: 100px; }";
  html += "button { background: #007BFF; color: white; border: none; padding: 10px 20px; cursor: pointer; font-size: 16px; }";
  html += "button:hover { background: #0056b3; }";
  html += ".section { margin: 15px 0; padding: 15px; background: white; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }";
  html += ".color-inputs { margin-top: 8px; }";
  html += "label { font-weight: bold; display: block; margin: 5px 0; }";
  html += "</style></head><body>";
  html += "<h1>🪧 Бегущая строка</h1>";
  html += "<p>IP: <strong>";
  html += localIP.toString();
  html += "</strong></p>";

  html += "<form method='GET' action='/settings'>";

  // Яркость
  html += "<div class='section'>";
  html += "<label>💡 Яркость</label>";
  html += "<input type='range' name='brightness' min='0' max='255' value='";
  html += brightness;
  html += "' onchange='document.getElementById(\"brightnessValue\").textContent=this.value' />";
  html += "<span id='brightnessValue'>"; html += brightness; html += "</span>";
  html += "</div>";

  // Скорость
  html += "<div class='section'>";
  html += "<label>⏱️ Скорость</label>";
  html += "<input type='range' name='speed' min='50' max='500' value='";
  html += SCROLL_DELAY;
  html += "' onchange='document.getElementById(\"speedValue\").textContent=this.value' />";
  html += "<span id='speedValue'>"; html += SCROLL_DELAY; html += "</span>";
  html += "</div>";

  // Строки
  for (int i = 0; i < NUM_LINES; i++) {
    html += "<div class='section'>";
    html += "<label>📜 Строка ";
    html += (i + 1);
    html += "</label>";
    html += "<input type='text' name='line";
    html += (i + 1);
    html += "' value='";
    html += String(lines[i]);
    html += "' />";
    html += "<div class='color-inputs'>";
    html += "R<input type='number' name='r";
    html += (i + 1);
    html += "' value='";
    html += reds[i];
    html += "' min='0' max='255' />";
    html += " G<input type='number' name='g";
    html += (i + 1);
    html += "' value='";
    html += greens[i];
    html += "' min='0' max='255' />";
    html += " B<input type='number' name='b";
    html += (i + 1);
    html += "' value='";
    html += blues[i];
    html += "' min='0' max='255' />";
    html += "</div>";
    html += "</div>";
  }

  html += "<button type='submit'>✅ Сохранить настройки</button>";
  html += "</form>";

  html += "<script>";
  html += "document.querySelector('form').onsubmit = function() {";
  html += "  document.body.innerHTML = '<h2 style=\"color:green;\">✔️ Сохранено!</h2><p>Перезагрузка...</p>';";
  html += "  setTimeout(() => location.reload(), 1000);";
  html += "};";
  html += "</script>";

  server.send(200, "text/html", html);
}

// ============ Обработка сохранения ============
void handleSettings() {
  bool changed = false;

  if (server.hasArg("brightness")) {
    int b = server.arg("brightness").toInt();
    if (b >= 0 && b <= 255) {
      brightness = b;
      strip.setBrightness(brightness);
      currentBrightness = brightness;
      changed = true;
    }
  }

  if (server.hasArg("speed")) {
    int s = server.arg("speed").toInt();
    if (s >= 50 && s <= 500) {
      SCROLL_DELAY = s;
      changed = true;
    }
  }

  for (int i = 1; i <= NUM_LINES; i++) {
    String argName = "line" + String(i);
    if (server.hasArg(argName)) {
      String newText = server.arg(argName);
      if (newText.length() > MAX_TEXT_LEN) newText = newText.substring(0, MAX_TEXT_LEN);
      newText.toCharArray(lines[i-1], MAX_TEXT_LEN + 1);
      changed = true;
    }
    if (server.hasArg("r" + String(i))) {
      int r = server.arg("r" + String(i)).toInt();
      reds[i-1] = constrain(r, 0, 255);
      changed = true;
    }
    if (server.hasArg("g" + String(i))) {
      int g = server.arg("g" + String(i)).toInt();
      greens[i-1] = constrain(g, 0, 255);
      changed = true;
    }
    if (server.hasArg("b" + String(i))) {
      int b = server.arg("b" + String(i)).toInt();
      blues[i-1] = constrain(b, 0, 255);
      changed = true;
    }
  }

  if (changed) {
    saveTextSettings();
  }

  server.send(200, "text/html", 
    "<!DOCTYPE html><html><body style='font-family:Arial;text-align:center;padding:20px;'>"
    "<h2 style='color:green;'>✔️ Сохранено!</h2>"
    "<script>setTimeout(() => location.href='/', 1000);</script>"
    "</body></html>");
}

// ============ Wi-Fi ============
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

// ============ setup и loop ============
void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.show();
  EEPROM.begin(EEPROM_SIZE);

  loadTextSettings();

  if (loadWiFiSettings()) {
    WiFi.begin(savedSSID, savedPass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) {
      delay(500);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    localIP = WiFi.localIP();
  } else {
    WiFi.softAP(AP_SSID, AP_PASS);
    localIP = WiFi.softAPIP();
    shouldServeWiFi = true;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_GET, handleSettings);
  if (shouldServeWiFi) {
    server.on("/", HTTP_GET, handleWiFiSelect);
    server.on("/save-wifi", HTTP_POST, handleSaveWiFi);
  }
  server.begin();

  textOffset = -SCREEN_WIDTH;
  currentBrightness = brightness;

  Serial.println("✅ Устройство готово");
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  switch (displayState) {
    case SCROLLING:
      if (now - lastScroll >= SCROLL_DELAY) {
        showTextFrame();
        textOffset++;
        if (textOffset > getTextWidth(lines[currentLineIndex]) + 2) {
          displayState = FADE_OUT;
          lastChange = now;
        }
        lastScroll = now;
      }
      break;
    case FADE_OUT:
      fadeOut();
      if (currentBrightness == 0 || now - lastChange > 2) {
        displayState = CHANGE_LINE;
        lastChange = now;
      }
      delay(2);
      break;
    case CHANGE_LINE:
      currentLineIndex = (currentLineIndex + 1) % NUM_LINES;
      textOffset = -SCREEN_WIDTH;
      displayState = FADE_IN;
      lastChange = now;
      break;
    case FADE_IN:
      fadeIn();
      if (currentBrightness >= brightness || now - lastChange > 2) {
        displayState = SCROLLING;
        lastScroll = now;
      }
      delay(2);
      break;
  }
  delay(1);
}
