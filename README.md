
# Readout of Landis Gyr E220 using ESP8266 - Webpage and MQTT

A simple Arduino project for optical readout of Landis+Gyr E220 power meters. Requires an **ESP8266 board** such as the Wemos D1 Mini, an ~880nm infrared phototransistor like [**BPW96C**](https://www.vishay.com/docs/81532/bpw96.pdf), some wiring and some **20 to 680 ohm** resistor, and for example a **door ring magnet** ca. 33mm outer diameter with an 5mm (M5) inner hole through with to glue/epoxy the 5mm diameter phototransistor. 

## E220 Power Meter Configuration
The E220 meter must be switched once into Extended INFO mode via its user menu ([E220 user guide](https://www.landisgyr.de/webfoo/wp-content/uploads//2018/08/D000063497-E220-AMxD-Benutzerhandbuch-de-f.pdf]) section 5.4.1). This requires entry of a 4-digit PIN that has to be requested from the utility or metering service company.

Brute forcing the PIN is doable (not implemented here). The meter does not lock on incorrect attempts. Time perhaps ~20 sec/cycle * all 10000 combinations = ~56 hours max.

Once extended info is enabled, the E220 screen continually displays the live power draw from / feed into the grid, and the infrared interface sends the same info as an SML formatted data frame. One frame per second.

## Board
The IR phototransistor BPW96C needs its emitter pin (round side) tied to GND. The collector pin (flattened side) to ESP8266 board pin D1. The same D1 pin needs a 20..680 ohm resistor for pull-up to Vcc/3.3V. 

![Wemos D1 board pin D1, GND, Vcc]([https://raw.githubusercontent.com/jwagnerhki/e220_esp8266/main/images/sample_page.png](https://raw.githubusercontent.com/jwagnerhki/e220_esp8266/main/images/wemos-D1.jpg))

## Arduino config
Need the Arduino IDE, adding ESP8266 into the board manager (c.f. [instructions](https://arduino-esp8266.readthedocs.io/en/latest/installing.html)). The board used here is the Wemos D1 Mini. Under the Arduino menu Tools, Board, it is called "Lolin(Wemos) D1 Mini (Clone)". Config is CPU 80 MHz and 4MB.

The Arduino sketch is under ./d0mqtt/

From the code Library need: ESP8266WiFi, SoftwareSerial, PubSubClient, WiFiUdp, ESP8266WebServer.

Before uploading the sketch, adjust the hard coded entries:
```
const char ssid[] = "<your wlan ssid>";
const char pass[] = "<your wlan passwd>";
const char MQTT_HOST[] = "<ip of mqtt host like 192.168.0.100>";
const int  MQTT_PORT = 1883;
#define SML_UDP_DEST_PORT      3220    // send undecoded SML payload to this port of MQTT Host
#define PRICE_CENT_PER_KWH     37      // Main tariff e.g. €0,31/kWh + 19% tax
#define TARIFF_START_SINCE_KWH  0      // Starting from which kWh reading the above price tariff applies
```


## MQTT
The ESP sends topics 'e220/P_W' and 'e220/E_Wh'. Example trace:
```
pi@raspberrypi:~$ mosquitto_sub -t "e220/#" -v
e220/P_W 272 
e220/E_Wh 71611.5 
e220/P_W 231 
e220/E_Wh 71611.5 
... 
```
To get MQTT data via Telegraf into InfluxDB (and then e.g. Grafana), there is an  example config under *./telegraf-conf/*

## UDP
A copy of SML frame payload is sent as an UDP packet to the MQTT host but a different port than the MQTT port. The UDP can be captured (e.g., ./debugCapture/pyUDPrx.py) and decoded through *libsml* (e.g., ./debugCapture/decodeE220sml.c).

## Web page
The ESP also serves a very simple status page. It refreshes every 5 seconds. Example page: ![example of status page](https://raw.githubusercontent.com/jwagnerhki/e220_esp8266/main/images/sample_page.png)

## Serial
SoftwareSerial is used for optical serial reception (pin D1) since ESP8266 has only one hardware serial port and that is already occupied by the Arduino USB-Serial program upload and serial monitor interface.

The serial monitor interface outputs a copy of live meter readings, and raw SML frame debug data if enabled.
