# 掌控板 BMP388 环境数据采集

用一块 DFRobot 掌控板（ESP32）读 BMP388 温压高度计，通过 WiFi 把数据实时上传到电脑，网页上看曲线，按需记录成 CSV。

板子工作在 **AP 模式**：自己发一个 WiFi 热点，不依赖任何路由器，电脑连上去就能收到数据。

```
掌控板 (AP, 192.168.4.1)
  │  BMP388 · I2C · 20 Hz 采样
  │  每秒打包 20 个点发一次 HTTP POST
  ▼
电脑 (连热点, 192.168.4.2)
  └── Node 服务 :8000 ──▶ 网页 / 实时曲线 / CSV 记录
```

---

## 目录结构

```
BMP388_WiFi/     板子固件（正式版）
I2C_Scan/        I2C 总线扫描，排查传感器有没有被识别
AP_Test/         最小化热点测试，隔离网络问题
read-serial.ps1  读板子串口调试输出（会先复位板子，抓开机那段）
pc-server/
  server.js      电脑端服务：收数据、存 CSV、提供网页
  index.html     网页（实时曲线 / 记录开关）
  start.ps1      只启动服务
  connect.ps1    连板子热点 + 启动服务
```

---

## 硬件

- 掌控板（ESP32，8MB Flash）
- DFRobot BMP388 气压计，插在板子的 I2C 扩展口（SDA=23, SCL=22）
- USB 数据线（板子载 CH9102 串口芯片）

---

## 板子端：烧录

### 方法一：命令行（推荐）

Mind+ 自带一份 arduino-cli，路径：

```
C:\Program Files\Mind+\apps\deps\mind-link\tool\arduino-cli.exe
```

**必须显式指定配置文件**，否则它会去找标准的 `AppData\Local\Arduino15` 然后报 `No platforms installed`：

```bash
CLI="C:\Program Files\Mind+\apps\deps\mind-link\tool\arduino-cli.exe"
CFG="C:\Users\<用户名>\AppData\Local\mind+\Arduino\arduino-cli.yaml"

# 编译
"$CLI" --config-file "$CFG" compile --fqbn mindplus:esp32:mpython BMP388_WiFi

# 烧录（COM 口号用 `board list` 查，换板子会变）
"$CLI" --config-file "$CFG" upload -p COM8 --fqbn mindplus:esp32:mpython BMP388_WiFi
```

在 Git Bash 里跑记得 `export MSYS_NO_PATHCONV=1`，否则反斜杠路径会被改写。

### 方法二：Arduino IDE

Arduino IDE 默认看不到掌控板的核和 DFRobot 的库，需要先把 Mind+ 里那套搬过去：

- 板子核 → `%LOCALAPPDATA%\Arduino15\packages\mindplus\`
- 库 → 项目目录的 `libraries\`

搬完后在 IDE 里选开发板 **mpython** 即可编译上传。

---

## 电脑端：启动

```powershell
# 连热点 + 启动服务（AP 模式用这个）
powershell -ExecutionPolicy Bypass -File "pc-server\connect.ps1"

# 已经连着热点时，只启动服务
powershell -ExecutionPolicy Bypass -File "pc-server\start.ps1"
```

浏览器打开 <http://localhost:8000>

> 系统执行策略是 `Restricted`，`.ps1` 直接双击会被拦，必须带 `-ExecutionPolicy Bypass`。

### 不用装 Node

`start.ps1` 用的是 Mind+ 自带的 `node.exe`：

```
C:\Program Files\Mind+\apps\deps\mind-link\node.exe
```

整台机器上没有任何独立的 Node / Python 安装也能跑。

---

## 使用流程

1. 板子插电，OLED 第一行显示 `192.168.4.1`，RGB 变**蓝**（热点开着，等电脑连）
2. 电脑连 WiFi 热点 `BMP388`，密码 `12345678`
3. 启动服务，打开网页
4. 要存数据就点网页上的 **「开始记录」** —— 不点的话数据只在内存里，一个字节都不落盘
5. 再点一次停止。CSV 存在 `pc-server\` 下，文件名带时间戳，每次记录单独一个文件

## 灯光含义

| 颜色 | 含义 |
|---|---|
| 蓝 | 热点开着，还没有设备连上 |
| 绿 | 本批数据上传成功 |
| 红 | 上传失败（电脑没连热点 / 服务没跑） |

---

## 关键参数

改 `BMP388_WiFi/BMP388_WiFi.ino` 顶部：

```c
#define AP_SSID          "BMP388"      // 热点名
#define AP_PASS          "12345678"    // 密码，至少 8 位
#define SERVER_HOST      "192.168.4.2" // 电脑连上热点后的地址
#define SERVER_PORT      8000
#define SAMPLE_HZ        20            // 采样频率，传感器上限 200
#define UPLOAD_PERIOD_MS 1000          // 攒多久发一次
```

改成别的采样率后，**服务端和网页都不用动**。

CSV 体积 = 每秒 `SAMPLE_HZ` 行，每行约 55 字节：

| 采样率 | 每天 | CSV 体积 |
|---|---|---|
| 10 Hz | 86 万行 | ~47 MB |
| 20 Hz | 173 万行 | ~95 MB |
| 50 Hz | 432 万行 | ~238 MB |
| 200 Hz | 1728 万行 | ~950 MB |

---

## 踩过的坑

**1. `DFRobot_Mindplus_Esp8266WIFI` 库在掌控板上编译不过**

它 `#include <ESP8266WiFi.h>`，但 ESP32 核里没有这个头文件。网络要用 `WiFi.h` 或 `DFRobot_Mindplus_Iot`。

**2. 核的 ESP-IDF 是 3.x 时代，只支持到 WPA2**

`wifi_auth_mode_t` 枚举里没有 WPA3。连纯 WPA3 的热点会失败；WPA2/WPA3 混合模式可以用 WPA2 连上。

**3. 库自带的 `readAltitude()` 有约 523 米的固定偏差**

`begin()` 里用 `readSeaLevel(525.0)` 校准，导致开机后无论真实海拔多少都读出约 523 米。改用 `readCalibratedAltitude(101325.0)` 走标准海平面公式。

**4. `bmp388.begin()` 不能放 `loop()`**

内部会 `Wire.begin()` 并且**最多阻塞 5 秒**重试传感器。

**5. OLED 刷屏一次要 ~110ms**

`printLine` 每次都会把整块 1024 字节显存推一遍 I2C，4 行就是 ~92ms。刷新周期设成 500ms，否则会挤占采样。

**6. 高频采样必须用 `while` 补点，不能用 `if`**

写成 `if (now - last >= 50) { last = now; ... }` 时，任何一次阻塞都会**永久丢点**——实测只能跑到 10 Hz。要用 `lastSample += 50` 按理论时间步进。

**7. 网页不能用 `Math.min(...arr)`**

参数展开有上限，点数超过约 6.5 万就 `RangeError: Maximum call stack size exceeded`。20 Hz 下一小时就 7.2 万点。改成循环求极值。

**8. 掌控板只有一条 I2C 总线**

`SDA=23, SCL=22`，板载 OLED 在 `0x3C`、加速度计 QMI8658 在 `0x6B`，外部扩展口共用这条总线。没有 `Wire1`。

---

## 排查

**连不上热点** —— 烧 `AP_Test/`，看串口里 `softAP() returned` 是 0 还是 1。

**传感器读数是 0** —— 烧 `I2C_Scan/`，OLED 和 RGB 灯会显示总线上有哪些器件。正常应看到 `3C`（OLED）、`6B`（加速度计）、以及 **`76` 或 `77`（BMP388）**。只看到前两个说明传感器没插好。

**板子发数据但电脑收不到** —— 电脑是不是连在板子热点上？`ipconfig` 看有没有 `192.168.4.x` 的地址。
