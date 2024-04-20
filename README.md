# energy-monitor
Wifi-capable energy monitor based on Arduino

The program obtains network data from the ADC attached to the Arduino such as voltage, current, active and reactive power and sends it through wifi using ESP8266 to a web server (code not included), in order to check a real-time graph. Then if a voltage fall happens, the system would send a SMS to the specified phone number as an alert. The device would survive making use of a short battery.

The first thing the system does is to set up a Wi-Fi access point so the user could configure the SSID and password of a near Wi-Fi and then the phone number to send alerts. The communication with the ESP8266 uses AT commands explained in its datasheet. In order to timestamp the data sent to the web server, the time is set on the RTC.