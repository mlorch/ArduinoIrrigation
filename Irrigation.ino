
/* Irrigation Control System with 5 valves
 partially based on the Arduino NTP sample code and the Arduino Telnet sample code
*/
#define WIFI
//#define ETHERNET

#define MAX_CMD_LENGTH   30

#include <Time.h> 
#include <avr/wdt.h>
#include <SD.h>

#if defined(WIFI)
  #include <WiFi.h>
  #include <WiFiUdp.h>
#endif

#if defined(ETHERNET)
  #include <Ethernet.h>
  #include <EthernetUdp.h>
#endif

#if defined(WIFI)
  char ssid[] = "";         //  your network SSID (name)
  char pass[] = ""; // your network password
  int status = WL_IDLE_STATUS;
  WiFiUDP Udp;
  WiFiServer server = WiFiServer(23);
  WiFiClient client;
#endif

#if defined(ETHERNET)
  EthernetUDP Udp;
  EthernetServer server = EthernetServer(23);
  EthernetClient client;
  byte mac[] = { 0x90, 0xA2, 0xDA, 0x07, 0x00, 0xC9 };  // the MAC of your Ethernet shield
#endif

const int c_no_of_valves = 5;  // number of valves; if this number is changed, the variables sw and pins have to be changed accordingly
int sw[5] = {0,0,0,0,0};
int pins[5] = {2, 3, 5, 6, 8}; // the Arduino PINS that will be used to switch the valves/releais
IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server set another NTP server here
const int timeZone = 2;     // Central European Time; time adjustment over UTC
unsigned int localPort = 8888;  // local port to listen for UDP packets
byte cmd_valve[10]; // the valve that should be switched on/off
byte cmd_weekday[10]; // the weekdays on which the valve should be turned on
time_t cmd_timeOn[10]; // time when the valve should go on
time_t cmd_timeOff[10];  // time when the valve should not be turned on anymore
boolean connected = false;
int lastconnected = 0;
String cmd = String();
byte tncommand = 0;
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets
const int resetdayminutes = 240; // time when the daily reset occurs

void setup()
{
  wdt_disable();  // disable the watchdog immediately in setup. Otherwise we end up in an endless re-boot loop.
//  Serial.begin(9600);
  
#if defined(WIFI)
  // attempt to connect to Wifi network:
  while ( status != WL_CONNECTED) { 
    status = WiFi.begin(ssid, pass);
  }
#endif

#if defined(ETHERNET)
  while (Ethernet.begin(mac) == 0);
#endif
  
  Udp.begin(localPort);
  
  setSyncProvider(getNtpTime);
  setSyncInterval(60*60);

  // set the pins as output
  for (int i=0; i<c_no_of_valves ; i++)
  {
    pinMode(pins[i], OUTPUT);
  }
  
  for (int i=0;i<10;i++)
  {
    cmd_valve[i] = 0;
    cmd_weekday[i] = B0000000;
    cmd_timeOn[i] = 0;
    cmd_timeOff[i] = 0;
  }
  
  server.begin();
  
  // now initialize the SD card library
  pinMode(10, OUTPUT); // according to the sample sketch of SD, this has to be done for SD to work
   
  if (SD.begin(4)) {
    readSD();
  }
  
  wdt_enable(WDTO_8S); // turn on the watchdog function and set to 8s. Setting it much lower runs the risk of an endless loop.
}


void loop()
{  
#if defined (WIFI)
  if (WiFi.status() != WL_CONNECTED) {
    while(true); // go into an endless loop when WIFI is not connected, this should reset the Arduino through the watchdog
  }

  if (server.status() == 0) {
    while (true);
  }
#endif

  wdt_reset(); // reset the watchdog, sketch is still alive
  
  int t_dayminutes = hour()*60+minute();
  
  if ((t_dayminutes == resetdayminutes) and (second() == 0)) {
    while (true); // reset through watchdog at 4am
  }
  
  int wd = weekday();
    
  if (timeStatus() != timeNotSet) {
    // now loop through the control strings and set the output switches
    // first set all to off
    for (int i=0; i<c_no_of_valves ; i++) {
      sw[i] = 0;
    }
  
    int t_sw;
    int t_weekday;
    int t_checkdmfrom;
    int t_checkdmto;
    
    for (int i=0; i<10 ; i++) {
      if (bitRead(cmd_weekday[i], wd-1) == 1) {
          t_sw = cmd_valve[i];
          
          t_checkdmfrom = hour(cmd_timeOn[i])*60+minute(cmd_timeOn[i]);
          t_checkdmto = hour(cmd_timeOff[i])*60+minute(cmd_timeOff[i]);
          
          // now check if this one should be turned on
          if ((t_dayminutes>=t_checkdmfrom) and (t_dayminutes<=t_checkdmto)) {
              sw[t_sw] = 1;
          }
        }
     }

    for (int i=0; i<c_no_of_valves; i++) {
      if (sw[i]==1) {
        digitalWrite(pins[i], HIGH);
      }
      else
      {
        digitalWrite(pins[i], LOW);
      }
    }
  }
  else
  {
    // time not set
  }
  
  // wait for a new client:
  client = server.available();

  if (client == true) {
    if (!connected) {
      client.flush();
      connected = true;
      client.println("Hello");
      client.print(">");
      cmd = "";
      lastconnected = t_dayminutes;
    }

    if (client.available() > 0) {
      readTelnetCommand(client.read());
    }
  }
  if (connected and ((t_dayminutes-lastconnected)>5)) {
    client.println("bye");
    client.stop();
    connected = false;
  }
}

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis(); 
  while (millis() - beginWait < 1500) {    
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:                 
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void readTelnetCommand(char c) {

  if(cmd.length() == MAX_CMD_LENGTH) {
    cmd = "";
  }

  if (tncommand>0) { // if we are still in a telnet command, discard and set the counter +1
    tncommand++;
  }
  
  if (tncommand>3) { // if two characters after -1 passed, start to append them again.
    tncommand=0;
  }
  
  if (c==-1) { // capture telnet commands and discard them. Usually it is started by -1 and then followed by another 2 characters.
    tncommand = 1;
  }
  
  
  if (tncommand == 0) { // if we are not in a command, append the character to the string and do what you need to do
    cmd += c;
    if (c == '\n') {
      if(cmd.length() > 2) {
        // remove \r and \n from the string
        cmd = cmd.substring(0,cmd.length() - 2);
        parseCommand();
      }
    }
  }
}

void parseCommand() {
  
  boolean cmdError = true; // assume error until told otherwise
  
  if(cmd.equals("exit")) {
      client.println("bye");
      client.stop();
      connected = false;
      cmdError = false;
  }
  if(cmd.equals("help")) {
      server.println("--- Command Help ---");
      server.println("time   : current date/time");
      server.println("set    : set X A:7654321:HH:MM:HH:MM");
      server.println("         X:position; A:valve; 7654321: weekdays (set to 0 or 1)");
      server.println("reset  : set all to 0");
      server.println("exit   : close the connection");
      server.println("ls     : list all");
      server.println("on     : on X");
      cmdError = false;
  }
  if(cmd.startsWith("set")) {
     if (cmd.length()==27) {
        char t_c = cmd.charAt(4); // the position/index to be written to
        if (t_c>='0' and t_c<='9') { // just make sure it is within boundaries
           parseCommand (t_c-48, cmd.substring(6, 27));
           server.println("data set");
           wdt_reset();
 //          writeSD();
//           server.println("File has been written to SD.");
           cmdError = false;
        }
     }
  }
  
  if(cmd.startsWith("get")) {
     if (cmd.length()==5) {
        char t_c = cmd.charAt(4);
        if (t_c>='0' and t_c<='9') {
          server.print("Valve:");
          server.println(cmd_valve[t_c-48]);
          server.print("Weekday:");
          server.println(cmd_weekday[t_c-48], BIN);
          server.print("On:");
          server.print(hour(cmd_timeOn[t_c-48]));
          server.print(":");
          server.println(minute(cmd_timeOn[t_c-48]));
          server.print("Off:");
          server.print(hour(cmd_timeOff[t_c-48]));
          server.print(":");
          server.println(minute(cmd_timeOff[t_c-48]));
          cmdError = false;
        }
     }
  }
  
  if(cmd.startsWith("on")) {
     if (cmd.length()==4) {
        char t_c = cmd.charAt(3);
        if (t_c>='0' and t_c<='9') {
          cmd_valve[0] = t_c-48;
          cmd_weekday[0] = B1111111;
          cmd_timeOn[0] = now();
          cmd_timeOff[0] = now()+(15*60); // 15 minutes
          server.print(t_c);
          server.println(" turned on for 15 minutes");
          cmdError = false;
        }
     }
   }
   
   if(cmd.equals("reset")) {
      for (int i=0; i<10; i++) {
        cmd_valve[i] = 0;
        cmd_weekday[i] = B0000000;
        cmd_timeOn[i] = 0;
        cmd_timeOff[i] = 0;
      }
      server.println("all set to 0");
      cmdError = false;
  }
  
  if (cmd.equals("time")) {
      server.print("Time:");
      server.print(hour());
      server.print(":");
      server.print(minute());
      server.print(":");
      server.println(second());
      server.print("Date:");
      server.print(day());
      server.print(".");
      server.print(month());
      server.print(".");
      server.println(year());
      server.print("Weekday:");
      server.println(weekday());
      cmdError = false;
  }
  
  if (cmd.equals("ls")) {
    for (int i=0;i<10;i++) {
      server.print(cmd_valve[i]);
      server.print(":");
      for (int j=0;j<7;j++) {
        wdt_reset(); // since the output takes a while ensure the watchdog does not reset
        server.print(bitRead(cmd_weekday[i],j));
      }
      server.print(":");
      printDigits(hour(cmd_timeOn[i]));
      server.print(":");
      printDigits(minute(cmd_timeOn[i]<10));
      server.print(":");
      printDigits(hour(cmd_timeOff[i]));
      server.print(":");
      printDigits(minute(cmd_timeOff[i]));
      server.println();
    }
    cmdError = false;
  }
  cmd = "";
  if (cmdError) {
    server.println("error");
  }
  server.print(">");
}

void printDigits(int digits){
  if(digits < 10)
    server.print('0');
  server.print(digits);
}

void readSD()
// this function reads the SD card file and puts the results into the cmd array
{
  File myFile = SD.open("cmds.txt");
  if (myFile) {
    char c = 0;
    String cmd = String();
    byte line = 0;
    // read from the file until there's nothing else in it:
    while (myFile.available())
    {
      c = myFile.read();
      cmd += c;
      if (c == '\n') {
        if(cmd.length() > 2) {
          // remove \r and \n from the string
          cmd = cmd.substring(0,cmd.length() - 2);
          parseCommand(line, cmd);
          line++; // increase the line
          if (line>9) {
            break;
          }
        }
      }
    }
  }
  myFile.close();
}

void writeSD()
// this function writes the command array onto the SD card
{
  // open the file
  File myFile = SD.open("cmds.txt", FILE_WRITE);
  
  // if the file opened okay, write to it:
  if (myFile) {
    for (int i=0;i<10;i++) {
      myFile.print(cmd_valve[i]);
      myFile.print(":");
      for (int j=0;j<7;j++) {
        wdt_reset(); // since the output takes a while ensure the watchdog does not reset
        myFile.print(bitRead(cmd_weekday[i],j));
      }
      myFile.print(":");
      if (hour(cmd_timeOn[i])<10) {
        myFile.print("0");
      }
      myFile.print(hour(cmd_timeOn[i]));
      myFile.print(":");
      if (minute(cmd_timeOn[i])<10) {
        myFile.print("0");
      }
      myFile.print(minute(cmd_timeOn[i]));
      myFile.print(":");
      if (hour(cmd_timeOn[i])<10) {
        myFile.print("0");
      }
      myFile.print(hour(cmd_timeOff[i]));
      myFile.print(":");
      if (minute(cmd_timeOff[i])<10) {
        myFile.print("0");
      }
      myFile.print(minute(cmd_timeOff[i]));
      myFile.println();
    }
    myFile.close();
  } 
}

void parseCommand (byte pos, String cmd)
{
   cmd_valve[pos] = cmd.charAt(6)-48;          
   for (int j=0;j<7;j++) {
     bitWrite(cmd_weekday[pos], j, cmd.charAt(14-j)-48);
   }
   TimeElements tm;
   tm.Second = 0;
   tm.Hour = ((cmd.charAt(16)-48)*10)+(cmd.charAt(17)-48);
   tm.Minute = ((cmd.charAt(19)-48)*10)+(cmd.charAt(20)-48);
   tm.Day = 1;
   tm.Month = 1;
   tm.Year = 0;
   cmd_timeOn[pos] = makeTime(tm);
   tm.Hour = ((cmd.charAt(22)-48)*10)+(cmd.charAt(23)-48);
   tm.Minute = ((cmd.charAt(25)-48)*10)+(cmd.charAt(26)-48);
   cmd_timeOff[pos] = makeTime(tm);  
}
