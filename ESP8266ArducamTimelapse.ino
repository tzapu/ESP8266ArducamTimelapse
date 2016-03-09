#include <ESP8266WiFi.h>

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>        //https://github.com/tzapu/WiFiManager

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <time.h>

extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}

typedef struct {
  byte markerFlag;
  byte counter;
  unsigned long sleepTime;
} rtcStore __attribute__((aligned(4))); // not sure if the aligned stuff is necessary, some posts suggest it is for RTC memory?

rtcStore rtcMem;

void setup() {
  wifi_station_disconnect();
  wifi_set_opmode(NULL_MODE);
  wifi_set_sleep_type(MODEM_SLEEP_T);
  wifi_fpm_open();
  wifi_fpm_do_sleep(0xFFFFFFF);

  Serial.setDebugOutput(true);
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println();
  Serial.println("Start");
  Serial.println(ESP.getResetReason());
  int val = analogRead(A0);
  Serial.println(val);
  delay(500);
  int a = analogRead(A0);
  if (a > 512 ) {
    Serial.println("Normal start");
    time_t now = time(nullptr);
    if (!now) {
      Serial.println("We have no time");

      /*
        Serial.println("deep sleep fix");
        wifi_fpm_do_wakeup();
        wifi_fpm_close();

        wifi_set_opmode(STATION_MODE);
        wifi_station_connect();
        delay(1);
      */
      Serial.println("deep sleep fix");
      wifi_fpm_do_wakeup();
      wifi_fpm_close();

      Serial.println("Reconnecting");
      //wifi_set_opmode(STATION_MODE);
      //wifi_station_connect();

      WiFiManager wifi;
      if (!wifi.autoConnect("Cashula")) {
        delay(1000);
        ESP.reset();
      }
      configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov");
      while (!time(nullptr)) {
        Serial.print(".");
        delay(100);
      }
      now = time(nullptr);
    }
    Serial.println(ctime(&now));
    Serial.println(now);

  } else {
    Serial.println("Enter config");
    Serial.println("deep sleep fix");
    wifi_fpm_do_wakeup();
    wifi_fpm_close();

    Serial.println("Reconnecting");
    //wifi_set_opmode(STATION_MODE);
    //wifi_station_connect();

    WiFiManager wifi;
    wifi.setTimeout(60);
    if (!wifi.autoConnect("Cashula")) {
      delay(1000);
      //ESP.reset();
      ESP.deepSleep(1000 * 1000 * 30, WAKE_RF_DEFAULT);
    }

    ArduinoOTA.onStart([]() {
      Serial.println("Start OTA");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return;
  }
  Serial.println("go to sleep");
  ESP.deepSleep(1000 * 1000 * 30, WAKE_RF_DEFAULT);
  delay(1000);
}

void loop() {
  ArduinoOTA.handle();
}

