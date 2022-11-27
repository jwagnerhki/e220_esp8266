
# Infrared readout of Landis+Gyr E220 with ESP8266 Webpage and MQTT

A simple Arduino project for optical read out of Landis+Gyr E220 power meters. Requires an **ESP8266 board** such as the Wemos D1 Mini, an 880 nm infrared phototransistor like **BPW96C**, some wiring and a **680 ohm** resistor, and some **door ring magnet** ca. 33mm outer diameter with an 5mm (M5) inner hole through with to glue/epoxy the 5mm diameter phototransistor. 

## E220 Power Meter Configuration
The E220 meter must be switched once into Extended INFO mode via its user menu ([E220 user guide](https://www.landisgyr.de/webfoo/wp-content/uploads//2018/08/D000063497-E220-AMxD-Benutzerhandbuch-de-f.pdf]) section 5.4.1). This requires entry of a 4-digit PIN that has to be requested from the utility or metering service company.

Brute forcing the PIN is doable (not implemented here). The meter does not lock on incorrect attempts. Time perhaps ~20 sec/cycle * all 10000 combinations = ~56 hours max.

Once extended info is enabled, the E220 screen continually displays the live power draw from / feed into the grid, and the infrared interface sends the same info as an SML formatted data frame. One frame per second.
