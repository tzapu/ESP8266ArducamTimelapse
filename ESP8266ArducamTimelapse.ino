#include <ESP8266WiFi.h>

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>        //https://github.com/tzapu/WiFiManager

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <time.h>

void setup() {
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

    WiFiManager wifi;
    if (!wifi.autoConnect("Cashula")) {
      delay(1000);
      ESP.reset();
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
  ESP.deepSleep(1000 * 1000 * 30);
  delay(1000);
}

void loop() {
  ArduinoOTA.handle();
}

