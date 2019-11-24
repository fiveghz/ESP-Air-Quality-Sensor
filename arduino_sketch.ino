/******************************************************************************
  Read basic air quality metrics and logs them to an InfluxDB database

  Eric K.

  November 24, 2019

  Components:

    - ESP8266-12E compatible board
    - CCS811 TVOC and CO2 sensor
    - HDC1080 temperature and humidity sensor
    - BMP280 air pressure sensor
    - Shinyei Model PPD42NS Particle sensor
  
  Notes: 
  
    - A new CCS811 sensor requires at 48-burn in. 
    - Once burned in a CCS811 sensor requires 20 minutes of run 
      time before readings are considered good.
    - 

  Resources: 
  
    https://github.com/sparkfun/CCS811_Air_Quality_Breakout
    https://github.com/sparkfun/SparkFun_CCS811_Arduino_Library
    http://www.seeedstudio.com/depot/grove-dust-sensor-p-1050.html
    http://www.sca-shinyei.com/pdf/PPD42NS.pdf

  Special Thanks:

    Marshall Taylor @ SparkFun Electronics
    Nathan Seidle @ SparkFun Electronics
    Christopher Nafis

  I2C Connections:
  
    3.3V to 3.3V pin
    GND to GND pin
    SDA to A4
    SCL to A5

  PPD42 Connections:
  
    JST Pin 1 (Black Wire)  => Arduino GND
    JST Pin 3 (Red wire)    => Arduino 5VDC
    JST Pin 4 (Yellow wire) => Arduino Digital Pin 8

  Development environment specifics:
    - Arduino IDE 1.8.10
    - Generic ESP8266 board package 2.4.2

  This code is released under the [MIT License](http://opensource.org/licenses/MIT).

  Distributed as-is; no warranty is given.

******************************************************************************/

// Sensor Libraries
// ----------------------------------------------------------------- //
// |
#include <Wire.h>
#include <SparkFunCCS811.h>
#include "ClosedCube_HDC1080.h"
// |

// ESP8266 Libraries
// ----------------------------------------------------------------- //
// |
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
// |

// CCS811 Address Definition
// ----------------------------------------------------------------- //
// |
//#define CCS811_ADDR 0x5B //Default I2C Address
#define CCS811_ADDR 0x5A //Alternate I2C Address
// |

// User Defined Variables
// ----------------------------------------------------------------- //
// |
const String INFLUXDB_HOST = "192.0.2.12";  // InfluxDB server       //
const String INFLUXDB_PORT = "8086";        // InfluxDB port         //
const String INFLUXDB_DB   = "airquality";  // Database name         //
const char* WIFI_SSID      = "Linksys";     // Wireless network name //
const char* WIFI_PASS      = "admin";       // WPA2 PSK              //
int read_interval          = 30;            // Time in seconds between measurement readings
// |

// ----------------------------------------------------------------- //
const String VER = "0.1";

CCS811 mySensor(CCS811_ADDR);

ClosedCube_HDC1080 hdc1080;

ESP8266WiFiMulti WiFiMulti;
// |

/*
// PPD42
int pin = 8;
unsigned long duration;
unsigned long starttime;
unsigned long sampletime_ms = 30000;//sampe 30s ;
unsigned long lowpulseoccupancy = 0;
float ratio = 0;
float concentration = 0;
*/

// Setup 
// ----------------------------------------------------------------- //
// |
void setup()
{
  // Serial debug
  Serial.begin(9600);

  Serial.println();
  Serial.println("---------------------------------");
  Serial.println("| ESP AQ_Sensor");
  Serial.println("| Version " + VER + " ");
  Serial.println("------------- SETUP -------------");

  Serial.println("| - Wait for ESP Initialization");
  for (uint8_t t = 4; t > 0; t--)
  {
    Serial.printf("| -- [SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }

  Serial.println("| - Configuring Wi-Fi Settings");
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFiMulti.addAP(WIFI_SSID, WIFI_PASS);

  // Inialize I2C Harware
  Wire.begin();

  // Start CCS811 Initialization
  // ----------------------------------------------------------------- //
  // |
  Serial.println("| - CCS811 Initialization");
  // | It is recommended to check return status on .begin(), but it is not required.
  CCS811Core::status returnCode = mySensor.begin();
  if (returnCode != CCS811Core::SENSOR_SUCCESS)
  {
    Serial.println("! CCS811 Error !");
    Serial.println("! .begin() returned with an error !");
    Serial.println("! CCS811 Initialization FAILED !");
    Serial.println("!!!!-----------HANG----------!!!!");
  // | Hang if there was a problem.
    while (1); 
  }
  // |
  // ----------------------------------------------------------------- //
  // End CCS811 Initialization



  // Start HDC1080 Initialization
  // ----------------------------------------------------------------- //
  // |
  Serial.println("| - HDC1080 Initialization");
  // | Default settings:
  // |  - Heater off
  // |  - 14 bit Temperature and Humidity Measurement Resolutions
  hdc1080.begin(0x40);
  // | Print some info
  Serial.print("| -- Manufacturer ID=0x");
  Serial.println(hdc1080.readManufacturerId(), HEX); // 0x5449 ID of Texas Instruments
  Serial.print("| -- Device ID=0x");
  Serial.println(hdc1080.readDeviceId(), HEX); // 0x1050 ID of the device
  // |
  // printSerialNumber();
  // |
  // ----------------------------------------------------------------- //
  // End HDC1080 Initialization



  // Start BMP280 Initialization
  // ----------------------------------------------------------------- //
  // | 
  //if(!bmp280.init()){
  //  Serial.println("BMP280 initialization error!");
  //}
  // |
  // ----------------------------------------------------------------- //
  // End BMP280 Initialization

  /*
  // Start PPD42 Initialization
  // ----------------------------------------------------------------- //
  // |
  pinMode(8,INPUT);
  starttime = millis();//get the current time;
  // | 
  // ----------------------------------------------------------------- //
  // End PPD42 Initialization
  */
  
  Serial.println("| - Setup completed");
}

// Loop 
// ----------------------------------------------------------------- //
// |
void loop()
{
  Serial.println("--------------LOOP---------------");

  String INFLUX_POST = "";  // Initialize empty variable for HTTP post
  
  // W-Fi Initialization
  // ----------------------------------------------------------------- //
  // |
  // | If the Wi-Fi isnt connected, connect it
  // |
  if (WiFi.status() != WL_CONNECTED)
  {
    delay(1);
    Serial.println("| - Wi-Fi Initialization");
    startWIFI();
    return;
  }

  // Start CCS811 Readings
  // ----------------------------------------------------------------- //
  // |
  // | Check to see if data is ready with .dataAvailable()
  // | 
  if (mySensor.dataAvailable())
  {
  // | If so, have the sensor read and calculate the results.
    mySensor.readAlgorithmResults();

    int tempCO2 = mySensor.getCO2();
    int tempVOC = mySensor.getTVOC();

  // | Form output for InfluxDB
    INFLUX_POST += "tvoc value=" + String(tempVOC) + "\n";
    INFLUX_POST += "co2 value=" + String(tempCO2) + "\n";
  // | ----------- Start Serial Output ------------- //
  // | Returns calculated CO2 reading
    Serial.println("CO2       [" + String(tempCO2) + "]");
  // | Returns calculated TVOC reading
    Serial.println("tVOC      [" + String(tempVOC) + "]");
  // | ----------- End Serial Output --------------- //
  }
  else if (mySensor.checkForStatusError())
  {
    Serial.println();
    Serial.println("----------------");
    Serial.println("! CCS811 Error !");
    Serial.println("----------------");
    Serial.println();
    while (1);
  }

  // Start HDC1080 Readings
  // ----------------------------------------------------------------- //
  // |
  Serial.println("TempC     [" + String(hdc1080.readTemperature()) + "]");
  Serial.println("Humidity  [" + String(hdc1080.readHumidity()) + "]");

  // | Form output for InfluxDB
  INFLUX_POST += "tempc value=" + String(hdc1080.readTemperature()) + "\n";
  INFLUX_POST += "humidity value=" + String(hdc1080.readHumidity()) + "\n";

  // Start BMP280 Readings
  // ----------------------------------------------------------------- //
  // |
  // TODO

  /*
  // PPD42
  duration = pulseIn(pin, LOW);
  lowpulseoccupancy = lowpulseoccupancy+duration;
  
  if ((millis()-starttime) > sampletime_ms)//if the sampel time == 30s
  {
    ratio = lowpulseoccupancy/(sampletime_ms*10.0);  // Integer percentage 0=>100
    concentration = 1.1*pow(ratio,3)-3.8*pow(ratio,2)+520*ratio+0.62; // using spec sheet curve
    Serial.print("concentration = ");
    Serial.print(concentration);
    Serial.println(" pcs/0.01cf");
    Serial.println("\n");
    lowpulseoccupancy = 0;
    starttime = millis();
  }
  */

  // | Time since program start
    Serial.print("Uptime is ");
    Serial.print(millis()/1000);
    Serial.print(" Seconds");
    Serial.println();

  // Start data export to InfluxDB via HTTP POST
  // ----------------------------------------------------------------- //
  // |
  postToInflux(INFLUXDB_HOST, INFLUXDB_PORT, INFLUXDB_DB, INFLUX_POST);

  // Wait for next reading
  // ----------------------------------------------------------------- //
  // |
  Serial.println();
  Serial.println("---------Wait " + String(read_interval) + " Seconds-----------");
  Serial.println();
  delay(read_interval*1000);

}

// ----------------------------------------------------------------- //
// Function: printDriverError
// Purpose:  decodes the CCS811Core::status type and prints the type
//           of error to the serial terminal.
//
// Use:      Save the return value of any function of type
//           CCS811Core::status, then pass to this function to see
//           what the output was.
// ----------------------------------------------------------------- //
void printDriverError( CCS811Core::status errorCode )
{
  switch ( errorCode )
  {
    case CCS811Core::SENSOR_SUCCESS:
      Serial.print("SUCCESS");
      break;
    case CCS811Core::SENSOR_ID_ERROR:
      Serial.print("ID_ERROR");
      break;
    case CCS811Core::SENSOR_I2C_ERROR:
      Serial.print("I2C_ERROR");
      break;
    case CCS811Core::SENSOR_INTERNAL_ERROR:
      Serial.print("INTERNAL_ERROR");
      break;
    case CCS811Core::SENSOR_GENERIC_ERROR:
      Serial.print("GENERIC_ERROR");
      break;
    default:
      Serial.print("Unspecified error.");
  }
}

// ----------------------------------------------------------------- //
// Function: printSerialNumber
// Purpose:  prints the HDC1080 serial number
//           to the serial terminal.
//
// ----------------------------------------------------------------- //
void printSerialNumber()
{
  Serial.print("Device Serial Number=");
  HDC1080_SerialNumber sernum = hdc1080.readSerialNumber();
  char format[12];
  sprintf(format, "%02X-%04X-%04X", sernum.serialFirst, sernum.serialMid, sernum.serialLast);
  Serial.println(format);
}

// ----------------------------------------------------------------- //
// Function: startWIFI
// Purpose:  Starts Wi-Fi connection
//
// ----------------------------------------------------------------- //
void startWIFI(void)
{
  // Connect to WiFi network
  Serial.print("| -- Connecting to ");
  delay(30);
  Serial.println(WIFI_SSID);
  delay(10);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int count = 90;  // set a maximum amount of time to connect before timeout

  while (WiFi.status() != WL_CONNECTED)
  {
    count--;
    Serial.print(".");
    delay(1000);
    if (count == 0)
    {
      Serial.println();
      Serial.println("! -- Timeout Reached, Resetting -- !");
      // If it's taking too long to connect to Wi-Fi, perform a soft reset
      // 
      // Note: Two methods exist to reset the ESP via software
      //       These are restart and reset.  Restart is the suggested
      //       method.  This could also be done via hardware, but doing
      //       it via software requires fewer wires.
      //ESP.restart();  // call restart
      ESP.reset();      // call reset
    }
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Print the IP address
  Serial.print("ESP8266 IP: ");
  Serial.println(WiFi.localIP());
}

// ----------------------------------------------------------------- //
// Function: postToInflux
// Purpose:  Sends data to influxdb
//
// Input:
//        INFLUXDB_HOST
//        INFLUXDB_PORT
//        INFLUXDB_DB
//        INFLUX_POST
// ----------------------------------------------------------------- //
void postToInflux(String INFLUXDB_HOST, String INFLUXDB_PORT, String INFLUXDB_DB, String INFLUX_POST)
{
  HTTPClient http;
  http.begin("http://" + INFLUXDB_HOST + ":" + INFLUXDB_PORT + "/write?db=" + INFLUXDB_DB); //HTTP
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // start connection and send HTTP header
  int httpCode = http.POST(INFLUX_POST);

  // httpCode will be negative on error
  if (httpCode > 0)
  {
    if (httpCode == 204)
    {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] POST SUCCESS!");
    } else {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] POST... code: %d\n", httpCode);
    }

    // file found at server
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      Serial.println(payload);
    }
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();

  //return httpCode;
}
