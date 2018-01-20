/*
  MQTT SERVER:
  mqtt.thingspeak.com

  TOPIC:
  channels/177083/publish/KLPZ79R8Q0MSJOWM

  channel2
  channels/236853/publish/1UV9V9GVHVP7SLRC

  channel 3
  channels/316750/publish/OGBF9DTUDWYMIRIT

*/

/********************************************************/
/*                    ATMOSCAN                          */
/*                                                      */
/* Author: Marc Finns 2017                              */
/*                                                      */
/*  Components:                                         */
/*      - NodeMCU ESP8266 ESP-12E                       */
/*      - HDC1080 Humidity & Temperature Sensor         */
/*      - Plantower PMS7003 Particle sensor             */
/*      - MZ-H19 CO2 sensor                             */
/*      - Air Quality Sensor (based on Winsen MP503)    */
/*      - BME 280 Pressure & humidity sensor            */
/*                                                      */
/*  Wiring:                                             */
/*                                                      */
/*  Analog:                                             */
/*      - AirQuality out    => ADC A0                   */
/*  I2C:                                                */
/*      - BMP280    SCL     => GPIO5                    */
/*      - BMP280    SDA     => GPIO4                    */
/*      - HDC1080   SCL     => GPIO5                    */
/*      - HDC1080   SDA     => GPIO4                    */
/*      - PAJ7620U  SCL     => GPIO5                    */
/*      - PAJ7620U  SDA     => GPIO4                    */
/*      - MiCS6814  SCL     => GPIO5                    */
/*      - MiCS6814  SDA     => GPIO4                    */
/*  SPI:                                                */
/*      - ILI9341   SCK     => GPIO14                   */
/*      - ILI9341   MISO    NC                          */
/*      - ILI9341   MOSI    => GPIO13                   */
/*      - ILI9341   D/C     => GPIO16                   */
/*      - ILI9341   RST     => RST                      */
/*      - ILI9341   CS      => GND                      */
/*      - ILI9341   LED     => GPIO12 (via MOS driver)  */
/* Serial:                                              */
/*      - PMS7003   TX      => RXD (GPIO0)              */
/*      - PMS7003   RX      => TXD (GPIO2)              */
/*      - MH-Z19    TX      => GPIO (SW RX)             */
/*      - MH-Z19    RX      => GPIO (SW TX)             */
/* Interrupts:                                          */
/*      - _Gesture          => GPIO10                   */
/*      - Geiger            => GPIO15                   */
/********************************************************/

// -------------------------------------------------------
// Libraries
// -------------------------------------------------------
#define FS_NO_GLOBALS
#include <FS.h>                   //this needs to be first
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include "SPI.h"

#include <Syslog.h>               // https://github.com/arcao/ESP8266_Syslog
#include <Average.h>              // https://github.com/MajenkoLibraries/Average
#include <PubSubClient.h>         // https://github.com/knolleary/pubsubclient
#include <ProcessScheduler.h>     // https://github.com/wizard97/ArduinoProcessScheduler
#include <NtpClientLib.h>         // https://github.com/gmag11/NtpClient
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include <TFT_eSPI.h>             // https://github.com/Bodmer/TFT_eSPI
#include <TimeSpace.h>            // https://github.com/MarcFinns/TimeSpaceLib

// My libraries
#include "GlobalDefinitions.h"
#include "ScreenFactory.h"

// Screens
#include "ScreenSensors.h"
#include "ScreenStatus.h"
#include "ScreenSetup.h"
#include "ScreenGeiger.h"
#include "ScreenPlaneSpotter.h"
#include "ScreenWeatherStation.h"
#include "ScreenErrLog.h"

// TO BE SORTED
#include "Artwork.h"
#include "ArialRoundedMTBold_14.h"
#include "ArialRoundedMTBold_36.h"
#include "Free_Fonts.h"
#include "Bitmaps.h"

#include <Wire.h>

extern "C" {
#include "user_interface.h"
}



// -------------------------------------------------------
//  Globals
// -------------------------------------------------------

// Unique board ID
String systemID;

// Turbo flag
bool turbo = false;

// LCD Screen
TFT_eSPI LCD = TFT_eSPI();

// Global UI management
GfxUi ui(&LCD);

// UDP instance to let us send and receive packets over UDP
WiFiUDP udpClient;

WiFiClient wifiClient;


// Create a new empty syslog instance
Syslog syslog(udpClient, SYSLOG_PROTO_IETF);

// Configuration
Configuration config;


// Download manager
WebResource webResource;

// Register screens with Screen Factory
ScreenCreatorImpl<ScreenSetup> creator0;
ScreenCreatorImpl<ScreenSensors> creator1;
ScreenCreatorImpl<ScreenStatus> creator2;
ScreenCreatorImpl<ScreenErrLog> creator3;
ScreenCreatorImpl<ScreenGeiger> creator4;
ScreenCreatorImpl<ScreenPlaneSpotter> creator5;
ScreenCreatorImpl<ScreenWeatherStation> creator6;



// Create a global Scheduler object
Scheduler sched;

// Create processes
ProcessContainer procPtr =
{
  Proc_ComboTemperatureHumiditySensor(sched,
  MEDIUM_PRIORITY,
  SLOW_SAMPLE_PERIOD,
  RUNTIME_FOREVER),

  Proc_ComboPressureHumiditySensor(sched,
  MEDIUM_PRIORITY,
  SLOW_SAMPLE_PERIOD,
  RUNTIME_FOREVER),

  Proc_CO2Sensor(sched,
  MEDIUM_PRIORITY,
  SLOW_SAMPLE_PERIOD,
  RUNTIME_FOREVER),

  Proc_ParticleSensor(sched,
  MEDIUM_PRIORITY,
  SLOW_SAMPLE_PERIOD,
  RUNTIME_FOREVER),

  Proc_VOCSensor(sched,
  MEDIUM_PRIORITY,
  SLOW_SAMPLE_PERIOD,
  RUNTIME_FOREVER),

  Proc_MultiGasSensor(sched,
  MEDIUM_PRIORITY,
  SLOW_SAMPLE_PERIOD,
  RUNTIME_FOREVER),

  Proc_GeigerSensor(sched,
  MEDIUM_PRIORITY,
  FAST_SAMPLE_PERIOD,
  RUNTIME_FOREVER),

  Proc_UIManager(sched,
  HIGH_PRIORITY,
  SLOW_SAMPLE_PERIOD,
  RUNTIME_FOREVER),

  Proc_MQTTUpdate(sched,
  MEDIUM_PRIORITY,
  MQTT_UPDATE_PERIOD,
  RUNTIME_FOREVER),

  Proc_GeoLocation(sched,
  MEDIUM_PRIORITY,
  GEOLOC_RETRY_PERIOD,
  RUNTIME_FOREVER)

};


void setup()
{

#ifdef DEBUG_SERIAL
  Serial.begin(115200);
#endif

  // Wait for electronics to settle
  delay(2000);

  systemID = F("ATMOSCAN-");
  systemID += WiFi.macAddress();
  systemID.replace(F(":"), F(""));

  // Setup the LCD
  LCD.init();

  LCD.setRotation(2);
  LCD.fillScreen(TFT_BLACK);

  // **********************  Splash screen
  ui.drawBitmap(Splash_Screen, (LCD.width() - splashWidth) / 2, 20, splashWidth, splashHeight);

  // Initialise text engine
  //LCD.setTextColor(TFT_YELLOW, TFT_BLACK);
  LCD.setTextColor(TFT_WHITE, TFT_BLACK);
  LCD.setTextWrap(false);

  LCD.setFreeFont(FSS9);
  int  xpos = 0;
  int  ypos = 20; //170;

  LCD.setTextDatum(BR_DATUM);
  LCD.drawString(ATMOSCAN_VERSION, xpos, ypos, GFXFF);

  xpos = 240;
  LCD.setTextDatum(BL_DATUM);
  LCD.drawString(F("(c) 2017 MarcFinns"), xpos, ypos, GFXFF);

  // **********************  Credits: Libraries
  ypos = 215;

  LCD.drawRect(0, ypos, 240, 105, TFT_WHITE);
  LCD.setFreeFont(&Dialog_plain_9);

  ypos += 13;
  int lineSpacing = 9;

  LCD.setTextDatum(BC_DATUM);
  LCD.drawString(F("INCLUDES LIBRARIES FROM:"), 120, ypos, GFXFF);

  LCD.setTextDatum(BL_DATUM);

  ypos +=  lineSpacing + 6;
  LCD.drawString(F("Adafruit"), 6, ypos, GFXFF);
  LCD.drawString(F("ClosedCube"), 85, ypos, GFXFF);
  LCD.drawString(F("Seeed"), 170, ypos, GFXFF);

  ypos +=  lineSpacing;
  LCD.drawString(F("Arcao"), 6, ypos, GFXFF);
  LCD.drawString(F("Gmag11"), 85, ypos, GFXFF);
  LCD.drawString(F("Squix78"), 170, ypos, GFXFF);

  ypos +=  lineSpacing;
  LCD.drawString(F("Bblanchon"), 6, ypos, GFXFF);
  LCD.drawString(F("Knolleary"), 85, ypos, GFXFF);
  LCD.drawString(F("Tzapu"), 170, ypos, GFXFF);

  ypos +=  lineSpacing;
  LCD.drawString(F("Bodmer"), 6, ypos, GFXFF);
  LCD.drawString(F("Lucadentella"), 85, ypos, GFXFF);
  LCD.drawString(F("Wizard97"), 170, ypos, GFXFF);

  // ********************* Credits: web services

  ypos +=  lineSpacing + 6;

  LCD.setTextDatum(BC_DATUM);
  LCD.drawString(F("INTEGRATES WEB SERVICES FROM:"), 120, ypos, GFXFF);

  LCD.setTextDatum(BL_DATUM);

  ypos +=  lineSpacing + 6;
  LCD.drawString(F("Adsbexchange.com"), 6, ypos, GFXFF);
  LCD.drawString(F("GeoNames.org"), 122, ypos, GFXFF);

  ypos +=  lineSpacing;
  LCD.drawString(F("Google.com"), 6, ypos, GFXFF);
  LCD.drawString(F("Wunderground.com"), 122, ypos, GFXFF);

  ypos +=  lineSpacing;
  LCD.drawString(F("Timezonedb.com"), 6, ypos, GFXFF);
  LCD.drawString(F("Mylnikov.org"), 122, ypos, GFXFF);


  /////////////////////////////////////////////////////
  // clean SPIFFS FS, for testing
  // SPIFFS.format();
  /////////////////////////////////////////////////////

  // **********************  Initialization screen

  // Initialise I2C bus
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // Create scheduler and process objects
  initProcesses();

  // Turn on LCD and wait a bit on splash screen...
  procPtr.UIManager.displayOn();
  delay (1000);

  LCD.setFreeFont(FSS9);
  LCD.setTextDatum(TL_DATUM);
  LCD.setTextColor(TFT_YELLOW, TFT_BLACK);
  xpos = 6;
  ypos = 195;

  // Retrieve dynamic config parameters from SPIFSS such as MQTT Topic and servers

  // Draw progress bar...
  ui.drawProgressBar(10, 175, 240 - 20, 15, 0, TFT_YELLOW, TFT_BLUE);

  // Show init step...
  LCD.drawString(F("Retrieving configuration...       "), xpos, ypos, GFXFF);
  ui.drawProgressBar(10, 175, 240 - 20, 15, 30, TFT_YELLOW, TFT_BLUE);

  delay(1000);
  if (retrieveConfig())
  {
    config.configValid = true;

    // Show init step...
    LCD.drawString(F("Connecting to network...    "), xpos, ypos, GFXFF);
    ui.drawProgressBar(10, 175, 240 - 20, 15, 60, TFT_YELLOW, TFT_BLUE);

    delay(1000);

    // Handle Wifi connection, including dynamic config parameters such as MQTT Topic and server
    config.connected = wifiConnect();

    // Initialize time library
    initNTP();

    // Prepare syslog configuration
    syslog.server(config.syslog_server, SYSLOG_PORT);
    syslog.deviceHostname(systemID.c_str());
    syslog.appName(APP_NAME);
    syslog.defaultPriority(LOG_KERN);

    syslog.log(LOG_INFO, "******* Booting firmware " + String(ATMOSCAN_VERSION) + ", Built " + String(__DATE__ " " __TIME__) + " ******* ");

    // Log current configuration
    syslog.log(LOG_INFO, "Connected to network " + WiFi.SSID() + " with address " + WiFi.localIP().toString());


#ifdef DEBUG_SYSLOG
    delay(1000); // Syslog does not like too many messages at a time...
    syslog.log(LOG_DEBUG, F("Configuration is:"));
    syslog.log(LOG_DEBUG, config.mqtt_server);
    syslog.log(LOG_DEBUG, config.mqtt_topic1);
    syslog.log(LOG_DEBUG, config.mqtt_topic2);
    syslog.log(LOG_DEBUG, config.syslog_server);
#endif

#ifdef DEBUG_SERIAL
    Serial.println(F("Configuration is:"));
    Serial.println(config.mqtt_server);
    Serial.println(config.mqtt_topic1);
    Serial.println(config.mqtt_topic2);
    Serial.println(config.syslog_server);
#endif

#ifdef DEBUG_SYSLOG
    // Log ESP configuration
    logESPconfig();
#endif
  }
  else
  {
    // If no valid config, force configuration screen
    config.configValid = false;
    config.startScreen = SETUP_SCREEN;
  }

  // Initialise OTA
  initOTA();

  // Show init step...
  LCD.drawString(F("Starting processes...         "), xpos, ypos, GFXFF);
  ui.drawProgressBar(10, 175, 240 - 20, 15, 100, TFT_YELLOW, TFT_BLUE);

  // Start processes
  startProcesses();

  delay(500);

  // Cleanup old maps from SPIFFS
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "SPIFFS dir listing:");
#endif
  String fileName;
  fs::Dir dir = SPIFFS.openDir(F("/"));
  while (dir.next())
  {
    fileName = dir.fileName();
    if (fileName.startsWith(F("/map")))
    {
#ifdef DEBUG_SYSLOG
      syslog.log(LOG_DEBUG, " FOUND " + fileName);
#endif
      bool outcome = SPIFFS.remove(fileName);

#ifdef DEBUG_SYSLOG
      syslog.log(LOG_DEBUG, " FILE REMOVAL " + String(outcome));
#endif

    }
  }

  syslog.log(LOG_INFO, F("************ INITIALIZATION COMPLETE, STARTING PROCESSES *************"));
}


void loop()
{

  // Handle OTA
  ArduinoOTA.handle();

  // Invoke scheduler
  sched.run();

  // Feed the WatchDog
  ESP.wdtFeed();

}


///////////////////////////////// OTA firmware update management

int lastOTAprogressPercentual = 0;

void initOTA()
{
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  //Sset Hostname
  ArduinoOTA.setHostname(systemID.c_str());

  // Default password
  ArduinoOTA.setPassword((const char *)"123456");

  ArduinoOTA.onStart([]()
  {
    // Turn on backlight
    LCD.setRotation(2);
    procPtr.UIManager.displayOn();
    LCD.fillScreen(TFT_BLUE);
    LCD.setTextColor(TFT_YELLOW, TFT_BLUE);
    LCD.setFreeFont(FSS9);
    //LCD.setTextSize(1);
    LCD.setTextDatum(BC_DATUM);
    LCD.drawString(F("OTA Firmware update"), 120, 20, GFXFF);
    syslog.log(LOG_INFO, F("OTA Update Start"));
  });

  ArduinoOTA.onEnd([]()
  {
    LCD.setTextDatum(BL_DATUM);
    LCD.drawString(F("Rebooting..."), 0, 120, GFXFF);
    syslog.log(LOG_INFO, F("OTA Update End - Rebooting"));
    delay(3000);
    ESP.restart();
  });


  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
  {
    int progressPercentual = progress / (total / 100.0);
    if (progressPercentual != lastOTAprogressPercentual)
    {
      // Print OTA progress
      LCD.drawString(String(progress) + "/" + String (total) + " bytes (" + progressPercentual + "%)", 120, 50, GFXFF);
      syslog.logf(LOG_INFO, "OTA Progress = %u%%\r", progressPercentual);
      ui.drawProgressBar(10, 60, 240 - 20, 15, progressPercentual, TFT_YELLOW, TFT_YELLOW);
      lastOTAprogressPercentual = progressPercentual;
    }
  });

  ArduinoOTA.onError([](ota_error_t error)
  {
    syslog.logf(LOG_ERR, "OTA Update Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      LCD.drawString(F("OTA Auth Failed"), 0, 100, GFXFF);
      errLog( F("OTA Auth Failed"));
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      LCD.drawString(F("OTA Update Begin Failed"), 0, 100, GFXFF);
      errLog( F("OTA Update Begin Failed"));
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      LCD.drawString(F("OTA Update Connect Failed"), 0, 100, GFXFF);
      errLog( F("OTA Update Connect Failed"));
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      LCD.drawString(F("OTA Update Receive Failed"), 0, 100, GFXFF);
      errLog( F("OTA Update Receive Failed"));
    }
    else if (error == OTA_END_ERROR)
    {
      LCD.drawString(F("OTA Update End Failed"), 0, 100, GFXFF);
      errLog( F("OTA Update End Failed"));
    }

    delay(3000);
    ESP.restart();
  });

  // OTA Setup
  String hostname(systemID);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);
  ArduinoOTA.setHostname((const char *)hostname.c_str());

  // Begin listening
  ArduinoOTA.begin();

}

// Manage network disconnection
void onSTADisconnected(WiFiEventStationModeDisconnected event_info)
{

#ifdef DEBUG_SERIAL
  Serial.println("WiFi disconnected");
#endif

  config.connected = false;
}

// Manage network reconnection
void onSTAGotIP(WiFiEventStationModeGotIP ipInfo)
{
#ifdef DEBUG_SERIAL
  Serial.println("WiFi Connected");
#endif
  config.connected =  true;
}


void initNTP()
{
  static WiFiEventHandler e1, e2;

  // NTP start
  NTP.onNTPSyncEvent([](NTPSyncEvent_t error)
  {
    if (error)
    {
      if (error == noResponse)
      {
        errLog(F("NTP server not reachable"));
      }
      else if (error == invalidAddress)
      {
        errLog(F("Invalid NTP server address"));
      }
    }
    else
    {
      syslog.log(LOG_INFO, "Got NTP time - " + NTP.getTimeDateString(NTP.getLastNTPSync()));
    }
  });

  e1 = WiFi.onStationModeGotIP(onSTAGotIP);
  e2 = WiFi.onStationModeDisconnected(onSTADisconnected);
}


void initProcesses()
{

  // Add processes to the scheduler
  procPtr.UIManager.add();
  procPtr.MQTTUpdate.add();
  procPtr.GeoLocation.add();
  procPtr.ComboTemperatureHumiditySensor.add();
  procPtr.ComboPressureHumiditySensor.add();
  procPtr.CO2Sensor.add();
  procPtr.ParticleSensor.add();
  procPtr.VOCSensor.add();
  procPtr.MultiGasSensor.add();
  procPtr.GeigerSensor.add();

}

void startProcesses()
{
  // Enable processes
  procPtr.ComboTemperatureHumiditySensor.enable();
  procPtr.UIManager.enable();
  procPtr.ComboPressureHumiditySensor.enable();
  procPtr.ParticleSensor.enable();
  procPtr.CO2Sensor.enable();
  procPtr.VOCSensor.enable();
  procPtr.MultiGasSensor.enable();
  procPtr.MQTTUpdate.enable();
  procPtr.GeigerSensor.enable();
  procPtr.GeoLocation.enable();
}


bool retrieveConfig()
{
  // Read configuration from FS json
  if (SPIFFS.begin())
  {
    if (SPIFFS.exists(F("/config.json")))
    {
#ifdef DEBUG_SERIAL
      Serial.println("Configuration found");
#endif

      //file exists, reading and loading
      fs::File configFile = SPIFFS.open(F("/config.json"), "r");
      if (configFile)
      {
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject &json = jsonBuffer.parseObject(buf.get());

        if (json.success())
        {
#ifdef DEBUG_SERIAL
          Serial.println("JSON parse success ");
#endif

          strcpy(config.mqtt_server, json[F("mqtt_server")]);
          strcpy(config.mqtt_topic1, json[F("mqtt_topic1")]);
          strcpy(config.mqtt_topic2, json[F("mqtt_topic2")]);
          strcpy(config.mqtt_topic3, json[F("mqtt_topic3")]);
          strcpy(config.syslog_server, json[F("syslog_server")]);
          return true;

        }
        else
        {
#ifdef DEBUG_SERIAL
          Serial.println("JSON parse failed ");
#endif
          // LCD.print(F(" Error parsing"));
          delay(2000);
          SPIFFS.format();
          ESP.eraseConfig();
          return false;
        }
      }
    }
    else
    {
#ifdef DEBUG_SERIAL
      Serial.println(F("JSON File does not exist!"));
#endif
      delay(2000);
      ESP.eraseConfig();
      return false;
    }
  }
  else
  {
    LCD.println(F(" > Could not access file system"));
#ifdef DEBUG_SERIAL
    Serial.println(F("Could access file system!"));
#endif
    return false;
  }
}

bool wifiConnect()
{
  // Connect to WiFi using last good credentials

  WiFi.mode(WIFI_STA);
  WiFi.begin();
  // Wait for connection...
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED && counter++ < 30)
  {
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED )
  {
    return true;
  }
  else
  {
    return false;
  }
}


#ifdef DEBUG_SYSLOG
void logESPconfig()
{
  // Log ESP configuration
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  syslog.logf(LOG_DEBUG, "Flash real id:   %08X\n", ESP.getFlashChipId());
  syslog.logf(LOG_DEBUG, "Flash real size: %u\n\n", realSize);
  syslog.logf(LOG_DEBUG, "Flash ide  size: %u\n", ideSize);
  syslog.logf(LOG_DEBUG, "Flash ide speed: %u\n", ESP.getFlashChipSpeed());
  syslog.logf(LOG_DEBUG, "Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

  if (ideSize != realSize)
  {
    errLog( F("Flash Chip configuration wrong!\n"));
  }
  else
  {
    syslog.log(LOG_DEBUG, F("Flash Chip configuration ok.\n"));
  }
}
#endif


void setTurbo(bool setTurbo)
{

  if (setTurbo)
  {
#ifdef DEBUG_SYSLOG
    syslog.log(LOG_DEBUG, F("Set TURBO mode"));
#endif
    system_update_cpu_freq(160);
    turbo = true;
  }
  else
  {
#ifdef DEBUG_SYSLOG
    syslog.log(LOG_DEBUG, F("Set NORMAL mode"));
#endif
    system_update_cpu_freq(80);
    turbo = false;
  }

}

bool isTurbo()
{
  return turbo;
}

void errLog(String msg)
{
  // Log time
  char logTime[25];
  sprintf(logTime, "[%d/%d/%d %d:%02d.%02d] ", day(), month(), year(), hour(), minute(), second());

  // Manage buffer
  if (config.lastErrors.isFull())
  {
    // Remove oldest (outgoing) element
    String first;
    config.lastErrors.pull(&first);
  }

  config.lastErrors.add(String(logTime) + msg);
  syslog.log(LOG_ERR, msg);
}


