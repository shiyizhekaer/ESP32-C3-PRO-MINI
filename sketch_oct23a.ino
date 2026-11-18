#include <GxEPD2_BW.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h> 
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <esp_wifi.h>
#include <esp32FOTA.hpp> // 🔔 引入 OTA 库

// ---------------- OTA 配置 ----------------
#define CURRENT_VERSION "1.0.1" // 每次发布新固件时，记得在这里和 version.json 里同步修改
const char* MANIFEST_URL = "https://github.com/shiyizhekaer/ESP32-C3-PRO-MINI/releases/latest/download/version.json";
// 实例化 FOTA 对象
esp32FOTA fota("esp32-fota-http", CURRENT_VERSION, false, false);

// ---------------- 屏幕引脚定义 ----------------
#define EPD_SCK  7
#define EPD_SDA  1
#define EPD_RST  2
#define EPD_DC   10
#define EPD_CS   20
#define EPD_BUSY 21

// ---------------- 温湿度传感器引脚定义 ----------------
#define I2C_SCL  5
#define I2C_SDA  6

// ---------------- 按钮引脚定义 ----------------
#define BOOT_BTN 0

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
Adafruit_AHTX0 aht;
WiFiManager wifiManager;

// ---------------- 全局变量 ----------------
float inTemp = 0.0, inHum = 0.0;
float outTemp = 0.0;
float outHum = 0.0; 
String weatherText = "未知";
int currentWeatherCode = -1; 
int currentMinute = -1;
int currentHour = -1;
int lastWeatherUpdate = -1;
bool isMaintenanceDone = false;
bool wifiConnected = false; 

float currentLat = 23.18; 
float currentLon = 113.49;
String currentCity = "定位中...";
bool locationFetched = false;

unsigned long lastWiFiCheck = 0; 
unsigned long lastFotaCheck = 0; // 🔔 OTA 检查计时器

// ---------------- 农历算法常量与函数 ----------------
const int yearInfo[] = {
    0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2,
    0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977,
    0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970,
    0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950,
    0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557,
    0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0,
    0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0,
    0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b6a0,0x195a6,
    0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570,
    0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x05ac0,0x0ab60,0x096d5,0x092e0,
    0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5,
    0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930,
    0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530,
    0x05aa0,0x076a3,0x096d0,0x04afb,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45,
    0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0
};
const char* nStr1[] = {"日","一","二","三","四","五","六","七","八","九","十"};
const char* nStr2[] = {"初","十","廿","卅"};
const char* monthName[] = {"正","二","三","四","五","六","七","八","九","十","冬","腊"};

int yearDays(int y) { int i, sum = 348; for (i = 0x8000; i > 0x8; i >>= 1) sum += (yearInfo[y - 1900] & i) ? 1 : 0; return (sum + leapDays(y)); }
int leapDays(int y) { if (leapMonth(y)) return ((yearInfo[y - 1900] & 0x10000) ? 30 : 29); return 0; }
int leapMonth(int y) { return (yearInfo[y - 1900] & 0xf); }
int monthDays(int y, int m) { return ((yearInfo[y - 1900] & (0x10000 >> m)) ? 30 : 29); }

String getLunarDate(int year, int month, int day) {
  int offset = 0, i, temp = 0;
  for (i = 1900; i < year; i++) offset += yearDays(i);
  int leap = leapMonth(year);
  for (i = 1; i < month; i++) {
    offset += monthDays(year, i);
    if (leap > 0 && i == leap) offset += leapDays(year);
  }
  offset += day;
  int y = 1900;
  while (offset > yearDays(y)) { offset -= yearDays(y); y++; }
  leap = leapMonth(y);
  int isLeap = 0;
  for (i = 1; i < 13 && offset > 0; i++) {
    if (leap > 0 && i == (leap + 1) && isLeap == 0) { --i; isLeap = 1; temp = leapDays(y); } 
    else { temp = monthDays(y, i); }
    if (isLeap == 1 && i == (leap + 1)) isLeap = 0;
    offset -= temp;
  }
  if (offset == 0 && leap > 0 && i == leap + 1) {
    if (isLeap == 1) isLeap = 0; else { isLeap = 1; --i; }
  }
  if (offset < 0) { offset += temp; --i; }
  int lMonth = i; int lDay = offset + 1;
  String res = "农历";
  res += monthName[lMonth - 1];
  res += "月";
  if (lDay <= 10) res += nStr2[0] + String(nStr1[lDay % 10 == 0 ? 10 : lDay % 10]);
  else if (lDay < 20) res += "十" + String(nStr1[lDay % 10 == 0 ? 10 : lDay % 10]);
  else if (lDay == 20) res += "二十";
  else if (lDay < 30) res += "廿" + String(nStr1[lDay % 10 == 0 ? 10 : lDay % 10]);
  else if (lDay == 30) res += "三十";
  return res;
}

void connectWiFi_WebPortal();
void getLocationByIP();
void checkWiFi(); 
void fetchWeatherAndTime();
void drawFullUI();
void drawPartialMinute();
void drawPartialSensor();
void dailyMaintenance();
void drawWeatherIcon(int x, int y, int code, bool isNight); 
void drawMiniCalendar(int year, int month, int today); 
void drawSetupScreen(String msg);
void drawConnectingScreen(); 
void configModeCallback(WiFiManager *myWiFiManager); 

int wwoToWmo(int wwo) {
  if (wwo == 113) return 0;
  if (wwo == 116 || wwo == 119 || wwo == 122) return 2;
  if (wwo == 143 || wwo == 248 || wwo == 260) return 3;
  if ((wwo >= 176 && wwo <= 185) || (wwo >= 263 && wwo <= 284) ||
      (wwo >= 293 && wwo <= 311) || (wwo >= 353 && wwo <= 359)) return 60;
  if ((wwo >= 179 && wwo <= 182) || (wwo >= 227 && wwo <= 230) ||
      (wwo >= 317 && wwo <= 338) || (wwo >= 368 && wwo <= 377)) return 73;
  if (wwo >= 386 && wwo <= 395) return 95;
  return 3;
}

String getWwoTextCN(int wwo) {
  if (wwo == 113) return "晴";
  if (wwo == 116) return "多云";
  if (wwo == 119 || wwo == 122) return "阴";
  if (wwo == 143 || wwo == 248 || wwo == 260) return "雾";
  if (wwo == 176) return "局部有雨";
  if (wwo == 179) return "局部有雪";
  if (wwo == 182 || wwo == 185 || wwo == 281 || wwo == 284) return "冻雨";
  if (wwo == 200) return "雷阵雨";
  if (wwo == 227 || wwo == 230) return "暴风雪";
  if (wwo == 263 || wwo == 266) return "毛毛雨";
  if (wwo == 293 || wwo == 296) return "小雨";
  if (wwo == 299 || wwo == 302) return "中雨";
  if (wwo == 305 || wwo == 308) return "大雨";
  if (wwo == 311 || wwo == 314) return "冻雨";
  if (wwo == 317 || wwo == 320) return "雨夹雪";
  if (wwo == 323 || wwo == 326) return "小雪";
  if (wwo == 329 || wwo == 332) return "中雪";
  if (wwo == 335 || wwo == 338) return "大雪";
  if (wwo == 350) return "冰粒";
  if (wwo == 353) return "小阵雨";
  if (wwo == 356) return "中阵雨";
  if (wwo == 359) return "暴雨";
  if (wwo == 362 || wwo == 365) return "阵雨夹雪";
  if (wwo == 368 || wwo == 371) return "阵雪";
  if (wwo == 374 || wwo == 377) return "冰粒";
  if (wwo == 386 || wwo == 389) return "雷阵雨";
  if (wwo == 392 || wwo == 395) return "雷阵雪";
  return "未知";
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(EPD_RST, OUTPUT);
  digitalWrite(EPD_RST, HIGH);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  SPI.begin(EPD_SCK, -1, EPD_SDA, EPD_CS);
  display.init(115200, true, 50, false); 
  display.setRotation(0);
  
  u8g2Fonts.begin(display);
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setFontDirection(0);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!aht.begin()) {
    Serial.println("AHT20 初始化失败！");
  }

  connectWiFi_WebPortal();

  if (wifiConnected) {
    // 设置 OTA 更新检查地址
    fota.setManifestURL(MANIFEST_URL);

    esp_err_t pwr_err = esp_wifi_set_max_tx_power(34);  // 8.5dBm
    Serial.printf("set_max_tx_power(8.5dBm) -> %s\n",
                  pwr_err == ESP_OK ? "OK" : esp_err_to_name(pwr_err));

    getLocationByIP(); 
    fetchWeatherAndTime();

    if (outTemp == 0.0 && outHum == 0.0) {
      Serial.println("⚠ 首次天气获取失败，用默认功率(20dBm)重试...");
      esp_wifi_set_max_tx_power(80);
      delay(1000);
      fetchWeatherAndTime();
      esp_err_t e = esp_wifi_set_max_tx_power(34);
      Serial.printf("恢复低功率(8.5dBm) -> %s\n", esp_err_to_name(e));
    }
  }

  struct tm timeinfo;
  if (wifiConnected) {
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) { delay(1000); retry++; }
    currentHour = timeinfo.tm_hour;
    currentMinute = timeinfo.tm_min;
  } else {
    currentHour = 12;
    currentMinute = 0;
  }

  drawFullUI();
}

void loop() {
  if (digitalRead(BOOT_BTN) == LOW) {
    Serial.println("检测到 BOOT 键按下，请保持 5 秒...");
    delay(3000); 
    if (digitalRead(BOOT_BTN) == LOW) {
      Serial.println("长按确认！清除 WiFi 配置并重启...");
      wifiManager.resetSettings(); 
      delay(500);
      ESP.restart(); 
    }
  }

  checkWiFi();

  // 🔔 OTA 定时检查逻辑 (目前设为每 1 小时检查一次，方便测试，可改为 24 * 60 * 60 * 1000UL)
  if (wifiConnected && (millis() - lastFotaCheck > 1 * 60 * 60 * 1000UL || lastFotaCheck == 0)) {
    lastFotaCheck = millis();
    Serial.println("正在检查云端固件更新...");
    
    // execHTTPcheck() 返回 true 表示发现了新版本
    if (fota.execHTTPcheck()) {
      Serial.println("发现新版本，开始下载并更新！请勿断电...");
      
      // 更新前，可以把屏幕显示清空并提示更新中
      display.setFullWindow();
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
        u8g2Fonts.setCursor(20, 50);
        u8g2Fonts.print("发现新固件，正在更新中...");
        u8g2Fonts.setCursor(20, 80);
        u8g2Fonts.print("请勿断电！");
      } while (display.nextPage());
      
      // 开始下载并刷写固件，下载完毕后设备会自动重启
      fota.execOTA(); 
    } else {
      Serial.println("当前已是最新版本。");
    }
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int nowMinute = timeinfo.tm_min;
    int nowHour = timeinfo.tm_hour;

    if (nowHour == 3 && nowMinute == 0 && !isMaintenanceDone) {
      dailyMaintenance();
      isMaintenanceDone = true;
    }
    if (nowHour == 0 && nowMinute == 0) {
      isMaintenanceDone = false;
    }

    if (nowMinute % 30 == 0 && nowMinute != lastWeatherUpdate) {
      if (WiFi.status() == WL_CONNECTED) {
        fetchWeatherAndTime();
        drawFullUI();
      }
      lastWeatherUpdate = nowMinute;
    }
    else if (nowMinute != currentMinute) {
      currentMinute = nowMinute;
      currentHour = nowHour;
      
      drawPartialMinute();
      delay(600); 
      drawPartialSensor();
    }
  }
  delay(1000); 
}

void getLocationByIP() {
  Serial.println("----------------------------------------");
  Serial.println("正在通过 ip9.com.cn 获取 IP 定位...");

  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  if (!http.begin(client, "https://ip9.com.cn/get")) {
    Serial.println("❌ 定位 http.begin() 失败");
    return;
  }
  http.setTimeout(10000);

  int httpCode = http.GET();
  Serial.printf("📡 定位 HTTP 响应码: %d\n", httpCode);

  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("📦 定位返回: " + payload);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      int ret = doc["ret"];
      if (ret == 200) {
        currentCity = doc["data"]["city"].as<String>();
        currentLat = doc["data"]["lat"].as<float>();
        currentLon = doc["data"]["lng"].as<float>();
        locationFetched = true;
        Serial.printf("✅ 定位成功: %s (经度: %.4f, 纬度: %.4f)\n", 
                      currentCity.c_str(), currentLon, currentLat);
      } else {
        Serial.printf("❌ 定位业务失败, ret: %d\n", ret);
      }
    } else {
      Serial.print("❌ JSON 解析失败: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.println("❌ 定位 HTTP 请求失败");
  }
  http.end();
}

void connectWiFi_WebPortal() {
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setCustomHeadElement("<meta charset=\"UTF-8\">");
  wifiManager.setConfigPortalTimeout(180);
  drawConnectingScreen();

  if (!wifiManager.autoConnect("MyClock")) {
    wifiConnected = false;
  } else {
    wifiConnected = true;
    delay(2000); 
  }
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("进入配网模式...");
  drawSetupScreen("请连接 WiFi 热点 [MyClock]");
}

void drawConnectingScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_logisoso24_tn);
    u8g2Fonts.setCursor(20, 40);
    u8g2Fonts.print("Connecting...");

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(20, 100);
    u8g2Fonts.print("系统启动，正在连接网络...");
    u8g2Fonts.setCursor(20, 130);
    u8g2Fonts.print("请稍候...");
    
    // 👇 🔔 把测试文字加在这里 👇
    u8g2Fonts.setCursor(20, 160);
    u8g2Fonts.print("NEW V1.0.1"); 
    // 👆 ---------------------- 👆

  } while (display.nextPage());
  display.hibernate();
}

void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) { 
      wifiConnected = false;
      lastWiFiCheck = millis(); 
    }
    if (millis() - lastWiFiCheck > 60000) {
      WiFi.reconnect(); 
      lastWiFiCheck = millis();
    }
  } else {
    if (!wifiConnected) { 
      wifiConnected = true;
      if (!locationFetched) getLocationByIP(); 
      fetchWeatherAndTime();
      drawFullUI();
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        lastWeatherUpdate = timeinfo.tm_min;
        currentMinute = timeinfo.tm_min;
        currentHour = timeinfo.tm_hour;
      }
    }
  }
}

void fetchWeatherAndTime() {
  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp1.aliyun.com");

  String url = "http://wttr.in/" + String(currentLat, 4) + "," +
               String(currentLon, 4) + "?format=j1&lang=zh";

  Serial.println("🌤 天气请求 URL: " + url);

  HTTPClient http;
  if (!http.begin(url)) {
    Serial.println("❌ 天气 http.begin() 失败");
    return;
  }
  http.setTimeout(15000);
  http.setConnectTimeout(10000);
  http.useHTTP10(true);
  http.setReuse(false);

  int httpCode = http.GET();
  Serial.printf("🌤 天气 HTTP 响应码: %d\n", httpCode);

  if (httpCode == 200) {
    WiFiClient *stream = http.getStreamPtr();
    stream->setTimeout(5000);

    String payload = http.getString();
    Serial.printf("🌤 响应体长度: %d\n", payload.length());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      JsonObject cur = doc["current_condition"][0];
      if (!cur.isNull()) {
        outTemp = cur["temp_C"].as<float>();
        outHum  = cur["humidity"].as<float>();

        int wwoCode = cur["weatherCode"].as<int>();
        currentWeatherCode = wwoToWmo(wwoCode);

        weatherText = getWwoTextCN(wwoCode);

        Serial.printf("🌤 天气 OK: %.1f°C, %.0f%%, WWO=%d → WMO=%d, text=%s\n",
                      outTemp, outHum, wwoCode, currentWeatherCode, weatherText.c_str());
      } else {
        Serial.println("❌ wttr.in JSON 中 current_condition 缺失");
      }
    } else {
      Serial.printf("❌ 天气 JSON 解析失败: %s\n", err.c_str());
    }
  } else {
    Serial.printf("❌ 天气 HTTP 失败, code=%d\n", httpCode);
  }
  http.end();

  sensors_event_t hum, temp;
  aht.getEvent(&hum, &temp);
  inTemp = temp.temperature;
  inHum  = hum.relative_humidity;
}

void drawSetupScreen(String msg) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_logisoso24_tn);
    u8g2Fonts.setCursor(20, 40);
    u8g2Fonts.print("Setup WiFi");

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(20, 100);
    u8g2Fonts.print("首次使用或更换网络，");
    u8g2Fonts.setCursor(20, 130);
    u8g2Fonts.print("请用手机连接热点:");
    
    u8g2Fonts.setFont(u8g2_font_helvB18_tf);
    u8g2Fonts.setCursor(20, 170);
    u8g2Fonts.print("MyClock");
    
    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(20, 210);
    u8g2Fonts.print("连上后手机会自动弹出网页");
    u8g2Fonts.setCursor(20, 240);
    u8g2Fonts.print("请按提示输入家里WiFi密码");
    
    display.drawRect(10, 260, 380, 30, GxEPD_BLACK);
    u8g2Fonts.setCursor(20, 280);
    u8g2Fonts.print("如需重置，长按 BOOT 键 3 秒后等待屏幕刷新");
  } while (display.nextPage());
  display.hibernate();
}

void drawFullUI() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    char yearBuf[8], monthBuf[8], dayBuf[8];
    sprintf(yearBuf, "%04d", timeinfo.tm_year + 1900);
    sprintf(monthBuf, "%02d", timeinfo.tm_mon + 1);
    sprintf(dayBuf, "%02d", timeinfo.tm_mday);
    
    u8g2Fonts.setFont(u8g2_font_logisoso20_tn);
    u8g2Fonts.setCursor(20, 45);
    u8g2Fonts.print(yearBuf);
    
    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(77, 40); 
    u8g2Fonts.print("年");
    
    u8g2Fonts.setFont(u8g2_font_logisoso20_tn);
    u8g2Fonts.setCursor(90, 45); 
    u8g2Fonts.print(monthBuf);
    
    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(118, 40); 
    u8g2Fonts.print("月");
    
    u8g2Fonts.setFont(u8g2_font_logisoso20_tn);
    u8g2Fonts.setCursor(137, 45); 
    u8g2Fonts.print(dayBuf);
    
    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(168, 40); 
    u8g2Fonts.print("日");

    const char* weekDays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    u8g2Fonts.setCursor(194, 40); 
    u8g2Fonts.print(weekDays[timeinfo.tm_wday]);

    String lunarStr = getLunarDate(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    u8g2Fonts.setCursor(272, 40); 
    u8g2Fonts.print(lunarStr);

    drawMiniCalendar(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

    char timeBuf[16];
    sprintf(timeBuf, "%02d:%02d", currentHour, currentMinute);
    u8g2Fonts.setFont(u8g2_font_logisoso58_tn);
    u8g2Fonts.setCursor(20, 145);
    u8g2Fonts.print(timeBuf);

    display.drawFastHLine(0, 170, 400, GxEPD_BLACK);

    bool isNight = (currentHour >= 18 || currentHour < 6);
    drawWeatherIcon(40, 200, currentWeatherCode, isNight); 
    
    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(65, 207); 
    u8g2Fonts.print(weatherText);

    int weatherW = u8g2Fonts.getUTF8Width(weatherText.c_str());
    u8g2Fonts.setCursor(65 + weatherW + 4, 207); 
    u8g2Fonts.print("(" + currentCity + ")");
    
    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(20, 240); 
    u8g2Fonts.print("室外温度:");
    
    char outTempBuf[16];
    sprintf(outTempBuf, "%.1f", outTemp);
    u8g2Fonts.setFont(u8g2_font_helvB18_tf); 
    u8g2Fonts.setCursor(100, 242); 
    u8g2Fonts.print(outTempBuf);
    
    u8g2Fonts.setFont(u8g2_font_helvB18_tf); 
    u8g2Fonts.setCursor(145, 242);
    u8g2Fonts.print("°C");

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(20, 270); 
    u8g2Fonts.print("室外湿度:");

    char outHumBuf[16];
    sprintf(outHumBuf, "%.0f", outHum); 
    u8g2Fonts.setFont(u8g2_font_helvB18_tf); 
    u8g2Fonts.setCursor(100, 272); 
    u8g2Fonts.print(outHumBuf);
    
    u8g2Fonts.setFont(u8g2_font_helvB18_tf); 
    u8g2Fonts.setCursor(128, 272); 
    u8g2Fonts.print("%");

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(192, 240); 
    u8g2Fonts.print("室内温度:");
    
    char inTempBuf[16];
    sprintf(inTempBuf, "%.1f", inTemp);
    u8g2Fonts.setFont(u8g2_font_helvB18_tf);
    u8g2Fonts.setCursor(272, 242); 
    u8g2Fonts.print(inTempBuf);
    
    u8g2Fonts.setFont(u8g2_font_helvB18_tf);
    u8g2Fonts.setCursor(317, 242); 
    u8g2Fonts.print("°C");

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(192, 270); 
    u8g2Fonts.print("室内湿度:");

    char inHumBuf[16];
    sprintf(inHumBuf, "%.1f", inHum);
    u8g2Fonts.setFont(u8g2_font_helvB18_tf);
    u8g2Fonts.setCursor(272, 272); 
    u8g2Fonts.print(inHumBuf);
    
    u8g2Fonts.setFont(u8g2_font_helvB18_tf);
    u8g2Fonts.setCursor(320, 272); 
    u8g2Fonts.print("%");

  } while (display.nextPage());
  display.hibernate();
}

void drawMiniCalendar(int year, int month, int today) {
  struct tm firstDay = {0};
  firstDay.tm_year = year - 1900; firstDay.tm_mon = month - 1; firstDay.tm_mday = 1; firstDay.tm_hour = 12; 
  mktime(&firstDay);
  int startWday = firstDay.tm_wday; 
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  if (isLeap) daysInMonth[1] = 29;
  int totalDays = daysInMonth[month - 1];
  int startX = 250; int startY = 55; int colWidth = 20; int rowHeight = 16;
  const char* headers[] = {"一", "二", "三", "四", "五", "六", "日"};
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312); 
  for (int i = 0; i < 7; i++) {
    int x = startX + i * colWidth;
    u8g2Fonts.setCursor(x + 4, startY + 10);
    u8g2Fonts.print(headers[i]);
  }
  display.drawFastHLine(startX, startY + 14, colWidth * 7 - 2, GxEPD_BLACK);
  int col = (startWday == 0) ? 6 : (startWday - 1); 
  int row = 0;
  for (int d = 1; d <= totalDays; d++) {
    int x = startX + col * colWidth;
    int y = startY + 16 + row * rowHeight;
    if (d == today) {
      display.fillRect(x + 2, y, 16, 11, GxEPD_BLACK);
    } else {
      char dayStr[4]; sprintf(dayStr, "%d", d);
      u8g2Fonts.setFont(u8g2_font_helvB10_tf);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
      int w = u8g2Fonts.getUTF8Width(dayStr);
      u8g2Fonts.setCursor(x + (colWidth - w) / 2, y + 11);
      u8g2Fonts.print(dayStr);
    }
    col++; if (col > 6) { col = 0; row++; }
  }
}

void drawWeatherIcon(int x, int y, int code, bool isNight) {
  if (code == 0) {
    if (!isNight) {
      int r = 12;
      display.drawCircle(x, y, r, GxEPD_BLACK); 
      for (int i = 0; i < 8; i++) {
        float angle = i * PI / 4.0;
        display.drawLine(x + cos(angle) * (r + 4), y + sin(angle) * (r + 4),
                         x + cos(angle) * (r + 9), y + sin(angle) * (r + 9), GxEPD_BLACK);
      }
    } else {
      int r = 14;
      display.fillCircle(x, y, r, GxEPD_BLACK);
      display.fillCircle(x - r/2, y - r/3, r, GxEPD_WHITE);
      display.fillCircle(x + 10, y - 8, 1, GxEPD_BLACK);
      display.fillCircle(x - 2, y + 10, 1, GxEPD_BLACK);
    }
  } 
  else if (code >= 1 && code <= 3) {
    if (!isNight) {
      display.drawCircle(x - 6, y - 6, 8, GxEPD_BLACK);
      for (int i = 0; i < 8; i++) {
        float angle = i * PI / 4.0;
        display.drawLine(x - 6 + cos(angle) * 10, y - 6 + sin(angle) * 10,
                         x - 6 + cos(angle) * 13, y - 6 + sin(angle) * 13, GxEPD_BLACK);
      }
    } else {
      display.fillCircle(x - 8, y - 10, 8, GxEPD_BLACK);     
      display.fillCircle(x - 12, y - 13, 8, GxEPD_WHITE);    
    }
    display.fillCircle(x + 2, y + 2, 8, GxEPD_BLACK);   
    display.fillCircle(x - 8, y + 4, 6, GxEPD_BLACK);   
    display.fillCircle(x + 12, y + 5, 6, GxEPD_BLACK);  
    display.fillRect(x - 8, y + 4, 20, 7, GxEPD_BLACK); 
  }
  else if (code >= 51 && code <= 67) {
    display.fillCircle(x - 5, y - 2, 8, GxEPD_BLACK); 
    display.fillCircle(x + 6, y - 4, 9, GxEPD_BLACK);
    display.fillCircle(x + 13, y + 2, 7, GxEPD_BLACK);
    display.fillRect(x - 12, y, 26, 8, GxEPD_BLACK);
    display.drawLine(x - 8, y + 14, x - 12, y + 22, GxEPD_BLACK);
    display.drawLine(x + 2, y + 14, x - 2, y + 22, GxEPD_BLACK);
    display.drawLine(x + 12, y + 14, x + 8, y + 22, GxEPD_BLACK);
  }
  else if (code >= 71 && code <= 77) {
    display.fillCircle(x - 5, y - 2, 8, GxEPD_BLACK); 
    display.fillCircle(x + 6, y - 4, 9, GxEPD_BLACK);
    display.fillCircle(x + 13, y + 2, 7, GxEPD_BLACK);
    display.fillRect(x - 12, y, 26, 8, GxEPD_BLACK);

    int sy = y + 17;
    int sx1 = x - 6;
    display.drawLine(sx1, sy - 4, sx1, sy + 4, GxEPD_BLACK);           
    display.drawLine(sx1 - 4, sy, sx1 + 4, sy, GxEPD_BLACK);
    display.drawLine(sx1 - 3, sy - 3, sx1 + 3, sy + 3, GxEPD_BLACK);   
    display.drawLine(sx1 - 3, sy + 3, sx1 + 3, sy - 3, GxEPD_BLACK);   

    int sx2 = x + 8;
    display.drawLine(sx2, sy - 4, sx2, sy + 4, GxEPD_BLACK);
    display.drawLine(sx2 - 4, sy, sx2 + 4, sy, GxEPD_BLACK);
    display.drawLine(sx2 - 3, sy - 3, sx2 + 3, sy + 3, GxEPD_BLACK);
    display.drawLine(sx2 - 3, sy + 3, sx2 + 3, sy - 3, GxEPD_BLACK);
  }
  else if (code >= 95 && code <= 99) {
    display.fillCircle(x - 5, y - 4, 8, GxEPD_BLACK); 
    display.fillCircle(x + 6, y - 6, 9, GxEPD_BLACK);
    display.fillCircle(x + 13, y, 7, GxEPD_BLACK);
    display.fillRect(x - 12, y - 2, 26, 8, GxEPD_BLACK);
    display.drawLine(x, y + 8, x - 4, y + 15, GxEPD_BLACK);
    display.drawLine(x - 4, y + 15, x + 2, y + 15, GxEPD_BLACK);
    display.drawLine(x + 2, y + 15, x - 2, y + 22, GxEPD_BLACK);
    display.drawLine(x - 10, y + 14, x - 14, y + 22, GxEPD_BLACK);
    display.drawLine(x + 10, y + 14, x + 6, y + 22, GxEPD_BLACK);
  }
  else {
    display.fillCircle(x - 8, y + 2, 6, GxEPD_BLACK);
    display.fillCircle(x + 4, y - 2, 7, GxEPD_BLACK);
    display.fillRect(x - 8, y + 2, 12, 5, GxEPD_BLACK);
    display.fillCircle(x - 2, y - 4, 8, GxEPD_BLACK);
    display.fillCircle(x + 9, y - 2, 7, GxEPD_BLACK);
    display.fillRect(x - 10, y - 4, 19, 10, GxEPD_BLACK);
  }
}

void drawPartialMinute() {
  display.setPartialWindow(16, 80, 184, 76); 
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE); 
    char timeBuf[16]; sprintf(timeBuf, "%02d:%02d", currentHour, currentMinute);
    u8g2Fonts.setFont(u8g2_font_logisoso58_tn); 
    u8g2Fonts.setCursor(20, 145); u8g2Fonts.print(timeBuf);
  } while (display.nextPage());
  display.hibernate();
}

void drawPartialSensor() {
  sensors_event_t hum, temp; 
  aht.getEvent(&hum, &temp); 
  inTemp = temp.temperature; 
  inHum = hum.relative_humidity;
  
  display.setPartialWindow(176, 224, 224, 72); 
  
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    
    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(192, 240); 
    u8g2Fonts.print("室内温度:");
    
    char inTempBuf[16]; 
    sprintf(inTempBuf, "%.1f", inTemp);
    u8g2Fonts.setFont(u8g2_font_helvB18_tf); 
    u8g2Fonts.setCursor(272, 242); 
    u8g2Fonts.print(inTempBuf);
    
    u8g2Fonts.setFont(u8g2_font_helvB18_tf);
    u8g2Fonts.setCursor(317, 242); 
    u8g2Fonts.print("°C");

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setCursor(192, 270); 
    u8g2Fonts.print("室内湿度:");
    
    char inHumBuf[16]; 
    sprintf(inHumBuf, "%.1f", inHum);
    u8g2Fonts.setFont(u8g2_font_helvB18_tf); 
    u8g2Fonts.setCursor(272, 272); 
    u8g2Fonts.print(inHumBuf);
    
    u8g2Fonts.setFont(u8g2_font_helvB18_tf);
    u8g2Fonts.setCursor(320, 272); 
    u8g2Fonts.print("%");
  } while (display.nextPage());
  
  display.hibernate();
}

void dailyMaintenance() {
  display.hibernate();
  display.end(); 
  delay(5 * 60 * 1000); 
  SPI.begin(EPD_SCK, -1, EPD_SDA, EPD_CS);
  display.init(115200, true, 50, false); 
  drawFullUI(); 
}