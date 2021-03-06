#include <thingsml_http.h>
#include <Sodaq_R4X.h> 
#include <Sodaq_wdt.h> 
#include <Sodaq_LSM303AGR.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Wire.h>
/*
 * *****************
 * NOTICE
 * *****************
 * 
 * This board requires additional libraries!
 * 
 * 1. Under File->Preferences->Additional Boards Manager URLs input:
 * http://downloads.sodaq.net/package_sodaq_samd_index.json
 * 
 * 2. Then under Tools->Board->Boards manager download:
 * SODAQ SAMD Boards
 * 
 * 2.1 Don't forget to select the board Sodaq Sara under Tools->Board :')
 * 
 * 3. Then under Tools->Manage libraries install the following 3 libraries:
 * Sodaq_LSM303AGR
 * Sodaq_R4X
 * Sodaq_wdt
 * 
 * 4. Configure details below.
 */

/*
 * Begin Configuration
 */
#define DEVICE_URN      "urn:dev:IMEI:354679093729344:"
#define DEVICE_KEY      "z2%#Xfuc!EVG8C7y$GyUn9vxU!FLmsik"

/*
 * APN information is provided with the sim card
 * If you lost the information the following options can be tried:
 *  - leave empty
 *  - "kpnthings.iot" (for KPN Things M2M)
 *  - "kpnthings2.m2m" (for KPN Things M2M+)
*/
#define APN             "kpnthings.iot"
/*
 * End Configuration
 */


#define HTTP_HOST       "m.m"
#define HTTP_IP         "10.151.236.157"
#define HTTP_PATH       "/ingestion/m2m/senml/v1"
#define HTTP_PORT       80

#define CONSOLE_STREAM   SerialUSB
#define MODEM_STREAM     Serial1

#define GPS_ADR 0x42
  
#define CURRENT_OPERATOR AUTOMATIC_OPERATOR
#define CURRENT_URAT     SODAQ_R4X_LTEM_URAT
#define CURRENT_MNO_PROFILE MNOProfiles::STANDARD_EUROPE

static Sodaq_R4X r4x;
static Sodaq_LSM303AGR AccMeter;
static Sodaq_SARA_R4XX_OnOff saraR4xxOnOff;
static bool isReady;
static bool isOff;

SenMLPack device(DEVICE_URN);
SenMLIntRecord temperature(SENML_NAME_TEMPERATURE, SENML_UNIT_DEGREES_CELSIUS);
SenMLDoubleRecord latitude(SENML_NAME_LATITUDE, SENML_UNIT_DEGREES_LATITUDE);
SenMLDoubleRecord longitude(SENML_NAME_LONGITUDE, SENML_UNIT_DEGREES_LONGITUDE);
SenMLDoubleRecord sound(SENML_NAME_SOUND, SENML_UNIT_DECIBEL);

#define STARTUP_DELAY 10000

#ifndef NBIOT_BANDMASK
#define NBIOT_BANDMASK BAND_MASK_UNCHANGED
#endif
unsigned long previousMillis = 0;
const long interval = 30000;

const int pinAdc = 7;

#define BUFFER_SIZE 1024
char buff[BUFFER_SIZE] = {0};

struct GPSInfo {
  bool hasFix = false;
  double latitude = 0;
  double longitude = 0;
};


int toInt(char * start, int len) {
  int result = 0;
  for(int i = 0; i < len; i += 1) {
    result = result * 10 + (start[i] - '0');
  }
  return result;
}

int readSound() {
  long soundValue = 0;
  for(int i=0; i<32; i++)
  {
      soundValue += analogRead(pinAdc);
  }
 
  soundValue >>= 5;
    
  CONSOLE_STREAM.println("Sound:");
  CONSOLE_STREAM.println(soundValue / 10);
  return soundValue / 10;
}

struct GPSInfo readGPS() {
  memset(buff, 0, BUFFER_SIZE);
  uint16_t count = 0;

  for(int i = 0; i < 20; i += 1) {
    Wire.beginTransmission(GPS_ADR);
    Wire.write((uint8_t)0xFD);
    Wire.endTransmission(false);
    Wire.requestFrom(GPS_ADR, 2);
    count = (uint16_t)(Wire.read() << 8) | Wire.read();
    count = (count > BUFFER_SIZE) ? BUFFER_SIZE : count;
    if (count > 0) {
      break;
    }
    Wire.endTransmission();
    delay(50);
  }
  GPSInfo info;
  if (count == 0) {
    CONSOLE_STREAM.println("No gps module response");
    return info;
  }
  for (size_t i = 0; i < (count-1); i++) {
    Wire.requestFrom(GPS_ADR, 1, false);
    buff[i] = Wire.read();
  }
  Wire.requestFrom(GPS_ADR, 1);
  buff[count-1] = Wire.read();
  // http://navspark.mybigcommerce.com/content/NMEA_Format_v0.1.pdf
  char * line = strstr(buff, "$GNRMC");

  if (line != NULL) {
    char * next = strchr(line, ',');
    int i = 0;
    int degrees, minutes, seconds;
    while(next != NULL) {
       next = strchr(next + 1, ',');
       if (i == 0) {
          info.hasFix = next[1] == 'A';
       } else if (i == 1 && info.hasFix) {
          degrees = toInt(&next[1], 2);
          minutes = toInt(&next[3], 2);
          seconds = toInt(&next[6], 5);
       } else if (i == 2 && info.hasFix) {
          info.latitude = (next[1] == 'N' ? 1 : -1) * ((double)degrees) + ((double) minutes) / 60 + ((double) seconds) / 6000000;
       } else if (i == 3 && info.hasFix) {
          degrees = toInt(&next[1], 3);
          minutes = toInt(&next[4], 2);
          seconds = toInt(&next[7], 5);
       } else if (i == 4 && info.hasFix) {
          info.longitude = (next[1] == 'E' ? 1 : -1) * ((double)degrees) + ((double) minutes) / 60 + ((double) seconds) / 6000000;
       } else if (i > 4) {
          break;
       }
       i++;
    }
  }
  CONSOLE_STREAM.println(buff);
  return info;
}

struct GPSInfo getGPS() {
  for(int i = 0; i < 10; i += 1) {
    GPSInfo info = readGPS();
    if (info.hasFix || i == 9) {
      return info;
    }
    delay(50);
  }
  
}


void setup() {
  device.add(temperature);
  device.add(latitude);
  device.add(longitude);

  device.add(sound);
  
  sodaq_wdt_safe_delay(STARTUP_DELAY);
  
  pinMode(GPS_ENABLE, OUTPUT);
  digitalWrite(GPS_ENABLE, 1);

  pinMode(pinAdc, INPUT);
  
  Wire.begin();

  AccMeter.rebootAccelerometer();
  delay(1000);

  // Enable the Accelerometer
  AccMeter.enableAccelerometer();

  while ((!CONSOLE_STREAM) && (millis() < 10000)) {
    // Wait max 10 sec for the CONSOLE_STREAM to open
  }
  CONSOLE_STREAM.begin(115200);
  CONSOLE_STREAM.println("Console open");
  MODEM_STREAM.begin(r4x.getDefaultBaudrate());

  r4x.setDiag(CONSOLE_STREAM);
  r4x.init(&saraR4xxOnOff, MODEM_STREAM);

  CONSOLE_STREAM.println("Attempting initial gps fix");
  
  GPSInfo info = getGPS(); 
  if (info.hasFix) {
    CONSOLE_STREAM.println("Initial gps fix succesfull");
  } else {
    CONSOLE_STREAM.println("Initial gps fix failed");
  }
}

void loop() {
  CONSOLE_STREAM.println("Turning on modem...");
  r4x.on();
  CONSOLE_STREAM.println("Connecting to network...");
  isReady = r4x.connect(APN, CURRENT_URAT, CURRENT_MNO_PROFILE, CURRENT_OPERATOR, BAND_MASK_UNCHANGED, NBIOT_BANDMASK);
  CONSOLE_STREAM.println(isReady ? "Network connected" : "Network connection failed");
  
  if (isReady) {
    postHTTP();
  }
  isOff = r4x.off();
  CONSOLE_STREAM.println(isOff ? "Modem off" : "Power off failed");  
  CONSOLE_STREAM.println("------------");
  sodaq_wdt_safe_delay(interval);
}

int8_t getBoardTemperature() {
  int8_t temp = AccMeter.getTemperature();
  return temp;
}

void postHTTP() {
  temperature.set(AccMeter.getTemperature());
  
  sound.set(readSound());
  
  GPSInfo info = getGPS();
  if (info.hasFix) {
    latitude.set(info.latitude);
    longitude.set(info.longitude);
  } else {
   CONSOLE_STREAM.println("Cannot get gps fix");
  }
  int len = ThingsML::httpPost(buff, BUFFER_SIZE, DEVICE_KEY, HTTP_HOST, HTTP_PATH, device);

  CONSOLE_STREAM.println("Sending message...");
  uint8_t socketId = r4x.socketCreate(0, TCP);
  r4x.socketConnect(socketId, HTTP_IP, HTTP_PORT);

  r4x.socketWrite(socketId, (uint8_t *) buff, len);
  r4x.socketWaitForRead(socketId);
  CONSOLE_STREAM.println("Receiving message...");
  int receiveLength = r4x.socketRead(socketId, (uint8_t *) buff, BUFFER_SIZE);

  r4x.socketClose(socketId);

  CONSOLE_STREAM.print("Message response length: ");
  CONSOLE_STREAM.println(receiveLength);

  if (receiveLength > 0) {
    buff[receiveLength] = 0; // Null terminate the string
    CONSOLE_STREAM.println("Message response:");
    CONSOLE_STREAM.println(buff);
  }
  CONSOLE_STREAM.println("Message sending finished.");
}
