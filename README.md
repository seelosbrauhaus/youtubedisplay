# youtubedisplay
Based on the build of the The HackMakeMod Subscription Counting Clock project.  I couldn't get their Social Genius API service to work so the new code is modified to go straight to the YouTube API... just update the values between lines 110-117 and push the code to your ESP8266.  

Original build guide here: https://hackmakemod.com/blogs/projects/diy-kit-guide-building-your-own-subscription-clock


# ESP8266 YouTube Counter (MAX7219 / MD_Parola)

An ESP8266 project that displays:
- **Time**
- **Day of week**
- **Date**
- **YouTube channel name**
- **Subscriber count**
- **Total channel views**

…on an 8-module MAX7219 LED matrix using **MD_Parola** with a smooth fade + hold rotation.

This repo fetches live channel stats from the **YouTube Data API v3** using a channel **handle** (e.g. `@seelosbrauhaus`).

---

## Hardware

- ESP8266 (NodeMCU / Wemos D1 mini / similar)
- MAX7219 LED matrix modules (default: **8 modules**)
- Wiring (default pins in code):
  - `CLK_PIN` = `D5`
  - `DATA_PIN` = `D7`
  - `CS_PIN` = `D8`

---

## Libraries Required (Arduino Library Manager)

Install these via **Tools → Manage Libraries**:

- **ESP8266 board support**
  - Install via Boards Manager: “ESP8266 by ESP8266 Community”
- **ArduinoJson** (by Benoît Blanchon)
- **MD_Parola** (by MajicDesigns)
- **MD_MAX72XX** (by MajicDesigns)
- **NTPClient** (by Fabrice Weinberg)

> Note: `ESP8266WiFi`, `WiFiClientSecure`, and `ESP8266HTTPClient` are part of the ESP8266 core.

---

## YouTube API Setup (Required)

1. Create or select a project in **Google Cloud Console**
2. Enable **YouTube Data API v3**
3. Create an **API key**
4. (Recommended) Restrict the key:
   - **API restrictions:** YouTube Data API v3 only  
   - (Optional) Application restrictions as appropriate for your environment

---

## Quick Start

1. Open the `.ino` sketch in Arduino IDE
2. Update the configuration values listed below
3. Select your ESP8266 board + COM port
4. Upload
5. Open Serial Monitor at **115200 baud**

---

## Configuration: Lines You Must Update

### 1) WiFi SSID + Password

Search for the WiFi credentials:

```cpp
char ssid[32]     = "SSID HERE";
char password[32] = "WIFI PASSWORD HERE";
````

Replace with your network name and password.

---

### 2) YouTube API Key

Search for:

```cpp
const char* YOUTUBE_API_KEY
```

Paste your API key between the quotes.

---

### 3) YouTube Channel Handle

Search for:

```cpp
const char* YT_HANDLE 
```

Replace with your channel handle (include the `@`), for example:

```cpp
const char* YT_HANDLE = "@YourChannelHandle";
```

---

### 4) Timezone Offset

Search for:

```cpp
timeClient.setTimeOffset(-21600);
```

That value is **seconds offset from UTC**.

Common examples:

* US Central (CST): `-21600`
* US Central (CDT): `-18000`
* US Eastern (EST): `-18000`
* US Eastern (EDT): `-14400`
* UTC: `0`

> Note: This is a fixed offset (no automatic daylight savings adjustment).

---

### 5) LED Matrix Hardware / Pinout (if different)

Search for:

```cpp
#define MAX_DEVICES  8
#define CLK_PIN   D5
#define DATA_PIN  D7
#define CS_PIN    D8
```

Change if you have fewer/more modules or different wiring.

---

## Display Timing Controls

### Hold time (how long each item stays fully visible)

Search for:

```cpp
const int timeDelay = 3000;
```

Set to milliseconds:

* `3000` = 3 seconds
* `5000` = 5 seconds

### YouTube fetch interval

Search for:

```cpp
uint32_t waitDelay_fetch = 10UL * 60UL * 1000UL;
```

Examples:

* 5 minutes: `5UL * 60UL * 1000UL`
* 10 minutes: `10UL * 60UL * 1000UL`
* 15 minutes: `15UL * 60UL * 1000UL`

> YouTube view/subscriber numbers don’t update in real time, so polling too frequently usually doesn’t help.

---

## Fit / Layout Tips

### Tighten spacing for long channel names

This project supports setting character spacing to 0 **only for the channel-name item**.
Search in the state machine for `setCharSpacing` and adjust as needed.

If your channel name still clips:

* Use a shorter prefix (e.g. `YT:` instead of `YouTube`)
* Consider scrolling the channel name (recommended for long names)


