#include "P_AirSensors.h"

#include "GlobalDefinitions.h"
#include "Arduino.h"

#include <ProcessScheduler.h>     // https://github.com/wizard97/ArduinoProcessScheduler
#include <Syslog.h>                 // https://github.com/arcao/ESP8266_Syslog
#include <Adafruit_BME280.h>        // https://github.com/adafruit/Adafruit_BME280_Library
#include <MutichannelGasSensor.h>   // https://github.com/Seeed-Studio/Mutichannel_Gas_Sensor


// #define DEBUG_SYSLOG


// External variables
extern Syslog syslog;
extern struct ProcessContainer procPtr;
extern struct Configuration config;

// Prototypes
void errLog(String msg);

// Geiger tube definitions
#define LND712_CONV_FACTOR  123 // CPS * 1/123 = uSv/h

// Particle sensor PMS7003 definitions
#define PMS7003_COMMAND_SIZE 7
#define PMS7003_RESPONSE_SIZE 32

// CO2 Sensor MH-Z19 definitions
#define MHZ19_COMMAND_SIZE 9
#define MHZ19_RESPONSE_SIZE 9


// Particle sensor PMS7003 definitions
static const  char PMS7003_cmdPassiveEnable[] = {0x42, 0x4d, 0xe1, 0x00, 0x00, 0x01, 0x70};
static const  char PMS7003_cmdPassiveRead[] = {0x42, 0x4d, 0xe2, 0x00, 0x00, 0x01, 0x71};
static const  char PMS7003_cmdSleep[] = {0x42, 0x4d, 0xe4, 0x00, 0x00, 0x01, 0x73};
static const  char PMS7003_cmdWakeup[] = {0x42, 0x4d, 0xe4, 0x00, 0x01, 0x01, 0x74};

// CO2 Sensor MH-Z19 definitions
static const byte MHZ19_cmdRead[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};


// Sensor processes implementation

// -------------------------------------------------------
// Combo Temperature & Umidity Sensor wrapper (HDC1080)
// -------------------------------------------------------

Proc_ComboTemperatureHumiditySensor::Proc_ComboTemperatureHumiditySensor(Scheduler &manager, ProcPriority pr, unsigned int period, int iterations)
  :  Process(manager, pr, period, iterations),
     avgTemperature(AVERAGING_WINDOW),
     avgHumidity(AVERAGING_WINDOW) {}

void Proc_ComboTemperatureHumiditySensor::setup()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_ComboTemperatureHumiditySensor::setup()");
#endif

  //  Sensor begin
  hdc1080.begin(0x40);

  // Is the device reachable?
  if (!(hdc1080.readDeviceId() == 0x1050))
  {
    // There was a problem detecting the sensor
    errLog(F("Could not find a valid hdc1080 sensor"));
  }

#ifdef DEBUG_SYSLOG
  syslog.log(LOG_INFO, "Manufacturer ID=" + String((hdc1080.readManufacturerId(), HEX)));  // 0x5449 ID of Texas Instruments
  syslog.log(LOG_INFO, "Device ID=" + String((hdc1080.readDeviceId(), HEX)));  // 0x1050 ID of the device
#endif
}

void Proc_ComboTemperatureHumiditySensor::service()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_ComboTemperatureHumiditySensor::service()");
#endif

  // Get temperature event
  float temp = hdc1080.readTemperature();

  // Get humidity event
  float humidity = hdc1080.readHumidity();

  // averages
  avgTemperature.push(temp - 1.5L); // NOTE: empirical correction based on observations, TBC
  avgHumidity.push(humidity);

}

float Proc_ComboTemperatureHumiditySensor::getTemperature()
{
  return avgTemperature.mean();
}

float Proc_ComboTemperatureHumiditySensor::getHumidity()
{
  return avgHumidity.mean();
}
// END Combo Temperature & Umidity Sensor wrapper (HDC1080)


// -------------------------------------------------------
// Combo Pressure & humidity Sensor process (BME280)
// -------------------------------------------------------

Proc_ComboPressureHumiditySensor::Proc_ComboPressureHumiditySensor(Scheduler &manager, ProcPriority pr, unsigned int period, int iterations)
  :  Process(manager, pr, period, iterations),
     avgPressure(AVERAGING_WINDOW),
     avgHumidity(AVERAGING_WINDOW),
     avgTemperature(AVERAGING_WINDOW)
{
}

void Proc_ComboPressureHumiditySensor::setup()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_ComboPressureHumiditySensor::setup()");
#endif

  // Initialise the sensor
  if (!bme.begin(0x76))
  {
    // There was a problem detecting the sensor
    errLog(F("No valid BME280 sensor"));
  }
}

void Proc_ComboPressureHumiditySensor::service()
{
  // syslog.log(LOG_DEBUG, "2 - BME280");
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_ComboPressureHumiditySensor::service()");
#endif

  // Get values
  float pressure = bme.readPressure() / 100.0F;
  float humidity = bme.readHumidity();
  float temperature = bme.readTemperature();

  avgPressure.push(pressure);
  avgHumidity.push(humidity);
  avgTemperature.push(temperature);

}


float Proc_ComboPressureHumiditySensor::getPressure()
{
  return avgPressure.mean();
}

float Proc_ComboPressureHumiditySensor::getHumidity()
{
  return avgHumidity.mean();
}

float Proc_ComboPressureHumiditySensor::getTemperature()
{
  return avgTemperature.mean();
}
// END Pressure Sensor process (BME280)


// -------------------------------------------------------
// CO2 Sensor process (MH-Z19)
// -------------------------------------------------------

Proc_CO2Sensor::Proc_CO2Sensor(Scheduler &manager, ProcPriority pr, unsigned int period, int iterations)
  :  Process(manager, pr, period, iterations),
     avgCO2(AVERAGING_WINDOW),
     co2(CO2_RX_PIN, CO2_TX_PIN, false, 256)
{
}

void Proc_CO2Sensor::setup()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_CO2Sensor::setup()");
#endif

  // SW  for CO2 sensor MH-Z19
  co2.begin(9600);

  // dummy read to clear buffer
  co2.write(MHZ19_cmdRead, MHZ19_COMMAND_SIZE); //request PPM CO2

  // Receive more data than needed, to empty  buffer (should generate  timeout)
  unsigned char response[32];
  co2.readBytes(response, MHZ19_RESPONSE_SIZE);

  // Log BUFFER
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "MH-Z19 RESPONSE " + bytes2hex(response, sizeof(response)));
#endif

}

void Proc_CO2Sensor::service()
{
  // syslog.log(LOG_DEBUG, "3 - MH-Z19");
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_CO2Sensor::service()");
#endif

  unsigned char Buffer[MHZ19_RESPONSE_SIZE];

#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Reading  for CO2 data");
#endif

  //request PPM CO2
  co2.write(MHZ19_cmdRead, MHZ19_COMMAND_SIZE);

  // Read response
  co2.readBytes(Buffer, MHZ19_RESPONSE_SIZE);

  //  PRINT BUFFER
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "CO2 sensor response - " + bytes2hex(Buffer, MHZ19_RESPONSE_SIZE));
#endif

  if (Buffer[0] != 0xFF)
  {
    delay(1000);
    errLog(F("CO2 Sensor - Wrong starting byte"));

    // empty buffer
    co2.readBytes(Buffer, MHZ19_RESPONSE_SIZE * 2);

    return ;
  }

  if (Buffer[1] != 0x86)
  {
    delay(1000);
    errLog(F("CO2 Sensor - Wrong command"));

    // empty buffer
    co2.readBytes(Buffer, MHZ19_RESPONSE_SIZE * 2);

    return ;
  }

#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "CO2 Sensor - header OK");
#endif

  // Get value
  int responseHigh = (int) Buffer[2];
  int responseLow = (int) Buffer[3];
  float co2 = (256 * responseHigh) + responseLow;

  // Average
  avgCO2.push(co2);
}

float Proc_CO2Sensor::getCO2()
{
  return avgCO2.mean();
}
// END CO2 Sensor process (MH-Z19)



// -------------------------------------------------------
// Particle Sensor process (PMS7003)
// -------------------------------------------------------

Proc_ParticleSensor::Proc_ParticleSensor(Scheduler &manager, ProcPriority pr, unsigned int period, int iterations)
  :  Process(manager, pr, period, iterations),
     avgPM01(AVERAGING_WINDOW),
     avgPM2_5(AVERAGING_WINDOW),
     avgPM10(AVERAGING_WINDOW)
{
}

void Proc_ParticleSensor::setup()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_ParticleSensor::setup()");
#endif

  unsigned char Buffer[256];

  // HW  for particle sensor PMS7003
  Serial.begin(9600);

  Serial.setTimeout(3000);

  // Set passive mode
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "PMS7003 SETTING PASSIVE MODE");
#endif

  Serial.write(PMS7003_cmdPassiveEnable, 7);
  Serial.flush();

  //Dummy read to clear  buffer
  Serial.write(PMS7003_cmdPassiveRead, 7);
  Serial.flush();

  // Receive more data than needed, to empty  buffer (should generate  timeout)
  Serial.readBytes(Buffer, sizeof(Buffer));
}

void Proc_ParticleSensor::service()
{
  // syslog.log(LOG_DEBUG, "4 - PMS7003");
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_ParticleSensor::service()");
#endif

  unsigned char Buffer[PMS7003_RESPONSE_SIZE];

#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Reading  for particle data");
#endif

  // Send READ command
  Serial.write(PMS7003_cmdPassiveRead, PMS7003_COMMAND_SIZE);
  Serial.flush();

  // Receive response
  Serial.readBytes(Buffer, PMS7003_RESPONSE_SIZE);

  // PRINT BUFFER
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Particle sensor response - " + bytes2hex(Buffer, PMS7003_RESPONSE_SIZE));
#endif

  //start to read when detect 0x42 0x4d
  if (Buffer[0] == 0x42 && Buffer[1] == 0x4d)
  {
#ifdef DEBUG_SYSLOG
    syslog.log(LOG_DEBUG, "Particle sensor - header OK");
#endif

    // Is checksum ok?
    if (verifyChecksum(Buffer, PMS7003_RESPONSE_SIZE))
    {
#ifdef DEBUG_SYSLOG
      syslog.log(LOG_DEBUG, "Buffer valid");
#endif
      // Get values
      int PM01 = extractPM01(Buffer);
      int PM2_5 = extractPM2_5(Buffer);
      int PM10 = extractPM10(Buffer);

      // Average
      avgPM01.push(PM01);    //count PM1.0 value of the air detector module
      avgPM2_5.push(PM2_5);  //count PM2.5 value of the air detector module
      avgPM10.push(PM10);    //count PM10 value of the air detector module

    }
    else
    {
      errLog(F("Particle sensor - Checksum wrong"));
    }
  }
  else
  {
    errLog(F("Particle sensor -  timeout"));
  }
}


float Proc_ParticleSensor::getPM01()
{
  return avgPM01.mean();
}

float Proc_ParticleSensor::getPM2_5()
{
  return avgPM2_5.mean();
}

float Proc_ParticleSensor::getPM10()
{
  return avgPM10.mean();
}

char Proc_ParticleSensor::verifyChecksum(unsigned char *thebuf, int leng)
{
  char receiveflag = 0;
  int receiveSum = 0;

  for (int i = 0; i < (leng - 2); i++)
  {
    receiveSum = receiveSum + thebuf[i];
  }
  // receiveSum = receiveSum;// + 0x42 + 0x4d;

  if (receiveSum == ((thebuf[leng - 2] << 8) + thebuf[leng - 1])) //check the  data
  {
    receiveSum = 0;
    receiveflag = 1;
  }
  return receiveflag;
}

int Proc_ParticleSensor::extractPM01(unsigned char *thebuf)
{
  int PM01Val;
  PM01Val = ((thebuf[4] << 8) + thebuf[5]); //count PM1.0 value of the air detector module
  return PM01Val;
}

//extract PM Value to PC
int Proc_ParticleSensor::extractPM2_5(unsigned char *thebuf)
{
  int PM2_5Val;
  PM2_5Val = ((thebuf[6] << 8) + thebuf[7]); //count PM2.5 value of the air detector module
  return PM2_5Val;
}

//extract PM Value to PC
int Proc_ParticleSensor::extractPM10(unsigned char *thebuf)
{
  int PM10Val;
  PM10Val = ((thebuf[8] << 8) + thebuf[9]); //count PM10 value of the air detector module
  return PM10Val;
}

// END Particle Sensor process (PMS7003)


// -------------------------------------------------------
// VOC Sensor process (Grove - Air quality sensor v1.3)
// -------------------------------------------------------

Proc_VOCSensor::Proc_VOCSensor(Scheduler &manager, ProcPriority pr, unsigned int period, int iterations)
  :  Process(manager, pr, period, iterations),
     //avgVOC(AVERAGING_WINDOW)
     avgVOC(60)
{
}

void Proc_VOCSensor::setup()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_VOCSensor::setup()");
#endif
}

void Proc_VOCSensor::service()
{
  // syslog.log(LOG_DEBUG, "5 - VOC");
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_VOCSensor::service()");
#endif

  // Air Quality reading
  float voc = analogRead(VOC_PIN);

  // Average
  avgVOC.push(voc);
}

float Proc_VOCSensor::getVOC()
{
  return avgVOC.mean();
}
// END VOC Sensor process (Grove - Air quality sensor v1.3)

// -------------------------------------------------------
// Geiger Sensor process (LND712)
// -------------------------------------------------------

Proc_GeigerSensor *Proc_GeigerSensor::instance = nullptr;

Proc_GeigerSensor::Proc_GeigerSensor(Scheduler &manager, ProcPriority pr, unsigned int period, int iterations)
  :  Process(manager, pr, period, iterations),
     avgCPM(AVERAGING_WINDOW * 2)
{
}

void Proc_GeigerSensor::setup()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_GeigerSensor::setup()");
#endif

  instance = this;

  // Set interrupt pin as input and attach interrupt
  pinMode(GEIGER_INTERRUPT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(GEIGER_INTERRUPT_PIN), onTubeEventISR, RISING);
  counts = 0;
  avgCPM.push(10.0);
}

void Proc_GeigerSensor::service()
{

#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_GeigerSensor::service()");
#endif

  unsigned long interval = millis() - lastCountReset;

#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Geiger: counts = " + String(counts));
#endif

  // SPURIOUS INTERVAL GUARD - If interval is too short or too long we skip the reading (spurious activation of process due to starvation)
  if ((interval < FAST_SAMPLE_PERIOD * 0.9) || (interval > FAST_SAMPLE_PERIOD * 2))
  {

#ifdef DEBUG_SYSLOG
    syslog.log(LOG_DEBUG, "Geiger: skipping this reading as interval is out of range " + String(interval));
#endif
  }

  else
  {
    // INTERVAL OK, Measure normally
    float thisCPM = (float(counts) * 60000.0 / float(interval));

    // SPURIOUS MEASUREMENT GUARD - If cpm is obviously out of range, discard it 
    if (thisCPM > 100000 || thisCPM < 0)
    {
      // Spurious run
#ifdef DEBUG_SYSLOG
      syslog.log(LOG_DEBUG, "WARNING - Geiger thisCPM = " + String(thisCPM));
#endif
    }
    else
    {
      // Good run, record it
      avgCPM.push(thisCPM);
      //lastCPM = thisCPM;

#ifdef DEBUG_SYSLOG
      syslog.log(LOG_DEBUG, "Geiger last CPM = " + String(thisCPM));
      syslog.log(LOG_DEBUG, "Geiger mean CPM = " + String(avgCPM.mean()));
#endif
    }

  }

  // In any case reset counters
  lastCountReset = millis();
  counts = 0;
}

/*
  unsigned long Proc_GeigerSensor::getLastCPM()
  {
  return lastCPM;
  }
*/

float Proc_GeigerSensor::getCPM()
{
  return avgCPM.mean();
}

float Proc_GeigerSensor::getRadiation()
{
  return avgCPM.mean() / LND712_CONV_FACTOR;
}

void Proc_GeigerSensor::onTubeEventISR()
{
  instance->onTubeEvent();
}

void Proc_GeigerSensor::onTubeEvent()
{
  counts++;
}

// END Geiger Sensor wrapper (LND712)



// -------------------------------------------------------
// MultiGas Sensor process (Grove - MiCS6814)
// -------------------------------------------------------


Proc_MultiGasSensor::Proc_MultiGasSensor(Scheduler & manager, ProcPriority pr, unsigned int period, int iterations)
  :  Process(manager, pr, period, iterations),
     avgNH3(AVERAGING_WINDOW),
     avgCO(AVERAGING_WINDOW),
     avgNO2(AVERAGING_WINDOW),
     avgC3H8(AVERAGING_WINDOW),
     avgC4H10(AVERAGING_WINDOW),
     avgCH4(AVERAGING_WINDOW),
     avgH2(AVERAGING_WINDOW),
     avgC2H5OH(AVERAGING_WINDOW)

{
}

void Proc_MultiGasSensor::setup()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_MultiGasSensor::setup()");
#endif

  gas.begin(0x04);//the default I2C address of the slave is 0x04
  gas.powerOn();
  delay(1000);
  syslog.log(LOG_INFO, "MultiGas firmware Version = " + String(gas.getVersion()));
}

void Proc_MultiGasSensor::service()
{
#ifdef DEBUG_SYSLOG
  syslog.log(LOG_DEBUG, "Proc_MultiGasSensor::service()");
#endif

  float nh3, co, no2, c3h8, c4h10, ch4, h2, c2h5oh;

  // Get values
  nh3 = gas.measure_NH3();
  co = gas.measure_CO();
  no2 = gas.measure_NO2();
  c3h8 = gas.measure_C3H8();
  c4h10 = gas.measure_C4H10();
  ch4 = gas.measure_CH4();
  h2 = gas.measure_H2();
  c2h5oh = gas.measure_C2H5OH();

  // Average
  if (nh3 >= 0)
    avgNH3.push(nh3);

#ifdef DEBUG_SYSLOG
  else
    errLog( "NH3 = " + String(nh3));
#endif

  if (co >= 0)
    avgCO.push(co);

#ifdef DEBUG_SYSLOG
  else
    errLog( "CO = " + String(co));
#endif

  if (no2 >= 0)
    avgNO2.push(no2);

#ifdef DEBUG_SYSLOG
  else
    errLog( "NO2 = " + String(no2));
#endif

  if (c3h8 >= 0)
    avgC3H8.push(c3h8);

#ifdef DEBUG_SYSLOG
  else
    errLog( "c3h8 = " + String(c3h8));
#endif

  if (c4h10 >= 0)
    avgC4H10.push(c4h10);

#ifdef DEBUG_SYSLOG
  else
    errLog( "c4h10 = " + String(c4h10));
#endif

  if (ch4 >= 0)
    avgCH4.push(ch4);

#ifdef DEBUG_SYSLOG
  else
    errLog( "ch4 = " + String(ch4));
#endif

  if (h2 >= 0)
    avgH2.push(h2);

#ifdef DEBUG_SYSLOG
  else
    errLog( "h2 = " + String(h2));
#endif

  if (c2h5oh >= 0)
    avgC2H5OH.push(c2h5oh);
#ifdef DEBUG_SYSLOG
  else
    errLog( "c2h5oh = " + String(c2h5oh));
#endif

}


float Proc_MultiGasSensor::getNH3()
{
  return avgNH3.mean();
}

float Proc_MultiGasSensor::getCO()
{
  return avgCO.mean();
}

float Proc_MultiGasSensor::getNO2()
{
  return avgNO2.mean();
}

float Proc_MultiGasSensor::getC3H8()
{
  return avgC3H8.mean();
}


float Proc_MultiGasSensor::getC4H10()
{
  return avgC4H10.mean();
}

float Proc_MultiGasSensor::getCH4()
{
  return avgCH4.mean();
}

float Proc_MultiGasSensor::getH2()
{
  return avgH2.mean();
}

float Proc_MultiGasSensor::getC2H5OH()
{
  return avgC2H5OH.mean();
}


// END MultiGas Sensor wrapper (Grove - MiCS6814)


// -------------------------------------------------------
// Shared methods
// -------------------------------------------------------

String BaseSensor::bytes2hex(unsigned char buf[], int len)
{
  char onebyte[2];
  String output;
  for (int i = 0; i < len; i++)
  {
    sprintf(onebyte, "%02X", buf[i]);
    output = output  + String(onebyte) + String(":");
  }
  return output;
}


