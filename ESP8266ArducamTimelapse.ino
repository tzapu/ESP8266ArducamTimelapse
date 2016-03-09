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
  int           verification;
  int           cycles;
  unsigned long sleepTime;
  time_t        lastTimestamp;
  int           driftCalibration;
} rtcStore __attribute__((aligned(4))); // not sure if the aligned stuff is necessary, some posts suggest it is for RTC memory?

rtcStore rtcMem;

#define RTC_VALIDATION 4579
#define DRIFT_CALIBRATION 200
#define CALIBRATION_CYCLES 5

#define MAX_SLEEP_INTERVAL_US (1000 * 1000 * 30)

time_t currentTimestamp;

void printRTCMem() {
  Serial.println("RTC Mem");
  Serial.print("last timestamp: ");
  Serial.print(ctime(&rtcMem.lastTimestamp));
  Serial.print("sleep time: ");
  Serial.println(rtcMem.sleepTime);
  Serial.print("verification: ");
  Serial.println(rtcMem.verification);
  Serial.print("current time: ");
  Serial.print(ctime(&currentTimestamp));
  Serial.print("cycles: ");
  Serial.println(rtcMem.cycles);
  Serial.print("drift calibration: ");
  Serial.println(rtcMem.driftCalibration);
}

void gotoSleep() {
  rtcMem.verification = RTC_VALIDATION;
  rtcMem.cycles++;
  rtcMem.lastTimestamp = currentTimestamp + (millis() + rtcMem.driftCalibration) / 1000;
  rtcMem.sleepTime = MAX_SLEEP_INTERVAL_US / 1000 / 1000;
  system_rtc_mem_write(65, &rtcMem, sizeof(rtcMem));
  printRTCMem();
  ESP.deepSleep(MAX_SLEEP_INTERVAL_US, WAKE_RF_DEFAULT);
  delay(100);
}


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
  //Serial.println(system_get_rtc_time());
  Serial.println(ESP.getResetReason());
  //Serial.println(ESP.getResetInfo());

  system_rtc_mem_read(65, &rtcMem, sizeof(rtcMem));
  printRTCMem();
  if (rtcMem.verification != RTC_VALIDATION) {
    Serial.println("invalid values");
    rtcMem.cycles = 0;
    rtcMem.sleepTime = 0;
    rtcMem.lastTimestamp = 0;
    rtcMem.driftCalibration = 200; //ms
  }
  currentTimestamp = rtcMem.lastTimestamp + rtcMem.sleepTime;
  printRTCMem();



  int a = analogRead(A0);
  Serial.println(a);
  if (a > 512 ) {
    Serial.println("Normal start");
    time_t now = currentTimestamp;
    //if no time or resync cycle
    if (!now || rtcMem.cycles % CALIBRATION_CYCLES == 0) {
      Serial.println("We have no time saved or it s s sync cycle");

      Serial.println("deep sleep fix");
      wifi_fpm_do_wakeup();
      wifi_fpm_close();

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
      Serial.println();
      now = time(nullptr);
      int drift = 0;
      if (currentTimestamp != 0) {
        drift = (now - millis() / 1000) - currentTimestamp;
      }
      Serial.print("DRIFT: ");
      Serial.println(drift);
      Serial.println((now - millis() / 1000));
      Serial.println(currentTimestamp);
      //correct for current elapsed runtime
      currentTimestamp = now - millis() / 1000;
      rtcMem.driftCalibration += (drift * 1000) / CALIBRATION_CYCLES;
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

    //invalidate rtc
    rtcMem.verification = 0;
    system_rtc_mem_write(65, &rtcMem, sizeof(rtcMem));
    return;
  }
  Serial.println("go to sleep");
  gotoSleep();
}

void loop() {
  ArduinoOTA.handle();
}

