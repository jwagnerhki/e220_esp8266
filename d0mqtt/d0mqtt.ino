//
// Power meter D0 Interface readout of SML messages into UDP & MQTT
//
// ESP8266 Wemos D1 Mini - Lolin(Wemos) D1 Mini (Clone)
//  + 880nm IR phototransistor like BPW96C
//  + wire GND to BPW96C emitter pin (round side of plastic)
//  + wire from BPW96 collector pin (flattened side of plastic) to ESP8266 board Pin D1
//  + 680 ohm pull-up from Pin D1 to 3.3V Vcc
//
//  BPW96C datasheet: Ic~4.5 mA, Vce_sat~0.3V
//   =>  R = (3.3-0.3)/4.5e-3 = ~670  => 680 ohm pull-up
//
// Landis Gyr E220 power meter
//      Optical *unidirectional* serial 9600,8,N,1
//      D0 interface DIN EN 62056-21 = translated copy of IEC 62056-21
//      https://www.landisgyr.de/webfoo/wp-content/uploads//2018/08/D000063497-E220-AMxD-Benutzerhandbuch-de-f.pdf
//
// References
//      https://github.com/volkszaehler/libsml
//      https://github.com/mwdmwd/iec62056-mqtt/tree/master/src
//      https://www.promotic.eu/en/pmdoc/Subsystems/Comm/PmDrivers/IEC62056_OBIS.htm
//      https://forum.arduino.cc/t/smart-meter-sml-hex-code-zerlegen/526925
//      https://www.msxfaq.de/sonst/bastelbude/smartmeter_d0_sml_protokoll.htm
//
// Details
//
//   1. Setup
//      The E220 meter must be switched once into Extended INFO mode. This has to
//      be done manually via the user menu on the E220. The step for enabling Extended info
//      mode requires entry of a 4-digit PIN via the user menu of the E220.
//
//      The E220 PIN is available only from the utility or metering service company.
//      Brute forcing is possible (not implemented here), ~10 sec/cycle * all 9999 = ~28 hours max
//
//      Once extended info is enabled, the E220 meter will periodically send out one SML message frame.
//
//  2. ESP8266 data capture
//
//      SoftwareSerial on Pin D1 captures the IR phototransistor -detected serial data.
//      Hardware serial is not used because ESP8266 has only one hardware serial port,
//      and it is occupied already with the Arduino firmware upload and serial monitor.
//
//      Arduino "Serial Monitor" can be opened for a debug dump of received optical data.
//      Compile with 'HAVE_SERIAL_TRACE' defined further below.
//
//  2. ESP8266 data output
//
//      Handling full capabilities of flexible SML is a pain on a small microcontroller,
//      hence the ESP8266 does not implement a full SML parser.
//
//      The ESP forwards SML frame payload as an a UDP packet to port 3220 of some host,
//      where it can be captured and flexibly decoded with 'libsml'.
//
//      The ESP also decodes two specific values from pre-determined byte offsets in SML
//      payload, namely
//          uint64_t in framebytes[168..175] = OBIS 1.8.0  = total energy reading in 100 Watt-hours
//          sint32_t in framebytes[196..199] = OBIS 16.7.0 = net power, A+ (drawn from grid) or A- (supplied to grid)
//
//      The decoded fields are sent to an MQTT broker on e.g. a Raspberry PI,
//      a test capture is possible with e.g.
//      pi@raspberrypi:~$ mosquitto_sub -h 192.168.0.185 -t "e220/#" -v
//          e220/P_W 272
//          e220/E_Wh 71611.5
//          e220/P_W 231
//          e220/E_Wh 71611.5
//          ...
//
//      The ESP also serves over HTTP a single web page that shows the decoded fields,
//      i.e. live power draw [W] and total energy consumed [kWh].
//      Configure PRICE_CENT_PER_KWH to show the accumulated cost [€] of energy so far under
//      a tariff that started after a meter reading of TARIFF_START_SINCE_KWH.
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// #define HAVE_SERIAL_TRACE // define to enable serial console dump of received optical SoftwareSerial data
#define HAVE_WEB             // define to enable Wifi, UDP, MQTT and HTTP

// #define DEBUG_SML_RX      // for debugging the SML de-framing function

//#include "secret.h"
#ifndef SECRET

    const char ssid[] = "<your wlan ssid>";
    const char pass[] = "<your wlan passwd>";
    const char MQTT_HOST[] = "<ip of mqtt host like 192.168.0.100>";
    const int  MQTT_PORT = 1883;
    const char MQTT_USER[] = ""; // blank = no credentials used
    const char MQTT_PASS[] = ""; // blank = no credentials used

#endif

#define MY_HOSTNAME            "e220"
#define MY_HTTP_PORT           80           // serve power readings on a web page on this port
#define SML_UDP_DEST_PORT      3220         // send undecoded SML payload to this port of MQTT Host IP
#define MQTT_TOPIC_POWER_W     "e220/P_W"
#define MQTT_TOPIC_ENERGY_WH   "e220/E_Wh"

#define PRICE_CENT_PER_KWH     37           // Main tariff e.g. €0,31/kWh + 19% tax
#define TARIFF_START_SINCE_KWH  0           // Starting from which kWh reading the above price tariff applies

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <ctype.h>
#include <stdio.h>

#include <SoftwareSerial.h>
#ifdef HAVE_WEB
  #if defined(ESP32)
    #include <WiFi.h>
  #elif defined(ESP8266)
    #include <ESP8266WiFi.h>
  #endif
  #include <PubSubClient.h>
  #include <WiFiUdp.h>
  #include <ESP8266WebServer.h>

  void http_handleRoot();
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

SoftwareSerial d0;
#ifdef HAVE_WEB
  WiFiUDP udp;
  WiFiClient espClient;
  PubSubClient mqttClient(espClient);
  ESP8266WebServer httpServer(MY_HTTP_PORT);
#endif

static signed int current_Power_W = 0;
static float current_Energy_Wh = 0;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

int readSMLFrame(SoftwareSerial* ss, byte* frame, unsigned int bufsize, unsigned int* noverflow = NULL)
{
  // FPGA/VHDL-Style implementation of a SML de-framer
  // SML ref https://de.wikipedia.org/wiki/Smart_Message_Language

  enum {
    STATE_LOCK_BEGIN_SEQ = 0,  // scan for [1B][1B][1B][1B]
    STATE_LOCK_VER1_SEQ,       // scan for [1][1][1][1] right after [1B][1B][1B][1B]
    STATE_READ_FRAME,          // read everything until (and including - but truncated!) reoccurrence of [1B][1B][1B][1B]
    STATE_LOCK_END_SEQ,        // read 4 bytes 1A <xx> <yy> <zz> ; xx = nr fill bytes, yy zz = crc16/x25 checksum
    STATE_QUIT
  };

  unsigned char state = STATE_LOCK_BEGIN_SEQ;
  unsigned char prev_state = state;

  unsigned int runlen_of_1B = 0;
  unsigned int runlen_of_01 = 0;
  unsigned int nrx_frame = 0;
  unsigned int nrx_after_state_change = 0;
  unsigned int noverflow_frame = 0;

  byte smlEnd[4];

  while(state != STATE_QUIT) {

    ss->perform_work();
    if (!ss->available()) {
#ifdef HAVE_WEB
      mqttClient.loop();
      httpServer.handleClient();
#endif
      continue;
    }

    byte ch = (byte)ss->read();

    #ifdef DEBUG_SML_RX
    Serial.print("state ");
    Serial.print(state);
    Serial.print(" rx ");
    Serial.print(ch, HEX);
    Serial.println();
    #endif

    // Helper counts for escape sequence detection
    if (ch == 0x1B) {
      ++runlen_of_1B;
    } else {
      runlen_of_1B = 0;
    }
    if (ch == 0x01) {
      ++runlen_of_01;
    } else {
      runlen_of_01 = 0;
    }

    // Helper intra-state byte rx counter
    if (state == prev_state) {
      ++nrx_after_state_change;
    } else {
      nrx_after_state_change = 1; // the above read() is counted as 1
    }
    prev_state = state;

    // State Machine
    switch(state) {

      case STATE_LOCK_BEGIN_SEQ:

        // scan for [1B][1B][1B][1B]
        if (runlen_of_1B >= 4) {
          state = STATE_LOCK_VER1_SEQ;
        }
        break;

      case STATE_LOCK_VER1_SEQ:

        // scan for [1][1][1][1] right after [1B][1B][1B][1B]
        if (runlen_of_01 != nrx_after_state_change) {
          state = STATE_LOCK_BEGIN_SEQ;
        } else if (runlen_of_01 >= 4) {
          state = STATE_READ_FRAME;
        }
        break;

      case STATE_READ_FRAME:
        // keep appending rx'ed data until [1B][1B][1B][1B]
        if (runlen_of_1B == 4) {
          nrx_frame -= 4;
          state = STATE_LOCK_END_SEQ;
        } else {
          if (nrx_frame < bufsize) {
            frame[nrx_frame] = ch;
            ++nrx_frame;
          } else {
            ++noverflow_frame;
          }
        }
        break;

      case STATE_LOCK_END_SEQ:
        // Get the last 4 bytes
        smlEnd[nrx_after_state_change-1] = ch;
        if (nrx_after_state_change >= 4) {
          // TODO
          //  check that     smlEnd[0] == 0x1A
          //  padding count  smlEnd[1] <= 3
          //  crc            smlEnd[2] smlEnd[3] should be inspected...
          if (smlEnd[1] <= 3) {
            nrx_frame -= smlEnd[1];
          }
          state = STATE_QUIT;
        }
        break;

      default:
        state = STATE_LOCK_BEGIN_SEQ;
    }
  }

  if (noverflow != NULL) {
    *noverflow = noverflow_frame;
  }

  return nrx_frame;
}


signed int parseSMLFrame_E220_Power(byte *smlframe, unsigned int nrx)
{
  const int offset = 12*16 + 4;
  signed int power_W = 0;

  if (nrx >= (offset + 4)) {
     unsigned int tmp;
     tmp = ((unsigned int)smlframe[offset]) << 24;
     tmp |= ((unsigned int)smlframe[offset+1]) << 16;
     tmp |= ((unsigned int)smlframe[offset+2]) << 8;
     tmp |= ((unsigned int)smlframe[offset+3]);
     power_W = (signed int)tmp;
  }
  return power_W;
}


float parseSMLFrame_E220_TotalEnergy(byte *smlframe, unsigned int nrx)
{
  const int offset = 10*16 + 8;
  float energy_kWh = 0;
  if (nrx >= (offset + 8)) {
    uint64_t tmp = ((uint64_t)smlframe[offset]) << 56 |
      ((uint64_t)smlframe[offset+1]) << 48 |
      ((uint64_t)smlframe[offset+2]) << 40 |
      ((uint64_t)smlframe[offset+3]) << 32 |
      ((uint64_t)smlframe[offset+4]) << 24 |
      ((uint64_t)smlframe[offset+5]) << 16 |
      ((uint64_t)smlframe[offset+6]) << 8 |
      ((uint64_t)smlframe[offset+7]);
    energy_kWh = ((float)tmp) / 10;
  }

  return energy_kWh;
}


void echoSwSerial(SoftwareSerial* ss)
{
  while (ss->available()) {

    byte ch = (byte)ss->read();

    if(isprint(ch)) {
      Serial.print((char)ch);
    } else {
      Serial.print('[');
      Serial.print(ch, HEX);
      Serial.print(']');
    }

    if(ch=='\n') {
      Serial.println("");
    }
  }
}


void prettyPrintBIN(byte* buf, int buflen)
{
  while(buflen > 1) {
    if(isprint(*buf)) {
      Serial.print((char)*buf);
    } else {
      Serial.print('[');
      Serial.print(*buf, HEX);
      Serial.print(']');
    }
    --buflen;
    ++buf;
  }
  Serial.println();
}

#ifdef HAVE_WEB
void http_handleRoot()
{
  // Ver 1
  #if 0
  static char msg[160];
  snprintf(msg, sizeof(msg), "<html><title>E220 Power Meter</title><meta http-equiv='refresh' content='5'><body>Power: %d W<br>Energy: %.3f kWh</body></html>", current_Power_W, current_Energy_Wh/1000);
  msg[sizeof(msg)-1] = '\0';
  httpServer.send(200, "text/html", msg);
  #endif

  // Ver 2
  String msg; // or msg[350], <html><head><meta http-equiv='refresh' content='5' /><style>div.mid { font-size: 10vw; height: 80vh; display: flex; align-items: center; justify-content: center }</style></head><title>E220 Power Meter</title><body><div class=mid><center>316 W&#x223f;<br><small>229.174 kWh</small></center></div></body></html>
  static char tmp[15];
  msg += F("<html><head><meta http-equiv='refresh' content='5' />\n");
  msg += F("<style>div.mid { font-size: 10vw; height: 80vh; display: flex; align-items: center; justify-content: center }</style>");
  msg += F("</head><title>E220 Power Meter</title>\n");
  msg += F("<body><div class=mid><center>");
  msg += current_Power_W;
  msg += F(" W&#x223f;<br>\n");
  msg += F("<small><small>");
  //msg += current_Energy_Wh/1000;
  snprintf(tmp, sizeof(tmp), "%.2f", current_Energy_Wh/1000);
  msg += tmp;
  msg += F(" kWh<br>&#x20ac;");
  snprintf(tmp, sizeof(tmp), "%.2f", (PRICE_CENT_PER_KWH*(current_Energy_Wh-TARIFF_START_SINCE_KWH))/100000);
  msg += tmp;
  msg += F("</small></small></center></div></body></html>\n");
  httpServer.send(200, "text/html", msg);
}
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {

  Serial.begin(115200); // USB debug comms for now

  d0.begin(9600, SWSERIAL_8N1, D1, -1, false, 512);
  d0.enableTx(false);
  d0.enableIntTx(false);

#ifdef HAVE_WEB
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  httpServer.on("/", http_handleRoot);
  httpServer.begin();
#endif

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.print("Configured for MQTT at ");
  Serial.print(MQTT_HOST);
  Serial.print(", and forwarding SML forward to MQTT host UDP port ");
  Serial.println(SML_UDP_DEST_PORT);
}

void loop()
{
  static byte smlframe[255];
  static char mqttStr[20];
  unsigned int noverflow = 0;

  while(Serial.available()) {
    Serial.read();
  }

  mqttClient.loop();
  httpServer.handleClient();

  // if (echoOnly) {
  //  echoSwSerial(&d0);
  //  return;
  //}

  int nrx = readSMLFrame(&d0, smlframe, sizeof(smlframe), &noverflow);
  if (nrx <=0) {
    return;
  }

  if (noverflow) {
    Serial.print("Warning: data truncated, SML frame size ");
    Serial.print(noverflow);
    Serial.print(" bytes larger than hardcoded buffer size of ");
    Serial.println(sizeof(smlframe));
  }

  digitalWrite(LED_BUILTIN, LOW);

  current_Power_W = parseSMLFrame_E220_Power(smlframe, nrx);
  current_Energy_Wh = parseSMLFrame_E220_TotalEnergy(smlframe, nrx);

#if HAVE_SERIAL_TRACE
  prettyPrintBIN(smlframe, nrx);
#endif

#ifdef HAVE_WEB
  if (WiFi.status()) {
    udp.beginPacket(MQTT_HOST, SML_UDP_DEST_PORT);
    udp.write(smlframe, nrx);
    udp.endPacket();
  }

  if (WiFi.status()) {
    if (mqttClient.connected()) {
      sprintf(mqttStr, "%d", current_Power_W);
      mqttClient.publish(MQTT_TOPIC_POWER_W, mqttStr);
      sprintf(mqttStr, "%.1f", current_Energy_Wh);
      mqttClient.publish(MQTT_TOPIC_ENERGY_WH, mqttStr);
    } else {
      mqttClient.connect(MY_HOSTNAME);
    }
  }
#endif

  digitalWrite(LED_BUILTIN, HIGH);

  Serial.print(current_Power_W);
  Serial.print(" W, ");
  Serial.print(current_Energy_Wh/1000);
  Serial.print(" kWh, ");
  Serial.print((PRICE_CENT_PER_KWH*(current_Energy_Wh-TARIFF_START_SINCE_KWH))/100000);
  Serial.println(" EUR");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
