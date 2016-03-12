//#define USE_SPIFFS
#define USE_SD

#ifdef USE_SPIFFS
#include <FS.h>
#endif

#ifdef USE_SD
#include <SD.h>
#endif

#include <ESP8266WiFi.h>

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>        //https://github.com/tzapu/WiFiManager

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <time.h>
#include <vector>

#include <Wire.h>
#include <ArduCAM.h>
#include <SPI.h>
#include <memorysaver.h>

#ifdef USE_SPIFFS
#include <ESP8266FtpServer.h>
#endif

extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}

typedef struct {
  int           verification;
  int           cycles;
  unsigned long sleepTime;
  time_t        lastTimestamp;
  int           driftCalibration;
  boolean       execute;
} rtcStore __attribute__((aligned(4))); // not sure if the aligned stuff is necessary, some posts suggest it is for RTC memory?

rtcStore rtcMem;

#define RTC_VALIDATION 4579
#define DRIFT_CALIBRATION 200
#define CALIBRATION_CYCLES 60

#define MAX_SLEEP_INTERVAL_US (1000 * 1000 * 60 * 30)

#define AT(h, m) (60 * m + 60 * 60 * h)

//int schedule[] = {AT(12, 30), AT(12, 40), AT(13, 25), AT(14, 35), AT(15, 30), AT(16, 00), AT(17, 00), AT(18, 00)};
//int* schedule = 0;
//int scheduleSize = 0;

//either define schedule here or at begining of sketch with helper
std::vector<int> schedule;// = {AT(12, 30), AT(12, 40), AT(13, 25), AT(14, 35), AT(15, 30), AT(16, 00), AT(17, 00), AT(18, 00)};

time_t currentTimestamp;

/////////////
// Arducam //
/////////////
// Here we define a maximum framelength to 64 bytes. Default is 256.
#define MAX_FRAME_LENGTH 64
// set GPIO2 as the slave select :
const int CS = 2;
const int SD_CS = 0;

boolean arducamEnabled = false;

ArduCAM arduCAM(OV2640, CS);
ESP8266WebServer server(80);
#ifdef USE_SPIFFS
FtpServer ftpSrv;   //set #define FTP_DEBUG in ESP8266FtpServer.h to see ftp verbose on serial
#endif

//holds the current upload
File fsUploadFile;

void start_capture() {
  arduCAM.clear_fifo_flag();
  arduCAM.start_capture();
}



void arduCAMCapture() {
  start_capture();
  Serial.println("CAM Capturing");

  int total_time = 0;

  total_time = millis();
  while (!arduCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
  total_time = millis() - total_time;
  Serial.print("capture total_time used (in miliseconds):");
  Serial.println(total_time, DEC);
  Serial.println("CAM Capture Done!");
}


#ifdef USE_SPIFFS
void arduCAMSaveToFile(char *fileName) {
  int total_time = millis();

  size_t len = arduCAM.read_fifo_length();
  if (len >= 393216) {
    Serial.println("Over size.");
    return;
  } else if (len == 0 ) {
    Serial.println("Size is 0.");
    return;
  }

  arduCAM.CS_LOW();
  arduCAM.set_fifo_burst();
  SPI.transfer(0xFF);


  File f = SPIFFS.open(fileName, "w");

  static const size_t bufferSize = 2048;
  static uint8_t buffer[bufferSize] = {0xFF};

  while (len) {
    size_t will_copy = (len < bufferSize) ? len : bufferSize;
    SPI.transferBytes(&buffer[0], &buffer[0], will_copy);
    //if (!client.connected()) break;
    //client.write(&buffer[0], will_copy);
    f.write(&buffer[0], will_copy);
    len -= will_copy;
  }

  arduCAM.CS_HIGH();
  f.close();

  total_time = millis() - total_time;
  Serial.print("save total_time used (in miliseconds):");
  Serial.println(total_time, DEC);
  Serial.println("CAM Save Done!");
}
#endif

#ifdef USE_SPIFFS
void arduCAMSaveToFileAndHTTP() {
  int total_time = millis();
  WiFiClient client = server.client();

  size_t len = arduCAM.read_fifo_length();
  if (len >= 393216) {
    Serial.println("Over size.");
    return;
  } else if (len == 0 ) {
    Serial.println("Size is 0.");
    return;
  }

  arduCAM.CS_LOW();
  arduCAM.set_fifo_burst();
  SPI.transfer(0xFF);

  File f = SPIFFS.open("/capture.jpg", "w");


  if (!client.connected()) return;
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: image/jpeg\r\n";
  response += "Content-Length: " + String(len) + "\r\n\r\n";
  server.sendContent(response);

  static const size_t bufferSize = 2048;
  static uint8_t buffer[bufferSize] = {0xFF};

  while (len) {
    size_t will_copy = (len < bufferSize) ? len : bufferSize;
    SPI.transferBytes(&buffer[0], &buffer[0], will_copy);
    if (!client.connected()) break;
    client.write(&buffer[0], will_copy);
    f.write(&buffer[0], will_copy);
    len -= will_copy;
  }

  arduCAM.CS_HIGH();
  f.close();

  total_time = millis() - total_time;
  Serial.print("save total_time used (in miliseconds):");
  Serial.println(total_time, DEC);
  Serial.println("CAM Save Done!");
}
#endif

/*
void arduCAMSaveToSDFile(char *fileName) {
  int total_time = millis();

  size_t len = arduCAM.read_fifo_length();
  if (len >= 393216) {
    Serial.println("Over size.");
    return;
  } else if (len == 0 ) {
    Serial.println("Size is 0.");
    return;
  }

  arduCAM.CS_LOW();
  arduCAM.set_fifo_burst();
  SPI.transfer(0xFF);

  File f = SD.open(fileName, O_WRITE | O_CREAT | O_TRUNC);

  static const size_t bufferSize = 2048;
  static uint8_t buffer[bufferSize] = {0xFF};

  while (len) {
    size_t will_copy = (len < bufferSize) ? len : bufferSize;
    SPI.transferBytes(&buffer[0], &buffer[0], will_copy);
    //if (!client.connected()) break;
    //client.write(&buffer[0], will_copy);
    f.write(buffer, will_copy);
    len -= will_copy;
    delay(0);
  }

  arduCAM.CS_HIGH();
  f.close();

  total_time = millis() - total_time;
  Serial.print("save total_time used (in miliseconds):");
  Serial.println(total_time, DEC);
  Serial.println("CAM Save Done!");
}
*/

void arduCAMSaveToSDFile(char *fileName) {
  int total_time = millis();

  char str[8];
  File outFile;
  byte buf[256];
  static int i = 0;
  static int k = 0;
  static int n = 0;
  uint8_t temp, temp_last;
  
  outFile = SD.open(fileName, O_WRITE | O_CREAT | O_TRUNC);
  if (! outFile)
  {
    Serial.println("open file failed");
    return;
  }
  i = 0;
  arduCAM.CS_LOW();
  arduCAM.set_fifo_burst();
  temp = SPI.transfer(0x00);
  //
  //Read JPEG data from FIFO
  while ( (temp != 0xD9) | (temp_last != 0xFF))
  {
    temp_last = temp;
    temp = SPI.transfer(0x00);

    //Write image data to buffer if not full
    if (i < 256)
      buf[i++] = temp;
    else
    {
      //Write 256 bytes image data to file
      arduCAM.CS_HIGH();
      outFile.write(buf, 256);
      i = 0;
      buf[i++] = temp;
      arduCAM.CS_LOW();
      arduCAM.set_fifo_burst();
    }
    delay(0);
  }
  //Write the remain bytes in the buffer
  if (i > 0)
  {
    arduCAM.CS_HIGH();
    outFile.write(buf, i);
  }
  //Close the file
  outFile.close();
  
  total_time = millis() - total_time;
  Serial.print("save total_time used (in miliseconds):");
  Serial.println(total_time, DEC);
  Serial.println("CAM Save Done!");

}

boolean initArduCAM() {
  uint8_t vid, pid;
  uint8_t temp;

  if (arducamEnabled) {
    return true;
  }
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  delay(100);
  Wire.begin();

  Serial.println("ArduCAM Start!");

  // set the CS as an output:
  pinMode(CS, OUTPUT);

  // initialize SPI:
  SPI.begin();
  SPI.setFrequency(4000000); //4MHz

  //Check if the ArduCAM SPI bus is OK
  arduCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  temp = arduCAM.read_reg(ARDUCHIP_TEST1);
  if (temp != 0x55) {
    Serial.println("SPI1 interface Error!");
    return false;
  }

  //Check if the camera module type is OV2640
  arduCAM.wrSensorReg8_8(0xff, 0x01);
  arduCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
  arduCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
  if ((vid != 0x26) || (pid != 0x41)) {
    Serial.println("Can't find OV2640 module!");
    return false;
  } else {
    Serial.println("OV2640 detected.");
  }

  //Change to JPEG capture mode and initialize the OV2640 module
  arduCAM.set_format(JPEG);
  arduCAM.InitCAM();
  arduCAM.OV2640_set_JPEG_size(OV2640_1600x1200);
  //arduCAM.OV2640_set_JPEG_size(OV2640_640x480);
  //arduCAM.OV2640_set_JPEG_size(OV2640_320x240);
  arduCAM.clear_fifo_flag();
  arduCAM.write_reg(ARDUCHIP_FRAMES, 0x00);
  delay(1600);
  Serial.println("ArduCam initialised");
  arducamEnabled = true;
  return true;
}

boolean captureToFile(char *fileName) {
  if (!initArduCAM()) {
    return false;
  }

  //SPIFFS.format();
  //SPIFFS.begin();
  //Serial.println("FS started");
  if (!SD.begin(SD_CS))
  {
    //while (1);    //If failed, stop here
    Serial.println("SD Card Error");
  }
  else
  {
    Serial.println("SD Card detected!");
  }

  arduCAMCapture();

  //arduCAMSaveToFile(fileName);
  arduCAMSaveToSDFile(fileName);
  return true;
}

/*********************************
   HTTP ARDUCAM REQUEST HANDLERS
 ********************************/

void httpCaptureRequest() {
  initArduCAM();
  start_capture();
  arduCAMCapture();
  
#ifdef USE_SPIFFS
  arduCAMSaveToFileAndHTTP();
  #endif
}


void httpStreamRequest() {
  initArduCAM();

  WiFiClient client = server.client();

  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (1) {
    start_capture();
    //Serial.println("start stream");
    while (!arduCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));

    size_t len = arduCAM.read_fifo_length();
    if (len >= 393216) {
      Serial.println("Over size.");
      continue;
    } else if (len == 0 ) {
      Serial.println("Size is 0.");
      continue;
    }

    arduCAM.CS_LOW();
    arduCAM.set_fifo_burst();
    SPI.transfer(0xFF);

    if (!client.connected()) break;
    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);

    static const size_t bufferSize = 2048;
    static uint8_t buffer[bufferSize] = {0xFF};

    while (len) {
      size_t will_copy = (len < bufferSize) ? len : bufferSize;
      SPI.transferBytes(&buffer[0], &buffer[0], will_copy);
      if (!client.connected()) break;
      client.write(&buffer[0], will_copy);
      len -= will_copy;
    }
    arduCAM.CS_HIGH();

    if (!client.connected()) break;
  }
}


/*********************************
          HTTP INTERFACE
 ********************************/

#ifdef USE_SPIFFS

//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

String getContentType(String filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path) {
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload() {
  if (server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}

void handleFileDelete() {
  if (server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileDelete: " + path);
  if (path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if (!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate() {
  if (server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if (path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if (SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if (file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while (dir.next()) {
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }

  output += "]";
  server.send(200, "text/json", output);
}
#endif

void handleNotFound() {
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text/plain", message);

  if (server.hasArg("ql")) {
    int ql = server.arg("ql").toInt();
    arduCAM.OV2640_set_JPEG_size(ql);
    Serial.println("QL change to: " + server.arg("ql"));
  }
}

/*********************************
         RTC & SCHEDULING
 ********************************/

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
  Serial.print("EXECUTE: ");
  Serial.println(rtcMem.execute);
}

void gotoSleep() {
  float spent = millis() / 1000 + rtcMem.driftCalibration / 1000;
  int timestamp =  currentTimestamp + round(spent);
  int today = timestamp % 86400;
  int sleepFor = MAX_SLEEP_INTERVAL_US;
  int arrCount = schedule.size();//sizeof(schedule) / sizeof(schedule[0]);
  boolean foundSchedule = false;
  boolean execute = false;
  char buffer[6];

  for (int i = 0; i < arrCount; i++) {


    if (schedule[i] <= today) {
      //Serial.println(" passed");
    } else {
      sprintf(buffer, "%02d:%02d ", schedule[i] / 60 / 60, (schedule[i] % (60 * 60)) / 60);
      Serial.print(buffer);
      Serial.println(" next");
      foundSchedule = true;
      if (schedule[i] - today < (MAX_SLEEP_INTERVAL_US / 1000 / 1000)) {
        sleepFor = (schedule[i] - today) * 1000 * 1000;
        execute = true;
      }
      break;
    }
  }

  if (!foundSchedule) {
    Serial.println("no schedule found");
    int remaining = 86400 - ((sleepFor / 1000 / 1000) + today);
    if (remaining < 0) {
      Serial.println("next day wrap");
      Serial.println(remaining);
      if ((schedule[0] < (remaining * -1))) {
        Serial.println("Schedule at start of next day");
        sleepFor = (schedule[0] + 86400 - today) * 1000 * 1000;
        foundSchedule = true;
        execute = true;
      }
    }
  }

  //failsafe so we don t go to sleep and never return
  if (sleepFor == 0) {
    sleepFor = 10 * 1000 * 1000;
  }

  rtcMem.verification = RTC_VALIDATION;
  rtcMem.cycles++;
  rtcMem.execute = execute;
  //Serial.print("wtf ");
  //Serial.println((millis() + rtcMem.driftCalibration) / 1000);
  spent = millis() / 1000 + rtcMem.driftCalibration / 1000;
  Serial.print("spent: ");
  Serial.println(spent);
  //Serial.print(ctime(&currentTimestamp));
  rtcMem.lastTimestamp = currentTimestamp + round(spent);
  //Serial.print(ctime(&rtcMem.lastTimestamp));
  rtcMem.sleepTime = sleepFor / 1000 / 1000;
  system_rtc_mem_write(65, &rtcMem, sizeof(rtcMem));
  printRTCMem();
  ESP.deepSleep(sleepFor, WAKE_RF_DEFAULT);
  delay(100);
}

//start from, to, every N minutes
void scheduleEveryNMinutes(int from, int to, int interval) {
  //change from minutes to seconds
  interval = interval * 60;
  Serial.println(from);
  Serial.println(to);
  Serial.println(interval);
  int count = int((to - from) / interval);
  int tm = 0;
  char buffer[6];
  for (int i = 0; i < count; i++) {
    tm = from + i * interval;
    schedule.push_back(tm);
    //sprintf(buffer, "Added: %02d:%02d", tm / 60 / 60, (tm % (60 * 60)) / 60);
    //Serial.println(buffer);
  }
}

/*********************************
          SETUP AND LOOP
 ********************************/

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
  Serial.println("** Start");
  //Serial.println(system_get_rtc_time());
  Serial.println(ESP.getResetReason());
  //Serial.println(ESP.getResetInfo());

  system_rtc_mem_read(65, &rtcMem, sizeof(rtcMem));
  printRTCMem();
  if (rtcMem.verification != RTC_VALIDATION) {
    Serial.println("** Invalid values");
    rtcMem.cycles = 0;
    rtcMem.sleepTime = 0;
    rtcMem.lastTimestamp = 0;
    rtcMem.driftCalibration = 200; //ms
    rtcMem.execute = false; //don t execute on first run
    rtcMem.execute = true; //execute on first run
  }
  currentTimestamp = rtcMem.lastTimestamp + rtcMem.sleepTime;
  //printRTCMem();



  int a = analogRead(A0);
  Serial.println(a);
  //if (a < 512 ) {
  if (a > 512 ) {
    // **NORMAL MODE** //

    Serial.println("** Normal start");

    //schedule[] = {AT(12, 30), AT(12, 40), AT(13, 25), AT(14, 35), AT(15, 30), AT(16, 00), AT(17, 00), AT(18, 00)};
    //schedule = new int [8];
    //schedule[] = {AT(12, 30), AT(12, 40), AT(13, 25), AT(14, 35), AT(15, 30), AT(16, 00), AT(17, 00), AT(18, 00)};
    //start, end, interval minutes
    scheduleEveryNMinutes(AT(7, 00), AT(19, 00), 30);

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

      //now = 1455148200;
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
      rtcMem.driftCalibration += ((drift * 1000) / CALIBRATION_CYCLES);
      printRTCMem();
    }
    //Serial.println(ctime(&now));
    //Serial.println(now);

    if (rtcMem.execute) {
      Serial.println("** Executing");
      //take picture
      char fileName[50];
      struct tm *localTime = localtime(&now);

      //long filename
      //sprintf(fileName, "/%d.%02d.%02d_%02d%02d.jpg", localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday + 1, localTime->tm_hour, localTime->tm_min);
      //8.3 filename
      sprintf(fileName, "%02d%02d%02d%02d.jpg", localTime->tm_mon + 1, localTime->tm_mday + 1, localTime->tm_hour, localTime->tm_min);

      Serial.println(fileName);

      if (!captureToFile(fileName)) {
        Serial.println("*** Capture failed");
      }
    }


    Serial.println();
    Serial.println("go to sleep");
    gotoSleep();


  } else {
    // **CONFIG MODE** //

    Serial.println("** Enter config");
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

    //download additional files to spiff if they are not there



    //start browsing pics/and capture portal
    //SPIFFS.format();
#ifdef USE_SPIFFS
    if (SPIFFS.begin()) {
      Serial.println("SPIFFS opened!");
      ftpSrv.begin("esp8266", "esp8266");   //username, password for ftp.  set ports in ESP8266FtpServer.h  (default 21, 50009 for PASV)
    }
#endif
    //SERVER INIT

    // Start the server
    server.on("/capture", HTTP_GET, httpCaptureRequest);
    server.on("/stream", HTTP_GET, httpStreamRequest);
    server.onNotFound(handleNotFound);

#ifdef USE_SPIFFS
    server.onNotFound([]() {
      if (!handleFileRead(server.uri()))
        handleNotFound();
    });
#endif
    //list directory
    
#ifdef USE_SPIFFS
    server.on("/list", HTTP_GET, handleFileList);
    //load editor
    server.on("/edit", HTTP_GET, []() {
      if (!handleFileRead("/index.htm")) server.send(404, "text/plain", "FileNotFound");
    });
    //create file
    server.on("/edit", HTTP_PUT, handleFileCreate);
    //delete file
    server.on("/edit", HTTP_DELETE, handleFileDelete);
    //first callback is called after the request has ended with all parsed arguments
    //second callback handles file uploads at that location
    server.on("/edit", HTTP_POST, []() {
      server.send(200, "text/plain", "");
    }, handleFileUpload);

    //get heap status, analog input value and all GPIO statuses in one json call
    server.on("/all", HTTP_GET, []() {
      String json = "{";
      json += "\"heap\":" + String(ESP.getFreeHeap());
      json += ", \"analog\":" + String(analogRead(A0));
      json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
      json += "}";
      server.send(200, "text/json", json);
      json = String();
    });
    #endif
    server.begin();
    Serial.println("HTTP server started");


    return;
  }

}

void loop() {
  //we only get here in config mode
  ArduinoOTA.handle();
  server.handleClient();
#ifdef USE_SPIFFS
  ftpSrv.handleFTP();
#endif
}

