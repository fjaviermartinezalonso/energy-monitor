#include <SoftwareSerial.h>
#include <TimerOne.h>
#include "Wire.h"
#include "EmonLib.h"

// STATE definition
#define ST_MEAS     1
#define ST_SEND     2
#define ST_FALL     3
#define ST_SLEEP    4
#define ST_HELLO    0
#define ST_SSID     1
#define ST_PASS     2
#define ST_IP       3
#define ST_PORT     4
#define ST_PHONE    5
#define ST_END      6

#define RECONNECT   3                                 // sets reconnection try number
#define COMPARATOR  150                               // voltage comparation, minimum value
#define COMPARATOR2 270                               // voltage comparation, maximum value

// Global variables
String  ssid, pass, dst_ip, port, mac, tlf;
byte    state = ST_MEAS;

// Objects
EnergyMonitor emon;                                   // measuring
SoftwareSerial ESPserial(5, 4);                       // ESP communication ports. RX 5, TX 4 (brown - white)
SoftwareSerial M590serial(13, 12);                    // GPRS communication ports. RX 13, TX 12





void setup() {
  // Setup serials
  Wire.begin();
  Serial.begin(9600);                                 // serial comm initialization (9600 baud rate as maximum for softserials) "Serial" used as monitor for debugging
  ESPserial.begin(9600);                              // ESP serial for WiFi communications
  
  // Setup ESP8266
  while (!ESPconnect())  delay(20);                   // don't continue until ESP's OK
  delay(20);
  APset();
  APwaitComm();
  //setRouter(F("AndroidAP"), F("pirulo112"), F("192.168.0.15"), F("3000"), F("684308616"));
  //setRouter(F("dd-wrt"), F("beaglebone"), F("192.168.2.102"), F("8080"), F("684308616"));
  Serial.println(F("Connecting..."));
  while (!connectWiFi()) delay(1000);                 // don't continue until WiFi communication
  ESPserial.println(F("AT+CIPMUX=0"));                // single TCP/UDP connection
  while (!ESPserial.find("OK")) delay(10);

  // Setup DS3231
  //setDS3231time(00,45,8,3,29,04,16);               // El RTC recordará la fecha que se le ha asignado. DS3231 seconds, minutes, hours, day, date, month, year
  Serial.println(getTime());

  // Setup measurements
  emon.voltage(1, 490, 1.7);                          // pin, calibracion, fase (cambiar valores)
  emon.current(2, 5.6);                               // pin, calibracion //4.6

  // Setup interrupts
  Timer1.initialize(3000000);                         // interrupt each 3000 ms
  Timer1.attachInterrupt(ISR_1SEC);                   // interrupt initialization and asociation with ISR_Blink

  state = ST_MEAS;
  Serial.println(F("END SETUP\n"));
}



void loop() {
  switch (state) {
    case ST_MEAS:
      emon.calcVI(20, 2000);                          // numero de ondas y tiempo
      //emon.serialprint();                           // prints measured data
      if (emon.getVrms() < double(COMPARATOR) || emon.getVrms() > double(COMPARATOR2)) state = ST_FALL;
      break;

    case ST_SEND:
      if (!sendWiFi(emon, getTime()))  Serial.println(F("SEND ERROR"));       // send data
      state = ST_MEAS;
      break;

    case ST_FALL:
      detachInterrupt(0);                             // stops SEND interruption
      Serial.println("\nFALL! " + getTime());
      /*ESPserial.end();                                // stops ESP serial until network recovery
      M590serial.begin(9600);
      delay(3000);                                    // wait until GPRS initialization
      M590serial.flush();
      while (!M590connect())  delay(20);              // don't continue until GPRS OK
      enviarSMS(getTime());
      Serial.println(F("Message SENT\n"));*/
      state = ST_SLEEP;
      break;

    case ST_SLEEP:                                    // low-energy MODE
      emon.calcVI(20, 2000);
      if (emon.getVrms() > double(COMPARATOR) && emon.getVrms() < double(COMPARATOR2)) {
        Timer1.attachInterrupt(ISR_1SEC);             // SEND interruption ON
        //M590serial.end();                             // stops M590 serial until network fall
        //ESPserial.begin(9600);                        // ESP serial ON
        state = ST_SEND;
      }
      break;
  }
}



/******************************/
/* ESP8266 functions      */
/******************************/
void setRouter(String id, String pwd, String ip, String pt, String phone) {
  ssid = id;
  pass = pwd;
  dst_ip = ip;
  port = pt;
  tlf = phone;
}



boolean ESPconnect() {
  ESPserial.println(F("AT"));
  delay(500);
  if (ESPserial.find("OK")) {
    ESPserial.flush();
    Serial.println(F("ESP8266 READY"));
    return true;
  }
  else return false;
}



boolean connectWiFi() {
  ESPserial.println(F("AT+CWQAP"));
  while (!ESPserial.find("OK")) delay(10);

  ESPserial.println(F("AT+CWMODE=1"));                 // wifimode: STA
  while (!ESPserial.find("OK")) delay(10);

  ESPserial.flush();
  String ESPcmd = F("AT+CWJAP=\"");                    // join access point with SSID and PASS
  ESPcmd.concat(ssid);
  ESPcmd.concat(F("\",\""));
  ESPcmd.concat(pass);
  ESPcmd.concat(F("\""));
  ESPserial.println(ESPcmd);
  delay(3000);

  if (ESPserial.find("OK")) {
    mac = getMac();
    if (getIP() != "0.0.0.0") {                        // if IP is 0.0.0.0, it's not properly connected
      Serial.println(F("Connected"));
      Serial.println("IP: " + getIP());
      Serial.println("MAC: " + mac);
      return true;
    }
    else Serial.println(F("IP is 0.0.0.0"));
  }
  return false;
}



boolean isConnected() {
  if (getIP() != "0.0.0.0")  return true;

  Serial.println(F("Reconnecting..."));
  for (int i = 0; i < RECONNECT; i++) {               // try "RECONNECT" times to connect to WiFi
    if (connectWiFi()) return true;
  }
  Serial.println(F("Can't reconnect"));
  return false;
}



String getMac() {
  String macAddr = "";
  int a, b, c, d;

  ESPserial.flush();
  ESPserial.println(F("AT+CIFSR"));
  while (ESPserial.available()) {
    macAddr.concat((char)ESPserial.read());
    delay(3);
  }
  ESPserial.flush();

  a = macAddr.indexOf('"');                         // primeras comillas
  b = macAddr.indexOf('"', a + 1);                  // segundas comillas
  c = macAddr.indexOf('"', b + 1);                  // terceras comillas
  d = macAddr.indexOf('"', c + 1);                  // ultimas comillas
  macAddr = macAddr.substring(c + 1, d);            // recortar el String con la mac que buscamos

  return macAddr;
}



String getIP() {
  // on APmode is always 192.168.4.1
  // on AndroidAP is 192.168.43.243

  String ip = "";
  int a, b;

  ESPserial.flush();
  ESPserial.println(F("AT+CIFSR"));
  delay(1000);  //delay(5);
  while (ESPserial.available()) {
    ip.concat((char)ESPserial.read());
    delay(3);
  }
  ESPserial.flush();

  a = ip.indexOf('"');                              // primeras comillas
  b = ip.indexOf('"', a + 1);                       // segundas comillas
  ip = ip.substring(a + 1, b);                      // recortar el String con la mac que buscamos

  return ip;
}



boolean sendWiFi(EnergyMonitor emon, String RTC_time) {
  if (!isConnected())  return false;                        // if no WiFi connection available, it continues measuring

  String ESPcmd = F("AT+CIPSTART=\"TCP\",\"");              // start communication (TCP, DST_IP, 80)
  ESPcmd.concat(dst_ip);
  ESPcmd.concat("\",");
  ESPcmd.concat(port);
  ESPserial.println(ESPcmd);
  Serial.println(ESPcmd);
  delay(20);
  if (ESPserial.find("ERROR")) {
    Serial.println(F("error CIPSTART\n"));
    ESPserial.flush();
    return false;                                           // if no WiFi connection available, it continues measuring
  }

  // Frame construction
  String body = String(F("{\"time\":\""));
  body.concat(RTC_time);
  body.concat(F("\",\"voltage\":{\"value\":\""));
  body.concat(String(emon.getVrms()));
  body.concat(F("\"},\"current\":{\"value\":\""));
  body.concat(String(emon.getIrms()));
  body.concat(F("\"},\"active\":{\"value\":\""));
  body.concat(String(emon.getrealPower()));
  body.concat(F("\"},\"reactive\":{\"value\":\""));
  body.concat(String(emon.getapparentPower()));
  body.concat(F("\"},\"device\":\""));
  body.concat("prototipo"); //mac
  body.concat(F("\"}"));

  ESPcmd = F("POST /api HTTP/1.1\r\nhost: http://");
  ESPcmd.concat(dst_ip);
  ESPcmd.concat(F(":"));
  ESPcmd.concat(port);
  ESPcmd.concat(F("\r\n"));
  ESPcmd.concat(F("Content-Type: application/json\r\n"));
  ESPcmd.concat(F("Content-Length: "));
  ESPcmd.concat(String(body.length()));
  ESPcmd.concat(F("\r\n\r\n"));

  Serial.print(ESPcmd);
  Serial.println(body);

  ESPserial.print(F("AT+CIPSEND="));
  ESPserial.println(ESPcmd.length() + body.length());
  ESPserial.flush();

  delay(1000);

  if (ESPserial.find(">")) {
    ESPserial.flush();
  } else {
    ESPserial.println(F("AT+CIPCLOSE"));
    Serial.println(F("Timeout"));
    ESPserial.flush();
    return false;
  }

  ESPserial.print(ESPcmd);
  ESPserial.println(body);

  delay(500);                                         // ACORTAR TIEMPO??
  ESPserial.println(F("AT+CIPCLOSE"));
  ESPserial.flush();
  Serial.println(F("Data SENT\n"));
  return true;
}



void APset() {
  ESPserial.println(F("AT+CWQAP"));
  while (!ESPserial.find("OK")) delay(10);
  ESPserial.println(F("AT+CWMODE=2"));
  while (!ESPserial.find("OK")) delay(10);
  ESPserial.println(F("AT+CWSAP=\"esp8266_AP\",\"pass\",3,0"));   // SSID: esp8266_AP, without password
  while (!ESPserial.find("OK")) delay(10);
  ESPserial.println(F("AT+CIPMUX=1"));
  while (!ESPserial.find("OK")) delay(10);
  ESPserial.println(F("AT+CIPSERVER=1,8000"));                    // port: 8000
  while (!ESPserial.find("OK")) delay(10);
  Serial.println(F("AP Setted up"));                              // AP IP: 192.168.4.1
  ESPserial.flush();
}



void APwaitComm() {
  boolean set = false;

  while (!set) {
    if (ESPserial.available()) {
      if (ESPserial.find("CONNECT")) {
        Serial.println(F("User connected"));
        set = APmode();
        Serial.println(F("User disconnected"));
        ESPserial.println(F("AT+RST"));                            // reset necesario para cambiar de modo
      }
    }
    delay(20);
  }
}



boolean APmode() {
  String id, pwd, ip, pt, phone;
  byte stateAP = ST_HELLO;

  while (1) {
    switch (stateAP) {
      case ST_HELLO:
        sendCMD(F("HELLO, NetAlt's user.\n\n"));
        stateAP = ST_SSID;
        break;

      case ST_SSID:
        if (!sendCMD(F("Enter SSID: ")))  return false;           // el usuario salio de la conexion
        id = responseESP();
        if (id == "0,CLOSED")  return false;
        if (!sendCMD(id + F("\n")))  return false;                // el usuario salio de la conexion
        stateAP = ST_PASS;
        break;

      case ST_PASS:
        if (!sendCMD(F("Enter password: ")))  return false;       // el usuario salio de la conexion
        pwd = responseESP();
        if (pwd == "0,CLOSED")  return false;
        if (!sendCMD(pwd + F("\n")))  return false;               // el usuario salio de la conexion
        stateAP = ST_IP;
        break;

      case ST_IP:
        if (!sendCMD(F("Enter IP: ")))  return false;             // el usuario salio de la conexion
        ip = responseESP();
        if (ip == "0,CLOSED")  return false;
        if (!sendCMD(ip + F("\n")))  return false;                // el usuario salio de la conexion
        stateAP = ST_PORT;
        break;

      case ST_PORT:
        if (!sendCMD(F("Enter port: ")))  return false;           // el usuario salio de la conexion
        pt = responseESP();
        if (pt == "0,CLOSED")  return false;
        if (!sendCMD(pt + F("\n")))  return false;                // el usuario salio de la conexion
        stateAP = ST_PHONE;
        break;

      case ST_PHONE:
        if (!sendCMD(F("Enter phone number: ")))  return false;   // el usuario salio de la conexion
        phone = responseESP();
        if (phone == "0,CLOSED")  return false;
        if (!sendCMD(phone + F("\n\n")))  return false;           // el usuario salio de la conexion
        stateAP = ST_END;
        break;

      case ST_END:
        sendCMD(F("Setup complete!!!\nYou can go out the App"));
        ESPserial.println(F("AT+CIPCLOSE"));
        setRouter(id, pwd, ip, pt, phone);
        Serial.println(F("New configuration"));
        return true;
        break;
    }
  }
}



boolean sendCMD(String cmd) {
  boolean flag = true;

  ESPserial.print(F("AT+CIPSEND=0,"));
  ESPserial.println(cmd.length());
  ESPserial.flush();
  if (ESPserial.find(">")) {
    ESPserial.println(cmd);
    //Serial.println("Sending response...");
  }
  else {
    ESPserial.flush();
    ESPserial.println(F("AT+CIPCLOSE"));
    flag = false;
    Serial.println(F("No response"));
  }
  while (!ESPserial.find("OK")) delay(10);
  ESPserial.flush();

  if (flag)  return true;
  else  return false;                                           // si hay algun problema con los datos, se cierra la conexión y el usuario queda desconectado. se espera por una nueva conexión
}



String responseESP() {
  char c;
  String response = "";

  while (response.length() == 0) {
    if (ESPserial.available() > 7) {                            // se asegura de que como mínimo se haya recibido la cabecera de la petición (+IPD,0,x:xxxxxx)
      while (ESPserial.available()) {
        c = ESPserial.read();
        response.concat(c);
        response.trim();
        if (response.endsWith(":")) response = "";
      }
    }
  }
  return response;
}



/******************************/
/* DS3231 functions (RTC)     */
/******************************/
// Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte val) {
  return ( (val / 10 * 16) + (val % 10) );
}

// Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte val) {
  return ( (val / 16 * 10) + (val % 16) );
}



void setDS3231time(byte second, byte minute, byte hour, byte dayOfWeek, byte dayOfMonth, byte month, byte year) {
  // sets time and date data to DS3231
  Wire.beginTransmission(0x68);
  Wire.write(0); // set next input to start at the seconds register
  Wire.write(decToBcd(second)); // set seconds
  Wire.write(decToBcd(minute)); // set minutes
  Wire.write(decToBcd(hour)); // set hours
  Wire.write(decToBcd(dayOfWeek)); // set day of week (1=Sunday, 7=Saturday)
  Wire.write(decToBcd(dayOfMonth)); // set date (1 to 31)
  Wire.write(decToBcd(month)); // set month
  Wire.write(decToBcd(year)); // set year (0 to 99)
  Wire.endTransmission();
}



void readDS3231time(byte *second, byte *minute, byte *hour, byte *dayOfWeek, byte *dayOfMonth, byte *month, byte *year) {
  Wire.beginTransmission(0x68);
  Wire.write(0); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(0x68, 7);
  // request seven bytes of data from DS3231 starting from register 00h
  *second = bcdToDec(Wire.read() & 0x7f);
  *minute = bcdToDec(Wire.read());
  *hour = bcdToDec(Wire.read() & 0x3f);
  *dayOfWeek = bcdToDec(Wire.read());
  *dayOfMonth = bcdToDec(Wire.read());
  *month = bcdToDec(Wire.read());
  *year = bcdToDec(Wire.read());
}



String getTime() {
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

  // format example: 2016-04-14T09:58:01

  String t = "20" + String(year) + "-";
  if (String(month).length() == 1) t.concat(F("0"));
  t += String(month) + "-";
  if (String(dayOfMonth).length() == 1) t.concat(F("0"));
  t += String(dayOfMonth) + "T";
  if (String(hour).length() == 1) t.concat(F("0"));
  t += String(hour) + F(":");
  if ((int) minute < 10) t.concat(F("0"));
  t += String(minute) + F(":");
  if ((int) second < 10) t.concat(F("0"));
  t.concat(String(second));

  return t;
}



/******************************/
/* M590 functions (GPRS)      */
/******************************/
boolean M590connect() {
  M590serial.print(F("AT\r"));
  if (M590serial.find("OK")) {
    M590serial.flush();
    Serial.println(F("GPRS READY"));
    return true;
  }
  else return false;
}



void enviarSMS(String tim) {
  //M590serial.print("\r");
  //delay(1000);

  //M590serial.print("AT+CPIN?");
  //delay(1000);
  //if(M590serial.find("SIM PIN")){
  ///M590serial.print("AT+CPIN=\"4511\"\r");
  // delay(1000);
  //}

  M590serial.print(F("AT+CREG?\r"));                              //Estandar de transimision
  while (!M590serial.find("+CREG: 0,5")) delay(10);
  M590serial.flush();


  M590serial.print(F("AT+CSCS=\"GSM\"\r"));                       //Estandar de transimision
  while (!M590serial.find("OK")) delay(10);
  M590serial.flush();

  M590serial.print(F("AT+CMGF=1\r"));                             //seleccionar formato
  while (!M590serial.find("OK")) delay(10);
  M590serial.flush();

  M590serial.print("AT+CMGS=\"+34" + tlf + "\"\r");               //Numero al q mandar sms
  delay(1000);
  M590serial.print("Caída de tensión\n" + tim + "\r");            //Mensaje a enviar
  delay(1000);
  M590serial.write(0x1A);                                         //comando para enviar el sms, (CTRL-Z)
  delay(1000);
  M590serial.flush();
}



/*********************************/
/* INTERRUPTION SERVICE ROUTINES */
/*********************************/
void ISR_1SEC() {
  state = ST_SEND;
}

