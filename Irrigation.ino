/* Irrigation Control System with 5 valves
 partially based on the Arduino NTP sample code and the Arduino Telnet sample code
*/

#include <Time.h> 
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <avr/wdt.h>

char ssid[] = "";         //  your network SSID (name)
char pass[] = ""; // your network password
const int c_no_of_valves = 5;  // number of valves; if this number is changed, the variables sw and pins have to be changed accordingly
int sw[5] = {0,0,0,0,0};
int pins[5] = {2, 3, 5, 6, 8}; // the Arduino PINS that will be used to switch the valves/releais

#define MAX_CMD_LENGTH   30

int status = WL_IDLE_STATUS;

IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server set another NTP server here

const int timeZone = 2;     // Central European Time; time adjustment over UTC

WiFiUDP Udp;
WiFiServer server = WiFiServer(23);

unsigned int localPort = 8888;  // local port to listen for UDP packets

byte cmd_valve[10]; // the valve that should be switched on/off
byte cmd_weekday[10]; // the weekdays on which the valve should be turned on
time_t cmd_timeOn[10]; // time when the valve should go on
time_t cmd_timeOff[10];  // time when the valve should not be turned on anymore

WiFiClient client;
boolean connected = false;
int lastconnected = 0;
String cmd = String();

byte tncommand = 0;

void setup() 
{
  wdt_disable();  // disable the watchdog immediately in setup. Otherwise we end up in an endless re-boot loop.
  Serial.begin(9600);
  
  // attempt to connect to Wifi network:
  while ( status != WL_CONNECTED) { 
    status = WiFi.begin(ssid, pass);
  }

  Udp.begin(localPort);
  
  setSyncProvider(getNtpTime);
  setSyncInterval(60*60);

  // set the pins as output
  for (int i=0; i<c_no_of_valves ; i++) {
    pinMode(pins[i], OUTPUT);
  }
  
  for (int i=0;i<10;i++) {
    cmd_valve[i] = 0;
    cmd_weekday[i] = B0000000;
    cmd_timeOn[i] = 0;
    cmd_timeOff[i] = 0;
  }

  server.begin();
  
  wdt_enable(WDTO_8S); // turn on the watchdog function and set to 8s. Setting it much lower runs the risk of an endless loop.
}


void loop()
{  
  if (WiFi.status() != WL_CONNECTED) {
    while(true); // go into an endless loop when WIFI is not connected, this should reset the Arduino through the watchdog
  }
  if (server.status() == 0) {
    while (true);
  }
  
  wdt_reset(); // reset the watchdog, sketch is still alive
  
  int t_dayminutes;
  int wd;
  
  if (timeStatus() != timeNotSet) {
    // now loop through the control strings and set the output switches
    // first set all to off
    for (int i=0; i<c_no_of_valves ; i++) {
      sw[i] = 0;
    }
  
    int t_sw;
    int t_weekday;
    
    t_dayminutes = hour()*60+minute();
    wd = weekday();
    
    int t_checkdmfrom;
    int t_checkdmto;
    
    for (int i=0; i<10 ; i++) {
      if (bitRead(cmd_weekday[i], wd) == 1) {
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
    client.println("timeout. bye.");
    client.stop();
    connected = false;
  }

}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis(); // this won't handle overflows correctly?
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
  
  if(cmd.equals("exit")) {
      client.println("bye.");
      client.stop();
      connected = false;
  }
  if(cmd.equals("help")) {
      server.println("--- Telnet Server Help ---");
      server.println("time   : current date/time");
      server.println("set    : set X A:1234567:HH:MM:HH:MM");
      server.println("reset  : set all to 0");
      server.println("exit   : close the connection");
      server.println("ls     : list all");
      server.println("on     : on X");
  }
  if(cmd.startsWith("set")) {
     if (cmd.length()==27) {
        char t_c = cmd.charAt(4); // the position/index to be written to
        if (t_c>='0' and t_c<='9') { // just make sure it is within boundaries
           cmd_valve[t_c-48] = cmd.charAt(6)-48;          
           for (int j=1;j<8;j++) {
             bitWrite(cmd_weekday[t_c-48], j, cmd.charAt(15-j)-48);
           }
           TimeElements tm;
           tm.Second = 0;
           tm.Hour = ((cmd.charAt(16)-48)*10)+(cmd.charAt(17)-48);
           tm.Minute = ((cmd.charAt(19)-48)*10)+(cmd.charAt(20)-48);
           tm.Day = 1;
           tm.Month = 1;
           tm.Year = 0;
           cmd_timeOn[t_c-48] = makeTime(tm);
           tm.Hour = ((cmd.charAt(22)-48)*10)+(cmd.charAt(23)-48);
           tm.Minute = ((cmd.charAt(25)-48)*10)+(cmd.charAt(26)-48);
           cmd_timeOff[t_c-48] = makeTime(tm);
           server.println("Data has been set.");
        }
     }
     else
     {
       server.println("I'm sure I do not understand");
     }
  }
  
  if(cmd.startsWith("get")) {
     if (cmd.length()==5) {
        char t_c = cmd.charAt(4);
        if (t_c>='0' and t_c<='9') {
          server.println(server.status());
          server.print("Valve:");
          server.println(cmd_valve[t_c-48]);
          server.print("Weekday:");
          server.println(cmd_weekday[t_c-48], BIN);
          server.print("Time On:");
          server.print(hour(cmd_timeOn[t_c-48]));
          server.print(":");
          server.println(minute(cmd_timeOn[t_c-48]));
          server.print("Time Off:");
          server.print(hour(cmd_timeOff[t_c-48]));
          server.print(":");
          server.println(minute(cmd_timeOff[t_c-48]));
        }
     }
     else
     {
       server.println("I'm sure I do not understand");
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
        }
     }
     else
     {
       server.println("I'm sure I do not understand");
     }
   }
   
   if(cmd.equals("reset")) {
      for (int i=0; i<10; i++) {
        cmd_valve[i] = 0;
        cmd_weekday[i] = B0000000;
        cmd_timeOn[i] = 0;
        cmd_timeOff[i] = 0;
      }
      server.println("All cmds set to 0");
      
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
  }
  
  if (cmd.equals("ls")) {
    for (int i=0;i<10;i++) {
      server.print(cmd_valve[i]);
      server.print(":");
      for (int j=1;j<8;j++) {
        server.print(bitRead(cmd_weekday[i],j));
      }
      server.print(":");
      server.print(hour(cmd_timeOn[i]));
      server.print(":");
      server.print(minute(cmd_timeOn[i]));
      server.print(":");
      server.print(hour(cmd_timeOff[i]));
      server.print(":");
      server.println(minute(cmd_timeOff[i]));
    }
  }
  cmd = "";
  server.print(">");
}
