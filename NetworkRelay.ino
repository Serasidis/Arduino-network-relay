/*

 Network controlled relay (based on ENC28J60 ethernet module)
 
 

 created:    31 Aug 2014 by vassilis Serasidis
  Author:    Vassilis Serasidis
    Home:    http://www.serasidis.gr
   email:    avrsite@yahoo.gr, info@serasidis.gr
 Version:    0.3
 Updated:    22 Sept 2014
 
 v0.3 (22 Sept 2014)
   ** Changed the incomming buffer type (serialData) from String to char[RX_BUFFER_LENGTH] for memory currupt protection (buffer overflow).
 
 -= Commands =-
  1*           asks for humidity, temperature, relay status and switch status (0=OFF, 1=ON)
  p1234r01*    password 1234, relay0 -> ON
  p1234r00*    password 1234, relay0 -> OFF
  p1234c5678*  change password from 1234 to 5678
  s0*          Sends the switch status (0=released)
  s1*          Sends the switch status (1=released)
  q*           Sends the connection close command (connection timeout) to android software.
  
  If a logic level change is occurred on switch pin, the arduino sends its status trough network to basic 4 android (s0* or s1*).
  
 
  Data stream format: dhh.hh#tt.tt#rs*
  d     - Initial character. It's always 'd' ('d' is for data)
  hh.hh - Humidity value from DHT22 (%)
  tt.tt - Temperature value from DHT22 (C)
  r     - Relay status 1=ON, 0=OFF
  s     - Switch status 1=Pressed, 0=Released
  *     - Is the finalizing character. Used to inform the android software that the incoming data stream has been completed.
  
  Example:
  If you send the command "1*" (without the quotes) the arduino will return:
        d52.10#29.30#10* 
  Humidity 55.10 % - Temperature 29.30 C - Relay is ON (1) - Switch is released (0).

  -= Connections =-
  Arduino pin:   Usage:
       2         Is connected the AM2302 Humidity / Temperaturee sensor. 
       7         button for restoring the password to '1234'
       8         Input switch. On that pin is connected the optocoupler K814P.
       9         Is connected the 12V DC relay through BC547 transistor. Relay is for driving electrical devices (motors, water pumps, adsl routers etc).
 
  
  This software is distributed under the GNU General Public License v3.0 http://www.gnu.org/copyleft/gpl.html
  
 */

#define TIMEOUT1 20000 //5 seconds timout
#define RELAY  9
#define SWITCH 8
#define DEFAULT_SETTINGS_PIN  7
#define BUFFER_LENGTH 30
#define RX_BUFFER_LENGTH 40 //Receiver buffer length
#define ETHERNET_PORT 10000

#include <SPI.h>
//#include <Ethernet.h>  //Select this line for using the stock ethernet library (W5100 ethernet module),

//UIPEthernet library was written by Norbert Truchsess (https://github.com/ntruchsess/arduino_uip/archive/UIPEthernet_v1.57.zip)
#include <UIPEthernet.h> //or select this line for using UIPEthernet library (ENC28J60 ethernet module).
#include "DHT.h"  //Library written by Mark Ruys  (https://github.com/markruys/arduino-DHT)
#include <EEPROM.h>

unsigned long timer1 = 0;
unsigned long timer2 = 0;
unsigned long timer3 = 0;
unsigned long timer4 = 0;

boolean alreadyConnected = false; // whether or not the client was connected previously
boolean connectionTIMEOUT1 = false;
boolean relayStatus = false;
boolean switchStatus = false;
boolean switchStatusBackup = false;

boolean stringComplete = false;
String password;
char serialData[RX_BUFFER_LENGTH];
unsigned char byteCounter;

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xEE, 0xED};
IPAddress ip(192, 168, 1, 240);

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 23 is default for telnet;
EthernetServer server(ETHERNET_PORT);
DHT dht;


void setup() {
  int i;
  unsigned char eepromData;
  pinMode(RELAY, OUTPUT);
  pinMode(SWITCH, INPUT);
  pinMode(DEFAULT_SETTINGS_PIN, INPUT);
  digitalWrite(DEFAULT_SETTINGS_PIN, HIGH); //Enabler internal pull-up resistor.
  
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  
  dht.setup(2); // data pin 2
  byteCounter = 0;
  
  // start the Ethernet connection:
  Ethernet.begin(mac, ip);
  
  // start listening for clients
  server.begin();
  
  Serial.println(F("*********************************"));
  Serial.println(F("    Arduino network relay"));
  Serial.println(F(" (c) 2014 by Vassilis Serasidis"));
  Serial.println(F("     http://www.serasidis.gr"));
  Serial.println(F("*********************************\n\n"));
  Serial.print(F("Ethernet server has been started\n  IP: "));
  Serial.print(Ethernet.localIP());
  Serial.print(F("\nPort: "));
  Serial.println(ETHERNET_PORT);


  if (EEPROM.read(0) == 0xff)  //If EEPROM is empty, write the default ASCII password '1234' in eeprom.
  {
    for(i=0;i<4;i++)
      EEPROM.write(i,0x31 + i);
    EEPROM.write(4,0); 
  }
  
  i=0;
  password = "";
  //Read the password from EEPROM and store it to 'password' string.
  for(i=0;i<4;i++)
  {
    char passwd = EEPROM.read(i);
    password += passwd;
  }
  //Serial.print(password); //Show the password on serial port .
  
  // give the Ethernet shield a second to initialize:
  delay(1000);
}

void loop()
{
    checkForClient();
    timer4 = millis();
    
    while(digitalRead(DEFAULT_SETTINGS_PIN) == LOW) // Restore factory default password '1234'.
    {
      if(millis() > timer4 + 3000)
      {
        Serial.println(F("Restore the default password '1234'. Change it as soon as possible"));
        EEPROM.write(0,'1');
        EEPROM.write(1,'2');
        EEPROM.write(2,'3');
        EEPROM.write(3,'4');
        EEPROM.write(4,0); //Terminal character
        password = "1234";
        while(digitalRead(DEFAULT_SETTINGS_PIN) == LOW)
          delay(200);
      }      
    }
    
}

//----------------------------------------------------------------------
//
//----------------------------------------------------------------------
void checkForClient()
{
  // Check for incoming clients
  EthernetClient client = server.available();
  
  if (client)
  {
      timer1 = millis();
      timer3 = millis();
      while((client.connected()&&(millis() < (timer3 + TIMEOUT1))))
      {
        switchStatus = digitalRead(SWITCH);
        if(switchStatus != switchStatusBackup)
        {
          server.write('b');
          if(switchStatus == HIGH)
            server.write('1');
          else
            server.write('0');
          server.write('*');
          switchStatusBackup = switchStatus;
        }
          
        while((client.available()) > 0)
        {
           char inData = client.read();

           if ((inData != '*') && ( byteCounter < (RX_BUFFER_LENGTH - 1)))
           {
             serialData[byteCounter++] = inData; 
           }
           else if ((inData == '*') && ( byteCounter <= (RX_BUFFER_LENGTH - 1)))
           {
             serialData[byteCounter] = '*'; //Put terminal character on RX buffer.
             checkIncomingData(byteCounter);
             byteCounter = 0;
           }
           else if(byteCounter == (RX_BUFFER_LENGTH - 1))
           {
             Serial.print(millis());
             Serial.println(F("- Incomming data Overflow"));
             byteCounter = 0;
           }            
            //Serial.write(inData);
            timer3 = millis();
        } 
        
        while(Serial.available() > 0)
        {
          char inData2 = (char)Serial.read();
          server.write(inData2); 
          timer3 = millis();
        }
      }
      server.write('q'); //Send the disconnect command to android software.
      server.write('*');
      client.stop();
  }          
}

//======================================================
//
//======================================================
void getValuesFromSensor()
{
  float humidity = dht.getHumidity();
  float temperature = dht.getTemperature(); 
  server.print("h"); 
  server.println(humidity);
  server.print("t"); 
  server.println(temperature);
  server.print("r");
  if(relayStatus == true)
    server.println("1");
  else
    server.println("0");
}

//======================================================
//
//======================================================
void checkIncomingData(unsigned char stringLength)
{
  int i;
  char data = serialData[0];
 
  //Serial.print(stringLength); //For debugging purpose only
  
  switch(data)
  {
    case '1':
      server.write('d'); //d means for 'Data'
      server.print(dht.getHumidity());
      server.write('#');
      server.print(dht.getTemperature());
      server.write('#');
      if(relayStatus == true)
        server.write('1');
      else
        server.write('0');
        
      if(switchStatus == HIGH)
        server.write('0');
      else
        server.write('1');
        
      server.write('*'); 
    break;
    
    case 'p':
      String newPassword = "";
      for(i=1;i<stringLength;i++)
      {
        if((serialData[i] >= '0')&&(serialData[i] <= '9')) //Password string must have only numbers 0-9
          newPassword += serialData[i];
        else
          break;       
      }
      if(newPassword.equals(password))
      {
        if((serialData[i] == '*'))
        {
          server.write('s');
          server.write('1');
          server.write('*');
        }
        
        if((serialData[i] == 'r')&& (serialData[i+2] == '1'))
        {
          digitalWrite(RELAY, HIGH);
          relayStatus = true;
        }
        
        if((serialData[i] == 'r')&& (serialData[i+2] == '0'))
        {
          digitalWrite(RELAY, LOW);
          relayStatus = false;
        }
          
        if(serialData[i] == 'c') //Change password command
        {
          newPassword = "";
          i++;
          for(;i<stringLength;i++)
          {
            if((serialData[i] >= '0')&&(serialData[i] <= '9'))
              newPassword += serialData[i];
            else
              break; //Terminate 'for' loop
          }
          password = newPassword; //Change password to the new one.
          for (i=0;i<password.length();i++)  //Read the password from EEPROM and store it to variable 'pin'
            EEPROM.write(i,password[i]);
        }
      }
      else
      {
        server.write('s');
        server.print("0");
        server.print('*');
      }
    break;
  }
}
