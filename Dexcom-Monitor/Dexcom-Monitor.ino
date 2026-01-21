#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
// #include <Adafruit_ILI9341.h>
// Install the "XPT2046_Touchscreen" library by Paul Stoffregen to use the Touchscreen - https://github.com/PaulStoffregen/XPT2046_Touchscreen
// Note: this library doesn't require further configuration
#include <XPT2046_Touchscreen.h>
#include <time.h>
#include "mycreds.h"
#include "unicorn.h"

// #include <Fonts/FreeSansBold12pt7b.h>  // Medium size for labels
// #include <Fonts/FreeSansBold24pt7b.h>  // For the glucose difference
// #include <Fonts/FreeSansBold42pt7b.h>  // Largest available for main reading

// Define ESP32 Pins for ILI9341
// #define TFT_CS 5  // Chip Select
// #define TFT_DC 2  // Data/Command
// #define TFT_RST 4 // Reset

#define BACKLIGHT_PIN 21
#define SPEAKER_PIN 26
#define CHANNEL 0

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Initialize Display
TFT_eSPI tft = TFT_eSPI();
// Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
// Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

#define GLUCOSE_HIGH 180
#define GLUCOSE_LOW 80
#define GLUCOSE_URGENT_LOW 70

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);


const char *dexcomAuthenticateURL = "https://share2.dexcom.com/ShareWebServices/Services/General/AuthenticatePublisherAccount";
const char *dexcomLoginURL = "https://share2.dexcom.com/ShareWebServices/Services/General/LoginPublisherAccountById";
const char *dexcomDataURL = "https://share2.dexcom.com/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues";

const char *applicationId = "d8665ade-9673-4e27-9ff6-92db4ce13d13"; // This is a constant for the Dexcom API

String accountId = "";
String sessionId = "";

float current_glucose_mgdl = 0;
float previous_glucose_mgdl = 0;
float glucose_diff = 0;

float glucose_mmol = 0;
String trend = "Stable";
String timestamp = "N/A";

String lastRawTime = "N/A";

bool loggedIn = false;
bool refreshSynced = false;
int fetchDelay = 15000;
int brightness = 128;
int rotation = 1;

// Trend direction mapping
const char *DEXCOM_TREND_DIRECTIONS[] = {
    "None",          // 0 - Unconfirmed
    "DoubleUp",      // 1 - Rapidly rising
    "SingleUp",      // 2 - Rising
    "FortyFiveUp",   // 3 - Slowly rising
    "Flat",          // 4 - Stable
    "FortyFiveDown", // 5 - Slowly falling
    "SingleDown",    // 6 - Falling
    "DoubleDown",    // 7 - Rapidly falling
    "NotComputable", // 8 - Unconfirmed
    "RateOutOfRange" // 9 - Unconfirmed
};

// Trend arrows for display
const char *DEXCOM_TREND_ARROWS[] = {
    "",         // 0 - No data
    "\x18\x18", // 1 - DoubleUp (↑↑)
    "\x18",     // 2 - SingleUp (↑)
    "\x1E",     // 3 - FortyFiveUp (↗)
    "\x1A",     // 4 - Flat (→)
    "\x1F",     // 5 - FortyFiveDown (↘)
    "\x19",     // 6 - SingleDown (↓)
    "\x19\x19", // 7 - DoubleDown (↓↓)
    "?",        // 8 - NotComputable
    "-"         // 9 - RateOutOfRange
};

bool fetchGlucoseData(bool notifyOld = false);

void setup()
{
    pinMode(SPEAKER_PIN, OUTPUT);

    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        logPrint("Connecting to WiFi...");
    }
    logPrint("Connected to WiFi");


    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSPI);

    // Initialize Display
    // SPI.begin(23, 19, 18, 5);
    tft.init();
    setBrightNess();
    tft.setRotation(rotation);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(30, 30);
    tft.print("Glucose Monitor!");

    tft.setCursor(30, 120);
    tft.print("Loading Glucose Data...");

    delay(2000);

    // loggedIn = authenticateToDexcom();

    // if (authenticateToDexcom())
    // {
    //     logPrint(accountId);
    //     if (loginToDexcom())
    //     {
    //         logPrint(sessionId);
    //         fetchGlucoseData();
    //     }
    // }
    // fetchGlucoseData();
}

void logPrint(String message) {
    unsigned long currentMillis = millis();

    // Calculate hours, minutes, seconds, and milliseconds
    unsigned long hours = currentMillis / 3600000;
    unsigned long minutes = (currentMillis % 3600000) / 60000;
    unsigned long seconds = (currentMillis % 60000) / 1000;
    unsigned long ms = currentMillis % 1000;
    
    // Format the timestamp as HH:MM:SS.mmm
    char timestamp[16];
    sprintf(timestamp, "%02lu:%02lu:%02lu.%03lu", hours, minutes, seconds, ms);
    
    // Print the timestamp and the message
    Serial.print("[");
    Serial.print(timestamp);
    Serial.print("] ");
    Serial.println(message);
}


void setBrightNess()
{
    pinMode(BACKLIGHT_PIN, OUTPUT);
    analogWrite(BACKLIGHT_PIN, brightness); 
}

void loop()
{
    static unsigned long lastFetchTime = 0;
    static unsigned long lastWiFiCheckTime = 0;
    static unsigned long lastLoginCheckTime = 0;
    static unsigned long lastSyncTime = 0;
    static unsigned long lastTouchTime = 0;
    unsigned long currentMillis = millis();

    if (currentMillis - lastWiFiCheckTime >= 300000)
    { // Check WiFi every 5 minutes
        checkWiFiConnection();
        lastWiFiCheckTime = currentMillis;
    }

    if (!loggedIn)
    {
        if (lastLoginCheckTime == 0 || currentMillis - lastLoginCheckTime >= 30000)
        {
            lastLoginCheckTime = currentMillis;
            loggedIn = authenticateToDexcom();
        }
    }

    if (refreshSynced && currentMillis - lastSyncTime >= 3600000) // redo sync every hour
    {
        refreshSynced = false;
    }

    if (loggedIn)
    {
      if (lastFetchTime == 0 || currentMillis - lastFetchTime >= fetchDelay)
      {
          if (fetchGlucoseData(refreshSynced))
          {
              if (!refreshSynced)
              {
                refreshSynced = true;
                fetchDelay = 300000;
                lastSyncTime = currentMillis;
                logPrint("Refresh synced");
              }          
          } else 
          {
              if (refreshSynced)
              {
                refreshSynced = false;
                fetchDelay = 15000;
                logPrint("Refresh desynced");  

                if (!timestamp.endsWith("(OLD)"))
                {
                  timestamp += " (OLD)";
                }
                updateDisplay();
              }                     
          }

          lastFetchTime = currentMillis;
      }
    }

    

  if (currentMillis - lastTouchTime > 300 && touchscreen.tirqTouched() && touchscreen.touched()) 
  {
      lastTouchTime = currentMillis;

      // Retrieve the touch coordinates (adjust these calls to match your library)

      TS_Point p = touchscreen.getPoint();
      int touchX = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
      int touchY = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
      int touchZ = p.z;
      
      // Check if touch is within the top-left 75x75 area
      if (touchX < 75 && touchY < 75) 
      {
          if (rotation == 3)
          {
              rotation = 1;  
          } else if (rotation == 1)
          {
              rotation = 3;
          }

          tft.setRotation(rotation);
          updateDisplay();

      } else {
          brightness -= 32;
          if (brightness <= 0) {
              brightness = 255;
          }
          setBrightNess();
      }
  }


}

bool authenticateToDexcom()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        http.begin(dexcomAuthenticateURL);
        http.addHeader("Content-Type", "application/json");

        // JSON payload for login
        String authenticatePayload = "{\"accountName\":\"" + String(dexcomUsername) + "\",\"password\":\"" + String(dexcomPassword) + "\",\"applicationId\":\"" + String(applicationId) + "\"}";

        int httpCode = http.POST(authenticatePayload);

        if (httpCode == HTTP_CODE_OK)
        {
            accountId = http.getString();
            accountId.replace("\"", ""); // Remove quotes
            logPrint("Dexcom Authenticate Session ID: " + accountId);
            http.end();
            return loginToDexcom();
        }
        else
        {
            Serial.printf("Authenticate failed, HTTP code: %d\n", httpCode);
            http.end();
            return false;
        }
    }
    return false;
}

bool loginToDexcom()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        http.begin(dexcomLoginURL);
        http.addHeader("Content-Type", "application/json");

        // JSON payload for login
        String loginPayload = "{\"accountId\":\"" + String(accountId) + "\",\"password\":\"" + String(dexcomPassword) + "\",\"applicationId\":\"" + String(applicationId) + "\"}";

        int httpCode = http.POST(loginPayload);

        if (httpCode == HTTP_CODE_OK)
        {
            sessionId = http.getString();
            sessionId.replace("\"", ""); // Remove quotes
            logPrint("Dexcom Login Session ID: " + sessionId);
            http.end();
            // playSuccessTone();
            // playCriticalAlertLow();
            // playAlertNoData();
            return true;
        }
        else
        {
            Serial.printf("Login failed, HTTP code: %d\n", httpCode);
            http.end();
            return false;
        }
    }
    return false;
}


void playSuccessTone() {
    tone(SPEAKER_PIN, 880, 100);  // A5
    delay(100);
    tone(SPEAKER_PIN, 1047, 100); // C6
    delay(100);
    noTone(SPEAKER_PIN);
}

bool fetchGlucoseData(bool notifyOld)
{
    bool ret = false;

    if (WiFi.status() == WL_CONNECTED && sessionId != "")
    {
        HTTPClient http;
        http.begin(dexcomDataURL);
        http.addHeader("Content-Type", "application/json");

        // Request last 4 glucose values
        String fetchPayload = "{\"sessionId\":\"" + sessionId + "\",\"minutes\":1440,\"maxCount\":4}";

        int httpCode = http.POST(fetchPayload);

        if (httpCode == HTTP_CODE_OK)
        {
            String payload = http.getString();
            logPrint("Response: " + payload);

            // Check for "SessionNotValid" in response
            if (payload.indexOf("SessionNotValid") != -1)
            {
                logPrint("Session expired! Re-authenticating...");

                // Re-login
                if (loginToDexcom())
                {
                    logPrint("Re-login successful. Retrying glucose data fetch...");
                    fetchGlucoseData(); // Retry after successful login
                }
                else
                {
                    logPrint("Re-login failed!");
                }
                return ret; // Stop execution here
            }

            // Parse JSON
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, payload);

            if (!error && doc.size() >= 3)
            {
                int difIndex = checkDiff(doc) ? 2 : 1;
                previous_glucose_mgdl = doc[difIndex]["Value"];
                current_glucose_mgdl = doc[0]["Value"];
                glucose_mmol = current_glucose_mgdl / 18;
                trend = String(doc[0]["Trend"]);

                // Extract & format timestamp
                String rawTime = doc[0]["DT"]; // Example: "Date(1741497044189-0500)"
                timestamp = formatTimestamp(rawTime);

                if (notifyOld)
                {
                    if (rawTime == lastRawTime)
                    {
                        timestamp += " (OLD)";
                    }
                }

                if (lastRawTime != "N/A" && lastRawTime != rawTime)
                {
                    ret = true;
                }

                // Calculate difference
                glucose_diff = current_glucose_mgdl - previous_glucose_mgdl;

                char logBuffer[128]; // Make sure this buffer is large enough for your message
                snprintf(logBuffer, sizeof(logBuffer), "Glucose: %.0f mg/dL | Trend: %s | Change: %+0.1f | Time: %s",
                      current_glucose_mgdl, trend.c_str(), glucose_diff, timestamp.c_str());
                logPrint(String(logBuffer));


                if (refreshSynced || lastRawTime != rawTime)
                {
                  updateDisplay(); // Update LCD
                }

                if (current_glucose_mgdl < GLUCOSE_URGENT_LOW)
                {
                    playAlertUrgentLow();
                } else if (glucose_diff*2 + current_glucose_mgdl < GLUCOSE_LOW)
                {
                    playAlertLow();
                } else if (current_glucose_mgdl < GLUCOSE_HIGH && glucose_diff*2 + current_glucose_mgdl > GLUCOSE_HIGH)
                {
                    playAlertHigh();
                }
                else if (timestamp.endsWith("(OLD)"))
                {
                    playAlertNoData();
                }


                lastRawTime = rawTime;
            }
            else
            {
                Serial.print("JSON Deserialization Error: ");
                logPrint(error.c_str());
            }
        }
        else
        {
            Serial.printf("Failed to get glucose data, HTTP code: %d\n", httpCode);
            loggedIn = false;
            refreshSynced = false;
        }
        http.end();
    }
    else
    {
        logPrint("WiFi not connected or invalid session ID.");
    }

    return ret;
}

bool checkDiff(DynamicJsonDocument &doc)
{
    if (doc.size() < 3)
        return false;

    time_t time0 = extractUnixTime(doc[0]["DT"]);
    time_t time1 = extractUnixTime(doc[1]["DT"]);
    // logPrint(time0);
    // logPrint(time1);
    // logPrint(abs(time0 - time1) < 300);

    return abs(time0 - time1) < 300;
}

time_t extractUnixTime(String rawTime)
{
    if (rawTime.startsWith("Date("))
    {
        int startPos = 5;
        int dashPos = rawTime.indexOf('-', startPos);
        if (dashPos == -1)
            dashPos = rawTime.indexOf('+', startPos);

        if (dashPos > startPos)
        {
            String timestampStr = rawTime.substring(startPos, dashPos);
            return strtoull(timestampStr.c_str(), NULL, 10) / 1000;
        }
    }
    return 0;
}

String formatTimestamp(String rawTime)
{
    char result[10]; // Increased buffer size for "HH:MM AM/PM" plus null terminator
    
    if (rawTime.startsWith("Date("))
    {
        // Find the positions of important parts
        int startPos = 5; // After "Date("
        int dashPos = rawTime.indexOf('-', startPos);
        int endPos = rawTime.indexOf(')', dashPos);
        
        if (dashPos > startPos && endPos > dashPos)
        {
            // Extract timestamp (milliseconds since epoch)
            String timestampStr = rawTime.substring(startPos, dashPos);
            uint64_t timestamp = strtoull(timestampStr.c_str(), NULL, 10);
            
            // Extract timezone offset
            String tzOffsetStr = rawTime.substring(dashPos + 1, endPos);
            int tzOffset = tzOffsetStr.toInt();
            
            // Convert timezone from HHMM format to hours
            int tzHours = tzOffset / 100;
            int tzMinutes = tzOffset % 100;
            
            // Convert milliseconds to seconds
            time_t seconds = timestamp / 1000;
            
            // Apply timezone offset
            seconds -= (tzHours * 3600 + tzMinutes * 60);
            
            // Convert to tm structure
            struct tm timeInfo;
            gmtime_r(&seconds, &timeInfo);
            
            // Get hour and determine AM/PM
            int hour = timeInfo.tm_hour;
            const char* ampm = hour >= 12 ? "PM" : "AM";
            
            // Convert to 12-hour format
            hour = hour % 12;
            if (hour == 0) hour = 12; // Handle midnight (0) as 12
            
            // Format as "H:MM AM/PM"
            sprintf(result, "%d:%02d %s", hour, timeInfo.tm_min, ampm);
            
            return String(result);
        }
    }
    
    return "N/A";
}

// Function to format the timestamp
// String formatTimestamp(String rawTime)
// {
//     char result[6]; // Buffer for "HH:MM" plus null terminator

//     if (rawTime.startsWith("Date("))
//     {
//         // Find the positions of important parts
//         int startPos = 5; // After "Date("
//         int dashPos = rawTime.indexOf('-', startPos);
//         int endPos = rawTime.indexOf(')', dashPos);

//         if (dashPos > startPos && endPos > dashPos)
//         {
//             // Extract timestamp (milliseconds since epoch)
//             String timestampStr = rawTime.substring(startPos, dashPos);
//             uint64_t timestamp = strtoull(timestampStr.c_str(), NULL, 10);

//             // Extract timezone offset
//             String tzOffsetStr = rawTime.substring(dashPos + 1, endPos);
//             int tzOffset = tzOffsetStr.toInt();

//             // Convert timezone from HHMM format to hours
//             int tzHours = tzOffset / 100;
//             int tzMinutes = tzOffset % 100;

//             // Convert milliseconds to seconds
//             time_t seconds = timestamp / 1000;

//             // Apply timezone offset
//             seconds -= (tzHours * 3600 + tzMinutes * 60);

//             // Convert to tm structure
//             struct tm timeInfo;
//             gmtime_r(&seconds, &timeInfo);

//             // Format as HH:MM
//             sprintf(result, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);

//             return String(result);
//         }
//     }

//     return "N/A";
// }

// Draw a 45-degree up-right arrow
void drawDiagonalUpArrow(int x, int y, int size, uint16_t color)
{
    // Arrow body
    tft.drawLine(x, y, x + size, y - size, color);
    // Arrow head
    tft.drawLine(x + size, y - size, x + size - (size / 3), y - size + (size / 3), color);
    tft.drawLine(x + size, y - size, x + size - (size / 3), y - size, color);
}

// Draw a 45-degree down-right arrow
void drawDiagonalDownArrow(int x, int y, int size, uint16_t color)
{
    // Arrow body
    tft.drawLine(x, y, x + size, y + size, color);
    // Arrow head
    tft.drawLine(x + size, y + size, x + size - (size / 3), y + size - (size / 3), color);
    tft.drawLine(x + size, y + size, x + size - (size / 3), y + size, color);
}

void updateDisplay()
{
    tft.fillScreen(TFT_BLACK);

    // Title
    // tft.setTextColor(TFT_WHITE);
    // tft.setTextSize(2);
    // tft.setCursor(20, 10);
    // tft.print("Glucose Monitor");

    int colorBasedOnGlucose = TFT_GREEN;

    if (current_glucose_mgdl > GLUCOSE_HIGH)
    {
        colorBasedOnGlucose = TFT_YELLOW;
    }
    else if (current_glucose_mgdl < GLUCOSE_LOW)
    {
        colorBasedOnGlucose = TFT_RED;
    }
    else
    {
        colorBasedOnGlucose = TFT_GREEN;
    }

    // Glucose mg/dL
    // tft.setTextColor(colorBasedOnGlucose);
    // tft.setTextSize(24);
    // tft.setCursor(40, 70);
    // tft.printf("%.0f", current_glucose_mgdl);

    // Set text datum to middle center
    tft.setTextDatum(MC_DATUM);

    // Load the largest font (Font64rle)
    // tft.loadFont("Font64rle");
    tft.setTextFont(7);
    tft.setTextSize(3);

    tft.setTextColor(colorBasedOnGlucose, TFT_BLACK);

    // Calculate center position
    int centerX = tft.width() / 2;
    int centerY = tft.height() / 2 - 40; // Slightly below center

    if (current_glucose_mgdl >= 100 && current_glucose_mgdl < 200)
    {
      centerX -= 30;
    }

    // Draw glucose value centered
    tft.drawNumber(round(current_glucose_mgdl), centerX, centerY);

    // Unload font to free memory
    // tft.unloadFont();

    // tft.setTextFont();
      tft.setTextFont(1);
      tft.setTextSize(4);

    // Glucose Change Indicator
    // tft.setTextSize(4);
    tft.setCursor(120, 160);
    if (glucose_diff > 0)
    {
        tft.printf("+%.0f", glucose_diff);
    }
    else if (glucose_diff < 0)
    {
        tft.printf("%.0f", glucose_diff);
    }
    else
    {
        tft.setTextColor(TFT_WHITE);
        tft.print("+0");
    }


    // **Trend Arrow & Text**
    tft.setTextSize(4);
    tft.setCursor(40, 170);

    int trendIndex = -1;
    for (int i = 0; i < 10; i++)
    {
        if (trend == DEXCOM_TREND_DIRECTIONS[i])
        {
            trendIndex = i;
            break;
        }
    }

    // Handle display based on trend index
    if (trendIndex != -1)
    {
        // For FortyFiveUp (3) and FortyFiveDown (5), use custom arrows
        if (trendIndex == 3)
        { // FortyFiveUp
            trend = "Increasing";
            drawDiagonalUpArrow(190, 185, 20, colorBasedOnGlucose);
        }
        else if (trendIndex == 5)
        { // FortyFiveDown
            trend = "Decreasing";
            drawDiagonalDownArrow(190, 160, 20, colorBasedOnGlucose);
        }
        else
        {
            // For all other trends, use the character arrows
            tft.setCursor(190, 160);
            tft.print(DEXCOM_TREND_ARROWS[trendIndex]);
        }
    }
    else
    {
        // Unknown trend
        tft.setCursor(190, 160);
        tft.print("?");
    }

    // Trend Text
    // tft.setTextSize(2);
    // tft.setCursor(100, 180);
    // tft.print(trend);

    // Timestamp
    if (timestamp.endsWith("(OLD)"))
    {
        tft.setTextColor(TFT_RED);
    }
    else
    {
        tft.setTextColor(TFT_WHITE);
    }

    tft.setTextSize(2);
    tft.setCursor(30, 210);
    tft.print("Updated: ");
    tft.print(timestamp);

    if (round(current_glucose_mgdl) == 100)
    {
      tft.pushImage(20, 155, UNICORN_SIZE, UNICORN_SIZE, epd_bitmap_unicorn_small);
      tft.pushImage(268, 155, UNICORN_SIZE, UNICORN_SIZE, epd_bitmap_unicorn_small);
    }
   
}

void displayWiFiStatus(bool status)
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(40, 60);

    if (status)
    {
        tft.print("WiFi Connected");
    }
    else
    {
        tft.setTextColor(TFT_RED);
        tft.print("WiFi Down...");
        tft.setCursor(40, 90);
        tft.print("Reconnecting...");
    }
}

void checkWiFiConnection()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        logPrint("WiFi connection is down, reconnecting...");
        displayWiFiStatus(false); // Show on TFT screen

        WiFi.disconnect();
        WiFi.reconnect();

        unsigned long startAttemptTime = millis();

        // Try reconnecting for 30 seconds
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000)
        {
            delay(1000);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            logPrint("\nReconnected to WiFi");
            displayWiFiStatus(true); // Show success on TFT
            delay(2000);             // Keep message visible before updating
            updateDisplay();         // Refresh glucose data display
        }
        else
        {
            logPrint("\nWiFi reconnection failed");
        }
    }
}

void playAlertNoData() {
  tone(SPEAKER_PIN, 100, 300); delay(150);
  tone(SPEAKER_PIN, 100, 300); delay(500);
  noTone(SPEAKER_PIN);
}

void playAlertLow() {
  tone(SPEAKER_PIN, 500, 300); delay(300);
  tone(SPEAKER_PIN, 300, 300); delay(300);
  tone(SPEAKER_PIN, 100, 500); delay(500);
  noTone(SPEAKER_PIN);
}

void playAlertHigh() {
  tone(SPEAKER_PIN, 100, 300); delay(300);
  tone(SPEAKER_PIN, 300, 300); delay(300);
  tone(SPEAKER_PIN, 500, 500); delay(500);
  noTone(SPEAKER_PIN);
}

void playAlertUrgentLow() {
  tone(SPEAKER_PIN, 300, 300); delay(500);
  tone(SPEAKER_PIN, 100, 300); delay(200);
  tone(SPEAKER_PIN, 100, 300); delay(800);
  tone(SPEAKER_PIN, 300, 300); delay(500);
  tone(SPEAKER_PIN, 100, 300); delay(200);
  tone(SPEAKER_PIN, 100, 300); delay(800);
  tone(SPEAKER_PIN, 300, 300); delay(500);
  tone(SPEAKER_PIN, 100, 300); delay(200);
  tone(SPEAKER_PIN, 100, 300); delay(800);
  noTone(SPEAKER_PIN);
}
