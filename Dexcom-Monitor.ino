#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <time.h>

const char *ssid = "xxxx";     // Replace with your Wi-Fi SSID
const char *password = "xxxx"; // Replace with your Wi-Fi password

// Define ESP32 Pins for ILI9341
#define TFT_CS 5  // Chip Select
#define TFT_DC 2  // Data/Command
#define TFT_RST 4 // Reset

// Initialize Display
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

const char *dexcomAuthenticateURL = "https://share2.dexcom.com/ShareWebServices/Services/General/AuthenticatePublisherAccount";
const char *dexcomLoginURL = "https://share2.dexcom.com/ShareWebServices/Services/General/LoginPublisherAccountById";
const char *dexcomDataURL = "https://share2.dexcom.com/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues";

const char *dexcomUsername = "xxxx";
const char *dexcomPassword = "xxxx";

const char *applicationId = "d8665ade-9673-4e27-9ff6-92db4ce13d13"; // This is a constant for the Dexcom API

String accountId = "";
String sessionId = "";

float current_glucose_mgdl = 0;
float previous_glucose_mgdl = 0;
float glucose_diff = 0;

float glucose_mmol = 0;
String trend = "Stable";
String timestamp = "N/A";

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

void setup()
{
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    // Initialize Display
    SPI.begin(23, 19, 18, 5);
    tft.begin();
    tft.setRotation(3); // Adjust as needed
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(30, 30);
    tft.print("Glucose Monitor!");

    delay(2000); // Keep message visible

    if (authenticateToDexcom())
    {
        Serial.println(accountId);
        if (loginToDexcom())
        {
            Serial.println(sessionId);
            fetchGlucoseData();
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

        String authenticatePayload = "{\"accountName\":\"" + String(dexcomUsername) + "\",\"password\":\"" + String(dexcomPassword) + "\",\"applicationId\":\"" + String(applicationId) + "\"}";

        int httpCode = http.POST(authenticatePayload);

        if (httpCode == HTTP_CODE_OK)
        {
            accountId = http.getString();
            accountId.replace("\"", ""); // Remove quotes
            Serial.println("Dexcom Session ID: " + accountId);
            http.end();
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

bool loginToDexcom()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        http.begin(dexcomLoginURL);
        http.addHeader("Content-Type", "application/json");

        String loginPayload = "{\"accountId\":\"" + String(accountId) + "\",\"password\":\"" + String(dexcomPassword) + "\",\"applicationId\":\"" + String(applicationId) + "\"}";

        int httpCode = http.POST(loginPayload);

        if (httpCode == HTTP_CODE_OK)
        {
            sessionId = http.getString();
            sessionId.replace("\"", ""); // Remove quotes
            Serial.println("Dexcom Session ID: " + sessionId);
            http.end();
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

void fetchGlucoseData()
{
    if (WiFi.status() == WL_CONNECTED && sessionId != "")
    {
        HTTPClient http;
        http.begin(dexcomDataURL);
        http.addHeader("Content-Type", "application/json");

        String fetchPayload = "{\"sessionId\":\"" + sessionId + "\",\"minutes\":1440,\"maxCount\":4}";

        int httpCode = http.POST(fetchPayload);

        if (httpCode == HTTP_CODE_OK)
        {
            String payload = http.getString();
            Serial.println("Response: " + payload);

            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, payload);

            if (!error)
            {
                if (doc.size() >= 3)
                {                                            // Ensure we have at least 2 readings
                    previous_glucose_mgdl = doc[2]["Value"]; // Older reading
                    current_glucose_mgdl = doc[0]["Value"];  // Latest reading
                    glucose_mmol = current_glucose_mgdl / 18;
                    trend = String(doc[0]["Trend"]);

                    String rawTime = doc[0]["DT"]; // Example: "Date(1741497044189-0500)"
                    timestamp = formatTimestamp(rawTime);

                    glucose_diff = current_glucose_mgdl - previous_glucose_mgdl;

                    Serial.printf("Glucose: %.0f mg/dL | Trend: %s | Change: %+0.1f | Time: %s\n",
                                  current_glucose_mgdl, trend.c_str(), glucose_diff, timestamp.c_str());

                    updateDisplay();
                }
                else
                {
                    Serial.println("Not enough glucose data received.");
                }
            }
            else
            {
                Serial.print("JSON Deserialization Error: ");
                Serial.println(error.c_str());
            }
        }
        else
        {
            Serial.printf("Failed to get glucose data, HTTP code: %d\n", httpCode);
        }
        http.end();
    }
    else
    {
        Serial.println("WiFi not connected or invalid session ID.");
    }
}

// Function to format the timestamp
String formatTimestamp(String rawTime)
{
    char result[6]; // Buffer for "HH:MM" plus null terminator

    if (rawTime.startsWith("Date("))
    {
        int startPos = 5;
        int dashPos = rawTime.indexOf('-', startPos);
        int endPos = rawTime.indexOf(')', dashPos);

        if (dashPos > startPos && endPos > dashPos)
        {
            String timestampStr = rawTime.substring(startPos, dashPos);
            uint64_t timestamp = strtoull(timestampStr.c_str(), NULL, 10);

            String tzOffsetStr = rawTime.substring(dashPos + 1, endPos);
            int tzOffset = tzOffsetStr.toInt();

            int tzHours = tzOffset / 100;
            int tzMinutes = tzOffset % 100;

            time_t seconds = timestamp / 1000;
            seconds -= (tzHours * 3600 + tzMinutes * 60);
            struct tm timeInfo;
            gmtime_r(&seconds, &timeInfo);

            sprintf(result, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);

            return String(result);
        }
    }

    return "N/A";
}

void updateDisplay()
{
    tft.fillScreen(ILI9341_BLACK);

    // Title
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 10);
    tft.print("Glucose Monitor");

    // Glucose mg/dL
    tft.setTextSize(4);
    tft.setCursor(40, 50);
    if (current_glucose_mgdl > 180)
    {
        tft.setTextColor(ILI9341_RED);
    }
    else if (current_glucose_mgdl < 70)
    {
        tft.setTextColor(ILI9341_BLUE);
    }
    else
    {
        tft.setTextColor(ILI9341_GREEN);
    }
    tft.printf("%.0f", current_glucose_mgdl);
    tft.setTextSize(2);
    tft.setCursor(160, 60);
    tft.print("mg/dL");

    // Glucose Change Indicator
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(115, 57);
    if (glucose_diff > 0)
    {
        tft.setTextColor(ILI9341_GREEN);
        tft.printf("+%.0f", glucose_diff);
    }
    else if (glucose_diff < 0)
    {
        tft.setTextColor(ILI9341_RED);
        tft.printf("%.0f", glucose_diff);
    }
    else
    {
        tft.setTextColor(ILI9341_WHITE);
        tft.print("+0");
    }

    // Glucose mmol/L
    if (current_glucose_mgdl > 180)
    {
        tft.setTextColor(ILI9341_RED);
    }
    else if (current_glucose_mgdl < 70)
    {
        tft.setTextColor(ILI9341_BLUE);
    }
    else
    {
        tft.setTextColor(ILI9341_GREEN);
    }
    tft.setTextSize(3);
    tft.setCursor(40, 100);
    tft.printf("%.1f", glucose_mmol);
    tft.setTextSize(2);
    tft.setCursor(160, 110);
    tft.print("mmol/L");

    // **Trend Arrow & Text**
    tft.setTextSize(4);
    tft.setCursor(40, 170);

    // Find the corresponding trend arrow
    String trendString = trend;
    String arrow = "?"; // Default in case of an unknown trend

    for (int i = 0; i < 10; i++)
    {
        if (trendString == DEXCOM_TREND_DIRECTIONS[i])
        {
            arrow = DEXCOM_TREND_ARROWS[i];
            break;
        }
    }

    tft.print(arrow); // Print the arrow

    // Trend Text
    tft.setTextSize(2);
    tft.setCursor(100, 180);
    tft.print(trend);

    // Timestamp
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(40, 210);
    tft.print("Updated: ");
    tft.print(timestamp);
}

void loop()
{
    static unsigned long lastFetchTime = 0;
    unsigned long currentMillis = millis();

    if (currentMillis - lastFetchTime >= 300000)
    { // 5 minutes
        fetchGlucoseData();
        lastFetchTime = currentMillis;
    }
}