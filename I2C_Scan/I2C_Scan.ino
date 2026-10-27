/*!
 * MindPlus
 * DFRobot, 掌控板
 *
 * I2C 总线扫描（诊断用）
 *
 * 每 3 秒扫一遍 I2C 总线，把挂在总线上的器件地址显示在 OLED 上。
 * 用途：确认 BMP388 到底有没有被识别到。
 *
 * 预期结果：
 *   0x3C  = 板载 OLED（一定有）
 *   0x76 或 0x77 = BMP388 气压计
 * 如果只看到 3C，说明 BMP388 没接好、没插在 I2C 口上，或者线序不对。
 */

#include <MPython.h>
#include <Wire.h>

void setup() {
  mPython.begin();
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== I2C scan start ===");
}

void loop() {
  Wire.begin();

  uint8_t found[16];
  int n = 0;
  for (uint8_t a = 1; a < 127 && n < 16; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      found[n++] = a;
      Serial.printf("found 0x%02X\n", a);
    }
  }
  Serial.printf("total=%d\n", n);

  // 地址拼成字符串
  String line = "";
  for (int i = 0; i < n; i++) {
    char buf[8];
    sprintf(buf, "%02X ", found[i]);
    line += buf;
  }

  display.setCursorLine(1);
  display.printLine("I2C 设备 " + String(n) + " 个");

  display.setCursorLine(2);
  display.printLine(line.substring(0, 16));

  display.setCursorLine(3);
  display.printLine(line.length() > 16 ? line.substring(16, 32) : String(""));

  display.setCursorLine(4);
  bool hasBmp = false;
  for (int i = 0; i < n; i++) if (found[i] == 0x76 || found[i] == 0x77) hasBmp = true;
  display.printLine(hasBmp ? String("BMP388 已识别") : String("BMP388 未找到"));

  // 绿灯=找到，红灯=没找到
  if (hasBmp) rgb.write(-1, 0x00FF00);
  else        rgb.write(-1, 0xFF0000);

  delay(3000);
}
