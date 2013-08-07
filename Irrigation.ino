/* Irrigation Control System with 5 valves
 partially based on the Arduino NTP sample code and the Arduino Telnet sample code
*/
#include <Time.h> 
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <avr/wdt.h>

char ssid[] = "";  //  your network SSID (name)
char pass[] = "";       // your network password
const int c_no_of_valves = 5; // number of valves. If you change this number, make sure to also change the variables sw and pins accordingly
int sw[5] = {0,0,0,0,0};
int pins[5] = {2, 3, 5, 6, 8};

#define MAX_CMD_LENGTH   30

int status = WL_IDLE_STATUS;

// NTP Servers:
IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server

const int timeZone = 2;     // Central European Time

WiFiUDP Udp;
WiFiServer server = WiFiServer(23);

unsigned int localPort = 8888;  // local port to listen for UDP packets

String cmds[10]; // this contains the switch commands. Not the most elegant way, but it's on the list of things to change.
String logstr;

WiFiClient client;
boolean connected = false;
int lastconnected = 0;
String cmd;
byte tncommand = 0;

void setup() 
{
  wdt_disable();  // disable the watchdog immediately in setup. Otherwise we end up in an endless re-boot loop.
  Serial.begin(9600);
  
  // check for the presence of the WiFi shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    // don't continue:
    while(true);
  } 


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
  
// set the initial command strings
// the format is switch, seven days of week, start time HH:MM, end time HH:MM

  cmds[0] = "0:0000000:00:00:00:00";
  cmds[1] = "0:0000011:21:00:21:15";
  cmds[2] = "1:0000011:21:15:21:30";
  cmds[3] = "2:0000011:21:30:21:45";
  cmds[4] = "3:0000011:21:45:22:05";
  cmds[5] = "4:0000011:22:05:22:25";
  cmds[6] = "0:0000000:00:00:00:00";
  cmds[7] = "0:0000000:00:00:00:00";
  cmds[8] = "0:0000000:00:00:00:00";
  cmds[9] = "0:0000000:00:00:00:00";
  server.begin();
  
  wdt_enable(WDTO_8S); // turn on the watchdog function and set to 8s. Setting it much lower runs the risk of an endless loop.
}


void loop()
{  
  
  wdt_reset(); // reset the watchdog, sketch is still alive
  
  int t_dayminutes;
  int wd;
  
  if (timeStatus() != timeNotSet) {
    // now loop through the controld strings and set the output switches
    // first set all to off
    for (int i=0; i<c_no_of_valves ; i++) {
      sw[i] = 0;
    }
  
    int t_sw;
    int t_hourfrom;
    int t_hourto;
    int t_minutefrom;
    int t_minuteto;
    int t_weekday;
    
    t_dayminutes = hour()*60+minute();
    wd = weekday();
    
    int t_checkdmfrom;
    int t_checkdmto;
    
    for (int i=0; i<10 ; i++) {
      if (cmds[i].charAt(1+wd) == '1') {
          t_sw = cmds[i].charAt(0)-48;
          
          t_hourfrom = (cmds[i].charAt(10)-48) * 10 + cmds[i].charAt(11)-48;
          t_minutefrom = (cmds[i].charAt(13)-48) * 10 + cmds[i].charAt(14)-48;
          t_hourto = (cmds[i].charAt(16)-48) * 10 + cmds[i].charAt(17)-48;
          t_minuteto = (cmds[i].charAt(19)-48) * 10 + cmds[i].charAt(20)-48;
          
          t_checkdmfrom = t_hourfrom*60+t_minutefrom;
          t_checkdmto = t_hourto*60+t_minuteto;
          
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
  if (connected and ((t_dayminutes-lastconnected)>2)) {
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
      server.println("on     : on X");
  }
  if(cmd.startsWith("set")) {
     if (cmd.length()==27) {
        char t_c = cmd.charAt(4);
        if (t_c>='0' and t_c<='9') {
          cmds[t_c-48] = cmd.substring(6,27);
          server.print(t_c);
          server.print(" has been changed to ");
          server.println(cmd.substring(6,27)); 
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
          server.println(cmds[t_c-48]);
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
          cmds[0] = "0:1111111:00:00:24:00";
          cmds[0][0] = t_c;
          server.print(t_c);
          server.println(" turned on");
        }
     }
     else
     {
       server.println("I'm sure I do not understand");
     }
   }
   
   if(cmd.equals("reset")) {
      for (int i=0; i<10; i++) {
        cmds[i] = "0:0000000:00:00:00:00";
      }
      server.println("All cmds set to 0");
      
  }
  
  if (cmd.equals("time")) {
      server.print("Current Time:");
      server.print(hour());
      server.print(":");
      server.print(minute());
      server.print(":");
      server.println(second());
      server.print(day());
      server.print(".");
      server.print(month());
      server.print(".");
      server.println(year());
      server.print("weekday:");
      server.println(weekday());
  }
  
  cmd = "";
  server.print(">");
}
