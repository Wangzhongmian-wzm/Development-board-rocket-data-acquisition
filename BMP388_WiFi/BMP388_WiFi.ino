/*!
 * MindPlus
 * DFRobot, 掌控板
 *
 * BMP388 温度 / 气压 / 高度  ->  WiFi(AP模式)  ->  电脑网页
 *
 * 高频采集：每 50ms 读一次传感器（20 Hz），攒够 1 秒打包成一批发一次 HTTP。
 * 这样传感器跑满速，而 HTTP 开销和不改频率时一样（还是每秒一个请求）。
 *
 * 用法：
 *   1. 烧录本程序
 *   2. 电脑连 WiFi 热点  BMP388  密码 12345678
 *   3. 电脑上跑 connect.ps1，浏览器打开 http://localhost:8000
 *
 * OLED：第1行=板子自己的地址，2/3/4行=温度/气压/高度（每 200ms 刷一次，不是 20Hz）
 * RGB灯：蓝=热点开着等电脑连，绿=本批上传成功，红=上传失败
 */

#include <MPython.h>
#include <DFRobot_BMP388_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ==================== 配置 ====================
#define AP_SSID       "BMP388"         // 板子开的热点名
#define AP_PASS       "12345678"       // 密码至少 8 位，ESP32 不接受更短的
#define SERVER_HOST   "192.168.4.2"    // 电脑连上热点后拿到的地址
#define SERVER_PORT   8000

#define SAMPLE_HZ         20           // 采样频率。传感器上限 200 Hz
#define UPLOAD_PERIOD_MS  1000         // 攒多久发一次。CSV 大小 = SAMPLE_HZ 行/秒
// ==============================================

#define SAMPLE_INTERVAL   (1000 / SAMPLE_HZ)
#define MAX_BATCH         (SAMPLE_HZ * 2)   // 留一倍余量，防采样比上传快时溢出
#define DISPLAY_INTERVAL  500               // OLED 刷新周期。每次刷屏要 ~110ms（整屏推 I2C），
                                            // 刷太快会挤占采样。500ms 人眼够看，开销降到 2 次/秒
#define SEA_LEVEL_PA      101325.0          // 标准海平面气压

DFRobot_BMP388_I2C bmp388;

// 这一批攒下的采样点
float    bTemp[MAX_BATCH];
float    bPress[MAX_BATCH];
float    bAlt[MAX_BATCH];
int      bCount = 0;

uint32_t lastSample  = 0;
uint32_t lastUpload  = 0;
uint32_t lastDisplay = 0;
uint32_t okCount     = 0;
uint32_t errCount    = 0;

// 性能打点：看看一秒里到底采了多少个点、时间花在哪
uint32_t samplesThisSec  = 0;
uint32_t sampleUsAccum   = 0;
uint32_t displayUsAccum  = 0;
uint32_t displayCount    = 0;

// 把攒下的一批打包成 JSON 发出去
bool uploadBatch() {
  String body = String("{\"hz\":") + String(SAMPLE_HZ) + ",\"temp\":[";
  for (int i = 0; i < bCount; i++) {
    if (i) body += ',';
    body += String(bTemp[i], 2);
  }
  body += "],\"press\":[";
  for (int i = 0; i < bCount; i++) {
    if (i) body += ',';
    body += String(bPress[i], 2);
  }
  body += "],\"alt\":[";
  for (int i = 0; i < bCount; i++) {
    if (i) body += ',';
    body += String(bAlt[i], 1);
  }
  body += "],\"ok\":" + String(okCount) + ",\"err\":" + String(errCount) + "}";

  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/data";

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  http.end();

  return (code > 0 && code < 400);
}

// 读一次传感器，存进当前批次
void takeSample() {
  if (bCount >= MAX_BATCH) return;

  float t  = bmp388.readTemperature();        // 摄氏度
  float pa = bmp388.readPressure();           // 帕斯卡

  bTemp[bCount]  = t;
  bPress[bCount] = pa / 100.0;                // Pa -> hPa
  // 和库的 readCalibratedAltitude 同一个公式，但气压已经读过了，不用再读一遍
  bAlt[bCount]   = (1.0 - pow(pa / SEA_LEVEL_PA, 0.190284)) * 287.15 / 0.0065;
  bCount++;
}

void setup() {
  mPython.begin();
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== BMP388 AP mode start ===");

  display.setCursorLine(1);
  display.printLine("BMP388 启动中");

  // BMP388 初始化。内部自己调 Wire.begin() 并读一次海平面基准，
  // 最多阻塞 5 秒，所以只能放 setup，不能放 loop。
  bmp388.begin();

  // 开热点。板子自己就是 192.168.4.1，并给连上来的电脑分配 IP。
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS);
  String apIP = WiFi.softAPIP().toString();

  Serial.printf("softAP ok=%d  ip=%s  mac=%s  ch=%d\n",
                (int)apOk, apIP.c_str(), WiFi.softAPmacAddress().c_str(), WiFi.channel());
  Serial.printf("sample %d Hz, upload every %d ms, POST -> http://%s:%d/api/data\n",
                SAMPLE_HZ, UPLOAD_PERIOD_MS, SERVER_HOST, SERVER_PORT);

  display.setCursorLine(1);
  if (apOk) {
    display.printLine(apIP);
    rgb.write(-1, 0x0000FF);
  } else {
    display.printLine("热点开启失败");
    rgb.write(-1, 0xFF0000);
  }
  display.setCursorLine(2);
  display.printLine(String("热点 ") + AP_SSID);
  display.setCursorLine(3);
  display.printLine(String(SAMPLE_HZ) + "Hz 采集中");
  display.setCursorLine(4);
  display.printLine("电脑连上后跑");
  delay(3000);
}

void loop() {
  uint32_t now = millis();

  // ---- 采样：每 50ms 一次 ----
  // 用 while + 按理论时间步进（不是 = now），这样被显示刷新阻塞后能把落下的点补回来。
  // 写 if + = now 的话，每次阻塞都会永久丢点，实测只能跑到 10Hz。
  if (now - lastSample > SAMPLE_INTERVAL * 5) {
    lastSample = now;          // 落后太多就重新对齐，避免一次性爆发补采
  }
  while (now - lastSample >= SAMPLE_INTERVAL && bCount < MAX_BATCH) {
    lastSample += SAMPLE_INTERVAL;
    uint32_t t0 = micros();
    takeSample();
    sampleUsAccum += micros() - t0;
    samplesThisSec++;
  }

  // ---- 上传：每 1 秒把攒的一批发出去 ----
  if (now - lastUpload >= UPLOAD_PERIOD_MS) {
    lastUpload = now;

    if (bCount > 0) {
      int stations = WiFi.softAPgetStationNum();
      if (stations == 0) {
        rgb.write(-1, 0x0000FF);                 // 蓝：还没电脑连，发了也没人收
        Serial.printf("[wait] %d 个点  %.2fC %.2fhPa %.1fm  (热点开着，等电脑连)\n",
                      bCount, bTemp[bCount - 1], bPress[bCount - 1], bAlt[bCount - 1]);
      } else if (uploadBatch()) {
        okCount++;
        rgb.write(-1, 0x00FF00);                 // 绿：本批上传成功
        Serial.printf("[ok]  %d 个点  %.2fC %.2fhPa %.1fm  ok=%u err=%u\n",
                      bCount, bTemp[bCount - 1], bPress[bCount - 1], bAlt[bCount - 1],
                      okCount, errCount);
      } else {
        errCount++;
        rgb.write(-1, 0xFF0000);                 // 红：上传失败
        Serial.printf("[ERR] %d 个点上传失败，已失败 %u 次（电脑连热点了吗？服务在跑吗？）\n",
                      bCount, errCount);
      }
      bCount = 0;
    }

    // 每秒报一次实际性能，看看有没有达到目标采样率
    Serial.printf("[stat] 本秒采样 %u 个 (目标 %d)  采样耗时 %u ms  显示 %u 次共 %u ms\n",
                  samplesThisSec, SAMPLE_HZ,
                  sampleUsAccum / 1000, displayCount, displayUsAccum / 1000);
    samplesThisSec = 0; sampleUsAccum = 0; displayUsAccum = 0; displayCount = 0;
  }

  // ---- 显示：每 200ms 刷一次，太快了人眼也读不过来 ----
  if (now - lastDisplay >= DISPLAY_INTERVAL && bCount > 0) {
    lastDisplay = now;
    uint32_t td0 = micros();
    int last = bCount - 1;

    display.setCursorLine(1);
    display.printLine(WiFi.softAPIP().toString());

    display.setCursorLine(2);
    display.printLine("温度 " + String(bTemp[last], 1) + "C");

    display.setCursorLine(3);
    display.printLine("气压 " + String(bPress[last], 1) + "hPa");

    display.setCursorLine(4);
    display.printLine("高度 " + String(bAlt[last], 1) + "m");
    displayUsAccum += micros() - td0;
    displayCount++;
  }
}
