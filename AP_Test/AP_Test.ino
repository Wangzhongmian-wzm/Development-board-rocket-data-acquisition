/*!
 * MindPlus
 * DFRobot, 掌控板
 *
 * 最小 AP 测试（诊断用）
 * 只做一件事：开热点，然后把返回值和状态打到串口。
 * 用来判断 softAP 是失败了，还是板子根本没跑到这儿。
 */

#include <MPython.h>
#include <WiFi.h>

void setup() {
  mPython.begin();
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== AP test start ===");
  Serial.printf("reset reason: %d\n", (int)esp_reset_reason());

  WiFi.mode(WIFI_AP);
  Serial.println("mode set to AP");

  bool ok = WiFi.softAP("BMP388", "12345678");
  Serial.printf("softAP() returned: %d\n", (int)ok);
  Serial.printf("AP IP   : %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("AP MAC  : %s\n", WiFi.softAPmacAddress().c_str());
  Serial.printf("channel : %d\n", WiFi.channel());
  Serial.printf("stations: %d\n", WiFi.softAPgetStationNum());

  display.setCursorLine(1);
  display.printLine(ok ? "AP OK" : "AP FAIL");
  display.setCursorLine(2);
  display.printLine(WiFi.softAPIP().toString());
  rgb.write(-1, ok ? 0x00FF00 : 0xFF0000);
}

void loop() {
  delay(3000);
  Serial.printf("[alive] stations=%d  ip=%s  free_heap=%u\n",
                WiFi.softAPgetStationNum(),
                WiFi.softAPIP().toString().c_str(),
                (unsigned)ESP.getFreeHeap());
}
