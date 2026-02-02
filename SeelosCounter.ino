/*
  ESP8266 + MAX7219 (MD_Parola) YouTube Channel Stats Display
  - Replaces api.socialgenius.io with YouTube Data API v3
  - Looks up channel by handle: @seelosbrauhaus
  - Keeps your state machine + display rotation (Time / Day / Channel / Subs / Views)
  - Adds robust HTTP handling + safe string formatting (no uninitialized buffers)

  You must:
    1) Enable YouTube Data API v3 in Google Cloud
    2) Create an API key
    3) Paste it into YOUTUBE_API_KEY below
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ----------------------------
// LED MATRIX DISPLAY DEFINITION
// ----------------------------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES  8
#define CLK_PIN   D5  // or SCK
#define DATA_PIN  D7  // or MOSI
#define CS_PIN    D8  // or SS

MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// ----------------------------
// Timing / Display Config
// ----------------------------
const int timeDelay = 5000; // Delay between Time / Day / Subs / Views
const int fadeSpeed = 40;   // Speed of the fade
const int fadeDuration = 3000 / 2;

const float numIterations = 15.0;
const int clockedIterations = 50;
const float iterationOffset = (fadeDuration / clockedIterations) * 1.0;

int statusCode;
String st;
String content;

// ----------------------------
// State Machine
// ----------------------------
enum class State : uint8_t {
  fetch,
  noop,
  wait,
  fadeIn,
  fadeOut
};

enum class Values : uint8_t {
  curTime = 0,
  weekDay = 1,
  date = 2,              // NEW
  yt_channel = 3,
  yt_subscribers = 4,
  yt_views = 5,
  count = 6
};

State nextState = State::fetch;
State state = State::fetch;

uint32_t prevTime = millis();
uint32_t currentTime = millis();
uint32_t waitDelay;

uint32_t waitDelay_fetch = 10UL * 60UL * 1000UL;   // 10 minutes
// uint32_t waitDelay_fetch = 20UL * 1000UL;      // debug: 20 seconds
uint32_t prevTime_fetch = millis() + waitDelay_fetch * 2;

String feed[(uint8_t)Values::count] = {};

// ----------------------------
// YouTube Data (populated by API fetch)
// ----------------------------
String youtube_views = "0";
String youtube_channel = "YouTube";
String youtube_subscribers = "0";
String youtube_videos = "0";

// ----------------------------
// Time Variables
// ----------------------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String weekDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *AMPM = "AM";

// ----------------------------
// HTTPS client
// ----------------------------
WiFiClientSecure client;

// ----------------------------
// YouTube API constants
// ----------------------------
#define YT_HOST "www.googleapis.com"
const char* YOUTUBE_API_KEY = "API KEY HERE";  // <-- replace locally
const char* YT_HANDLE = "@seelosbrauhaus";

// ----------------------------
// WiFi Credentials
// ----------------------------
char ssid[32]     = "serpland";
char password[32] = "l3tth3g00dt1m3sr0ll";

// Web server (unused by this sketch’s core logic, kept from your original)
ESP8266WebServer server(80);

/* Soft AP network parameters (kept from original; not used in main flow) */
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

/** Should I connect to WLAN asap? */
boolean connect;

/** Last time I tried to connect to WLAN */
unsigned long lastConnectTry = 0;

/** Current WLAN status */
unsigned int status = WL_IDLE_STATUS;

// ----------------------------
// Helpers
// ----------------------------
boolean isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) return false;
  }
  return true;
}

String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) res += String((ip >> (8 * i)) & 0xFF) + ".";
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

void nextStateAfter(State ss, uint32_t d) {
  waitDelay = d;
  nextState = ss;
  state = State::wait;
  prevTime = currentTime;
}

// ----------------------------
// WiFi Connect
// ----------------------------
void connectWifi() {
  Serial.println("Connecting as wifi client...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  int connRes = WiFi.waitForConnectResult();
  client.setInsecure(); // simplest; see note below if you want TLS validation
  Serial.print("connRes: ");
  Serial.println(connRes);
}

// ----------------------------
// YouTube Fetch (replaces SocialGenius)
// ----------------------------
void fetchYouTubeChannelStats() {
  Serial.println(F("Fetching YouTube channel stats (HTTPClient)..."));

  client.setInsecure();
  client.setTimeout(12000); // 12s network timeout

  HTTPClient https;

  // Build URL
  String url = "https://";
  url += YT_HOST;
  url += "/youtube/v3/channels?part=snippet,statistics&forHandle=";
  url += YT_HANDLE;                 // "@seelosbrauhaus"
  url += "&key=";
  url += YOUTUBE_API_KEY;

  Serial.print(F("URL: "));
  Serial.println(url);

  if (!https.begin(client, url)) {
    Serial.println(F("https.begin() failed"));
    return;
  }

  https.addHeader("Accept", "application/json");
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = https.GET();
  Serial.print(F("HTTP code: "));
  Serial.println(httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.print(F("GET failed, error: "));
    Serial.println(https.errorToString(httpCode));
    String errBody = https.getString();
    Serial.println(F("Error body (first 400 chars):"));
    Serial.println(errBody.substring(0, 400));
    https.end();
    return;
  }

  String payload = https.getString();
  https.end();

  Serial.print(F("Payload bytes: "));
  Serial.println(payload.length());

  // Optional: quick sanity check
  if (payload.length() < 20 || payload.charAt(0) != '{') {
    Serial.println(F("Payload not JSON (unexpected). First 200 chars:"));
    Serial.println(payload.substring(0, 200));
    return;
  }

  DynamicJsonDocument doc(8192); // give room for YouTube response
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(err.f_str());
    Serial.println(F("First 200 chars:"));
    Serial.println(payload.substring(0, 200));
    return;
  }

  JsonArray items = doc["items"].as<JsonArray>();
  if (items.size() == 0) {
    Serial.println(F("No channel items returned. (Handle ok? API key ok?)"));
    // Print the "kind"/"pageInfo"/etc if present:
    if (doc.containsKey("error")) {
      Serial.println(F("API error object present:"));
      serializeJson(doc["error"], Serial);
      Serial.println();
    }
    return;
  }

  JsonObject item = items[0];
  JsonObject snippet = item["snippet"];
  JsonObject stats   = item["statistics"];

  youtube_channel     = (const char*)(snippet["title"] | "YouTube");
  youtube_views       = (const char*)(stats["viewCount"] | "0");
  youtube_subscribers = (const char*)(stats["subscriberCount"] | "0");
  youtube_videos      = (const char*)(stats["videoCount"] | "0");

  // Normalize channel name for LED matrix display
  youtube_channel.trim();          // remove leading/trailing whitespace
  youtube_channel.replace(" ", ""); // collapse double spaces (run twice if you want)

  feed[(uint8_t)Values::yt_channel] = "Seelos Brauhaus"; //youtube_channel;

  Serial.print(F("Channel: ")); Serial.println(youtube_channel);
  Serial.print(F("Subs: "));    Serial.println(youtube_subscribers);
  Serial.print(F("Views: "));   Serial.println(youtube_views);
  Serial.print(F("Videos: "));  Serial.println(youtube_videos);
}


// ----------------------------
// State Machine
// ----------------------------
void statemachine() {
  static float intensity = 0.0;
  static int textValue = 0;

  switch (state) {
    case State::noop:
      if (Serial.available() > 0) {
        Serial.readString();
        state = State::fadeIn;
      }
      break;

    case State::fetch:
      if (currentTime - prevTime_fetch > waitDelay_fetch) {
        prevTime_fetch = currentTime;
        fetchYouTubeChannelStats();
        Serial.println("Getting Data after 10 Minutes");
      }
      nextStateAfter(State::fadeIn, 50);
      break;

    case State::wait:
      if (currentTime - prevTime > waitDelay) {
        state = nextState;
        prevTime = currentTime;
      }
      break;

    case State::fadeIn:
    //  myDisplay.print(feed[textValue]);

      if (textValue == (uint8_t)Values::yt_channel) {
  myDisplay.setCharSpacing(0);
} else {
  myDisplay.setCharSpacing(1);   // or whatever your normal spacing is
}

myDisplay.print(feed[textValue]);

      intensity += numIterations / iterationOffset;

    if (intensity > numIterations) {
  intensity = numIterations;
  myDisplay.setIntensity((uint8_t)intensity);

  // Hold text on screen using existing timeDelay
  nextState = State::fadeOut;   // what comes after the wait
  state = State::wait;          // enter wait state now
  waitDelay = timeDelay;        // <--- use your existing const
  prevTime = currentTime;       // start timer

      } else if (intensity == 0) {
        myDisplay.displayClear();
        nextStateAfter(State::fadeIn, clockedIterations);
      } else {
        myDisplay.setIntensity((uint8_t)intensity);
        nextStateAfter(State::fadeIn, clockedIterations);
      }
      break;

    case State::fadeOut:
      myDisplay.print(feed[textValue]);

      intensity -= numIterations / iterationOffset;

      if (intensity <= 0) {
        intensity = 0;
        textValue++;

        if (textValue == (uint8_t)Values::count) {
          textValue = 0;
          myDisplay.displayClear();
          nextStateAfter(State::fetch, 0);
        } else {
          nextStateAfter(State::fadeIn, clockedIterations);
        }
      } else {
        myDisplay.setIntensity((uint8_t)intensity);
        nextStateAfter(State::fadeOut, clockedIterations);
      }
      break;
  }
}

// ----------------------------
// Configure Display Data (Time + formatted YouTube stats)
// ----------------------------
void configureData() {
  timeClient.update();

  
  // ---------- Time (HH:MM AM/PM) ----------
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  AMPM = (currentHour >= 12) ? "PM" : "AM";
  if (currentHour >= 13) currentHour -= 12;
  if (currentHour == 0) currentHour = 12;

  char timeStr[40];
  if (currentMinute < 10) {
    snprintf(timeStr, sizeof(timeStr), "%d:0%d %s", currentHour, currentMinute, AMPM);
  } else {
    snprintf(timeStr, sizeof(timeStr), "%d:%d %s", currentHour, currentMinute, AMPM);
  }

  // ---------- Weekday ----------
  String weekDay = weekDays[timeClient.getDay()];

  // ---------- Date (Mon Jan 31, 2026 style) ----------
  time_t rawTime = timeClient.getEpochTime();
  struct tm* ti = localtime(&rawTime);

  const char* months[] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
  };

  char dateStr[32];
  snprintf(
    dateStr,
    sizeof(dateStr),
    "%s %d, %d",
    months[ti->tm_mon],        // 0-11
    ti->tm_mday,               // 1-31
    ti->tm_year + 1900         // years since 1900
  );
  
  

  // --- Subscribers formatting (SAFE defaults) ---
  float subs = youtube_subscribers.toFloat();
  char subTrunc[30] = {0};

  if (subs >= 1000000.0) {
    float subDec = subs / 1000000.0;
    snprintf(subTrunc, sizeof(subTrunc), "%4.2fM Subs", subDec);
  }
  else if (subs >= 10000.0) {
    float subDec = subs / 1000.0;
    snprintf(subTrunc, sizeof(subTrunc), "%4.0fK Subs", subDec);
  }
  else if (subs >= 1.0) {
    snprintf(subTrunc, sizeof(subTrunc), "%lu Subs", (unsigned long)subs);
  }
  else {
    snprintf(subTrunc, sizeof(subTrunc), "0 Subs");
  }

  // --- Views formatting (SAFE defaults + <1000 handling) ---
  float views = youtube_views.toFloat();
  char viewTrunc[30] = {0};

  if (views >= 10000000.0) {
    float viewDec = views / 1000000.0;
    snprintf(viewTrunc, sizeof(viewTrunc), "%4.0fM Views", viewDec);
  }
  else if (views >= 1000000.0) {
    float viewDec = views / 1000000.0;
    snprintf(viewTrunc, sizeof(viewTrunc), "%4.2fM Views", viewDec);
  }
  else if (views >= 10000.0) {
    float viewDec = views / 1000.0;
    snprintf(viewTrunc, sizeof(viewTrunc), "%4.2fK Views", viewDec);
  }
  else if (views >= 1.0) {
    // Handle < 1000 cleanly (your original code skipped this range)
    snprintf(viewTrunc, sizeof(viewTrunc), "%lu Views", (unsigned long)views);
  }
  else {
    snprintf(viewTrunc, sizeof(viewTrunc), "0 Views");
  }

  // Set feed entries (same rotation order as your original)
  myDisplay.setTextAlignment(PA_CENTER);
feed[(uint8_t)Values::curTime] = String(timeStr);
feed[(uint8_t)Values::weekDay] = weekDay;
feed[(uint8_t)Values::date] = String(dateStr);
  feed[(uint8_t)Values::yt_views] = String(viewTrunc);
  feed[(uint8_t)Values::yt_subscribers] = String(subTrunc);

  // yt_channel is updated on fetch; keep a safe default if empty
  if (feed[(uint8_t)Values::yt_channel].length() == 0) {
    feed[(uint8_t)Values::yt_channel] = youtube_channel.length() ? youtube_channel : "YouTube";
  }
}

// ----------------------------
// Setup / Loop
// ----------------------------
void setup() {
  delay(1000);
  Serial.begin(115200);

  myDisplay.begin();
  myDisplay.setIntensity(10);
  myDisplay.setTextAlignment(PA_CENTER);
  myDisplay.setPause(2000);
  myDisplay.setSpeed(40);
  myDisplay.displayClear();
  myDisplay.print("Loading...");

  timeClient.begin();
  timeClient.setTimeOffset(-21600); // CST offset in seconds (adjust if needed)

  connect = strlen(ssid) > 0;

  // Prime defaults so display rotation is stable immediately
  feed[(uint8_t)Values::yt_channel] = "Seelos Brauhaus"; //youtube_channel;
  feed[(uint8_t)Values::yt_subscribers] = "0 Subs";
  feed[(uint8_t)Values::yt_views] = "0 Views";

  // Optional: fetch soon after boot
  prevTime_fetch = millis() - waitDelay_fetch;
  state = State::fetch;
}

void loop() {
  currentTime = millis();

  // WiFi management (kept from your original)
  if (connect) {
    Serial.println("Connect requested");
    connect = false;
    connectWifi();
    lastConnectTry = millis();
  }

  unsigned int s = WiFi.status();
  if (s == 0 && millis() > (lastConnectTry + 60000)) {
    connect = true;
  }

  if (status != s) {
    Serial.print("Status: ");
    Serial.println(s);
    status = s;

    if (s == WL_CONNECTED) {
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    } else if (s == WL_NO_SSID_AVAIL) {
      WiFi.disconnect();
    }
  }

  if (s == WL_CONNECTED) {
    timeClient.update();
  }

  configureData();
  statemachine();

  server.handleClient();
}
