/* 
 homecontrol incl. Intertechno BT-Switch Support - v1.2
 
 Arduino Sketch für homecontrol 
 Copyright (c) 2016 Daniel Scheidler All right reserved.
 
 homecontrol ist Freie Software: Sie können es unter den Bedingungen
 der GNU General Public License, wie von der Free Software Foundation,
 Version 3 der Lizenz oder (nach Ihrer Option) jeder späteren
 veröffentlichten Version, weiterverbreiten und/oder modifizieren.
 
 homecontrol wird in der Hoffnung, dass es nÃ¼tzlich sein wird, aber
 OHNE JEDE GEWÄHRLEISTUNG, bereitgestellt; sogar ohne die implizite
 GewÃ¤hrleistung der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
 Siehe die GNU General Public License für weitere Details.
 
 Sie sollten eine Kopie der GNU General Public License zusammen mit diesem
 Programm erhalten haben. Wenn nicht, siehe <http://www.gnu.org/licenses/>.
 */


#include <SPI.h>
#include <Ethernet.h>
#include <MemoryFree.h>
#include <RCSwitch.h>
#include <SoftwareSerial.h>

// ---------------------------------------------------------------
// --                      START CONFIG                         --
// ---------------------------------------------------------------
#define TRANSMITTER_PIN 7      // Transmitter is connected to Arduino Pin #7  
#define RECEIVER_PIN 2         //Receiver is on Interrupt 0 - Arduino Pin #2 

// Bluetooth 
//#define BT_STATE 8
#define BT_EN 6
#define BT_RX 8
#define BT_TX 9

char btLearnCode1[] = "Tr4v"; 
char btLearnCode2[] = "5f42";
char btLearnCode3[] = "G4n1";
char btLearnCode4[] = "3f5#";
char btLearnCode5[] = "2m2F";

char* on = "ON";
char* off= "OFF";

boolean serialOut = true; // FÃ¼r Debugzwecke aktivierbar (sorgt auf Dauer fÃ¼r AbstÃ¼rze)
// ---------------------------------------------------------------
// --                       END CONFIG                          --
// ---------------------------------------------------------------



SoftwareSerial bluetooth(BT_RX, BT_TX); 
bool switched_on = false;
unsigned long tprev = 0;


// Netzwerkdienste
EthernetServer HttpServer(80); 
EthernetClient interfaceClient;


// Webseiten/Parameter
char*      rawCmdAnschluss          = (char*)malloc(sizeof(char)*10);
char*      rawCmdDimmLevel          = (char*)malloc(sizeof(char)*10);
const int  MAX_BUFFER_LEN           = 80; // max characters in page name/parameter 
char       buffer[MAX_BUFFER_LEN+1]; // additional character for terminating null

// Instance of RCSwitch
RCSwitch mySwitch = RCSwitch();

int lastMem = freeMemory();

#if defined(__SAM3X8E__)
    #undef __FlashStringHelper::F(string_literal)
    #define F(string_literal) string_literal
#endif

const __FlashStringHelper * htmlHeader;
const __FlashStringHelper * htmlHead;
const __FlashStringHelper * htmlFooter;

// ------------------ Reset stuff --------------------------
void(* resetFunc) (void) = 0;
unsigned long resetMillis;
boolean resetSytem = false;
// --------------- END - Reset stuff -----------------------



/**
 * SETUP
 *
 * Grundeinrichtung der HomeControl
 * - Serielle Ausgabe aktivieren
 * - TFT initialisieren
 * - Netzwerk initialisieren
 * - Webserver starten
 * - IN-/OUT- Pins definieren
 * - Waage initialisieren (Tara)
 */
void setup() {
  unsigned char mac[]  = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED  };
  unsigned char ip[]   = { 192, 168, 1, 12 };
  unsigned char dns[]  = { 192, 168, 1, 1  };
  unsigned char gate[] = { 192, 168, 1, 1  };
  unsigned char mask[] = { 255, 255, 255, 0  };

  // BlueTooth
  pinMode(BT_EN,OUTPUT);
  digitalWrite(BT_EN, LOW);
  bluetooth.begin(38400);
  
  // Serial initialisieren
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  Serial.println(F("HomeControl v1"));
  Serial.println();

  mySwitch.enableTransmit(TRANSMITTER_PIN);
  mySwitch.enableReceive(RECEIVER_PIN);
 
  // Optional set pulse length.
  //mySwitch.setPulseLength(350);  

  // Netzwerk initialisieren
  Ethernet.begin(mac, ip, dns, gate, mask);
  HttpServer.begin();

  Serial.print( F("IP: ") );
  Serial.println(Ethernet.localIP());

  initStrings();
}

/**
 * LOOP
 * 
 * Standard-Loop-Methode des Arduino Sketch
 *
 * - Webserver: 
 *    * auf Client warten
 *      * Falls Client verbunden ist entsprechende Webseite ausgeben
 *        und Verbindung beenden.
 */
void loop() {
  EthernetClient client = HttpServer.available();
  if (client) {
    while (client.connected()) {
      if(client.available()){        
        if(serialOut){
          Serial.println(F("Website anzeigen"));
        }
        showWebsite(client);
        
        if(serialOut){
          Serial.print(F("> Freier Speicher: "));
          Serial.println(freeMemory());
        }
        delay(100);
        client.stop();
      }
    }
  }
  delay(100);
  // Gecachte URL-Parameter leeren
  memset(rawCmdAnschluss,0, sizeof(rawCmdAnschluss)); 
  memset(rawCmdDimmLevel,0, sizeof(rawCmdDimmLevel)); 
}


/**
 * switchBT(char[] unitNr, char[] action)
 * 
 * Sendet einen Schaltbefehl an den gebundenen Intertechno BT-Switch
 * Eine Anleitung, wie der BT-Switch gebunden gibts hier:
 * https://smart-home-technology.ch/en/blog/control-your-intertechno-devices-with-an-arduino
 * 
 * @param btLearnCode - ist die Gruppe bzw das Passwort fÃ¼r die 16 Units
 * @param unitNr      - ist die Nummer (1-16) des GerÃ¤ts
 * @param action      - zum ein-/ausschalten muss hier "ON" oder "OFF" Ã¼bergeben werden.
 *                      Um das GerÃ¤t zu Dimmen muss eine Ganzzahl zwischen 1 und 16 Ã¼bergeben werden.
 *
 * 
 * Dim-Level:
 * 01 =  6,25%          09 = 56,25%
 * 02 = 12,50%          10 = 62,50%
 * 03 = 18,75%          11 = 68,75%
 * 04 = 25,00%          12 = 75,00%
 * 05 = 31,25%          13 = 81,25%
 * 06 = 37,50%          14 = 87,50%
 * 07 = 43,25%          15 = 93,75%
 * 08 = 50,00%          16 = 100,0%
 */
void switchBT(char btLearnCode[], int unitNr, char action[]){
   while(bluetooth.available()) {
     bluetooth.read();
   }

  char unitNrChar[5];
  sprintf(unitNrChar,"%02i",unitNr);

  int len = strlen(btLearnCode)+strlen(unitNrChar)+strlen(action)+5;
  char n2a[len];

  strncpy(n2a, "\n", len);
  strncat(n2a, btLearnCode, len);
  strncat(n2a, "+", 1);
  strncat(n2a, unitNrChar, len);
  strncat(n2a, "+", 1);
  strncat(n2a, action, len);
  
  if(serialOut){
    Serial.print(F("BT-CODE: "));
    Serial.println(n2a);
  }

  bluetooth.write(n2a);
 //bluetooth.write("\nTOM#4+01+ON");
}





// ---------------------------------------
//     RcSwitch Hilfsmethoden
// ---------------------------------------
/**
 * Schaltet die GerÃ¤te per Funk / BT 
 * 
 * 1-50    = 433-MHz Funkstandard (Funksteckdosen mit Dip-Schalter)
 * 51-306  = Intertechno 433MHz Drehrad-Kodierung von A-01 bis P-16
 * 307-386 = Intertechno BT-Switch 
 */
void switchWirelessOutlet(int number){
   switchWirelessOutlet(number, 0);
}

void switchOnOff( char switchCode, int switchId, boolean switchOn, int numberStk){
  if(switchOn){
    mySwitch.switchOn(switchCode, switchId, numberStk);  
  } else {
    mySwitch.switchOff(switchCode,switchId, numberStk);  
  }
}

void switchWirelessOutlet(int number, int dimm){
  mySwitch.disableReceive();

  if(serialOut){
    Serial.print(F("Schalte: "));
    Serial.println(number);
  }
  delay(10);

  char dimmLevel[5];
  sprintf(dimmLevel,"%02i",dimm);

  int numberStk = number % 5;  
  if (numberStk == 0) numberStk = 5;
  if (numberStk < 0) numberStk = numberStk*(-1);
  
  int numberStkIT = (number-50) % 4;
  if (numberStkIT == 0) numberStkIT = 4;
  if (numberStkIT < 0) numberStkIT = numberStkIT*(-1);
  
  int switchNr = number>=0?number:number * (-1);
  
  int switchId = 0;
  boolean switchOn = true;
  
  //Intertechno BT-Switch
  if (switchNr > 10000) {
    mySwitch.send(switchNr , 24);
  } else if (switchNr > 370 && switchNr <= 386){
    switchBT(btLearnCode5, (switchNr-370), (dimm>0&&dimm<17)?dimmLevel:(number<0?off:on));
  } else if (switchNr > 354){
    switchBT(btLearnCode4, (switchNr-354), (dimm>0&&dimm<17)?dimmLevel:(number<0?off:on));
  } else if (switchNr > 338){
    switchBT(btLearnCode3, (switchNr-338), (dimm>0&&dimm<17)?dimmLevel:(number<0?off:on));
  } else if (switchNr > 322){
    switchBT(btLearnCode2, (switchNr-322), (dimm>0&&dimm<17)?dimmLevel:(number<0?off:on));
  } else if (switchNr > 306){
    switchBT(btLearnCode1, (switchNr-306), (dimm>0&&dimm<17)?dimmLevel:(number<0?off:on));
  } else if (switchNr > 290){
    switchOnOff('p',((((switchNr-290)-1)/4)+1), number>=0, numberStkIT);  
  } else if (switchNr > 274){
    switchOnOff('o',((((switchNr-274)-1)/4)+1), number>=0, numberStkIT);   
  } else if (switchNr > 258){
    switchOnOff('n',((((switchNr-258)-1)/4)+1), number>=0, numberStkIT);  
  } else if (switchNr > 242){
    switchOnOff('m',((((switchNr-242)-1)/4)+1), number>=0, numberStkIT);   
  } else if (switchNr > 226){
    switchOnOff('l',((((switchNr-226)-1)/4)+1), number>=0, numberStkIT);    
  } else if (switchNr > 210){
    switchOnOff('k',((((switchNr-210)-1)/4)+1), number>=0, numberStkIT);    
  } else if (switchNr > 194){
    switchOnOff('j',((((switchNr-194)-1)/4)+1), number>=0, numberStkIT);   
  } else if (switchNr > 178){
    switchOnOff('i',((((switchNr-178)-1)/4)+1), number>=0, numberStkIT);    
  } else if (switchNr > 162){
    switchOnOff('h',((((switchNr-162)-1)/4)+1), number>=0, numberStkIT);    
  } else if (switchNr > 146){
    switchOnOff('g',((((switchNr-146)-1)/4)+1), number>=0, numberStkIT);    
  } else if (switchNr > 130){
    switchOnOff('f',((((switchNr-130)-1)/4)+1), number>=0, numberStkIT);    
  } else if (switchNr > 114){
    switchOnOff('e',((((switchNr-114)-1)/4)+1), number>=0, numberStkIT);   
  } else if (switchNr > 98){
    switchOnOff('d',((((switchNr-98)-1)/4)+1), number>=0, numberStkIT);   
  } else if (switchNr > 82){
    switchOnOff('c',((((switchNr-82)-1)/4)+1), number>=0, numberStkIT);    
  } else if (switchNr > 66){
    switchOnOff('b',((((switchNr-66)-1)/4)+1), number>=0, numberStkIT);   
  } else if (switchNr > 50){
    switchOnOff('a',((((switchNr-50)-1)/4)+1), number>=0, numberStkIT);    
  } else {
    //Standard Logik
  if(number>=0){
      mySwitch.switchOff(int2bin(((switchNr-1)/5)+1), numberStk);
   } else {
      mySwitch.switchOn(int2bin(((switchNr-1)/5)+1), numberStk);
   }
  } 

  delay(10);
  mySwitch.enableReceive(0);
}


// ---------------------------------------
//     Webserver Hilfsmethoden
// ---------------------------------------

/**
 *  URL auswerten und entsprechende Seite aufrufen
 */
void showWebsite(EthernetClient client){
  char * HttpFrame =  readFromClient(client);
  
 // delay(200);
  boolean pageFound = false;
  
  char *ptr = strstr(HttpFrame, "favicon.ico");
  if(ptr){
    pageFound = true;
  }
  ptr = strstr(HttpFrame, "index.html");
  if (!pageFound && ptr) {
    runIndexWebpage(client);
    pageFound = true;
  } 
  ptr = strstr(HttpFrame, "rawCmd");
  if(!pageFound && ptr){
    runRawCmdWebpage(client, HttpFrame);
    pageFound = true;
  } 

  delay(200);

  ptr=NULL;
  HttpFrame=NULL;

 if(!pageFound){
    runIndexWebpage(client);
  }
  delay(20);
}




// ---------------------------------------
//     Webseiten
// ---------------------------------------

/**
 * Startseite anzeigen
 */
void  runIndexWebpage(EthernetClient client){
  showHead(client);

  client.print(F("<h4>Navigation</h4><br/>"
    "<a href='/rawCmd'>Manuelle Schaltung</a><br>"));

  showFooter(client);
}


/**
 * rawCmd anzeigen
 */
void  runRawCmdWebpage(EthernetClient client, char* HttpFrame){
  if (atoi(rawCmdAnschluss)!=0 ) {
    postRawCmd(client, rawCmdAnschluss);
    return;
  }

  showHead(client);
  
  client.println(F(  "<h4>Manuelle Schaltung</h4><br/>"
                     "<form action='/rawCmd'>"));

  client.println(F( "<b>Anschluss: </b>" 
                    "<select name=\"schalte\" size=\"1\" > "));
  
  for(int i=0; i<=386;i++){
    client.print(F( "  <option value=\""));
    client.print(i); 
    client.print(F( "\">Anschluss "));
    client.print(i);
    client.println(F( "</option>"));
  }

  client.println(F( "</select>" ));
  client.println(F( "<input type='submit' value='Abschicken'/>"
                    "</form>"));

  showFooter(client);
}


void postRawCmd(EthernetClient client, char* anschluss){
  showHead(client);
    
  client.println(F( "<h4> Funk schalten</h4><br/>" ));
  if(atoi(rawCmdDimmLevel)>0 && atoi(rawCmdDimmLevel)<17){
    switchWirelessOutlet(atoi(anschluss), atoi(rawCmdDimmLevel));
  } else {
    switchWirelessOutlet(atoi(anschluss));
  }
  
  showFooter(client);
}





// ---------------------------------------
//     HTML-Hilfsmethoden
// ---------------------------------------

void showHead(EthernetClient client){
  client.println(htmlHeader);
  client.print("IP: ");
  client.println(Ethernet.localIP());
  client.println(htmlHead);
}


void showFooter(EthernetClient client){
  client.println(F("<div  style=\"position: absolute;left: 30px; bottom: 40px; text-align:left;horizontal-align:left;\" width=200>"));
  client.print(F("<b>Freier Speicher:</b> "));                              
  client.println(freeMemory()); 
  client.println(F("</div>"));
  client.print(htmlFooter);
}


void initStrings(){
  htmlHeader = F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n");
  
  htmlHead = F("<html><head>"
    "<title>HomeControl</title>"
    "<style type=\"text/css\">"
    "body{font-family:sans-serif}"
    "*{font-size:14pt}"
    "a{color:#abfb9c;}"
    "</style>"
    "</head><body text=\"white\" bgcolor=\"#494949\">"
    "<center>"
    "<hr><h2>HomeControl</h2><hr>") ;
    
    htmlFooter = F( "</center>"
    "<a  style=\"position: absolute;left: 30px; bottom: 20px; \"  href=\"/\">Zurueck zum Hauptmenue;</a>"
    "</body></html>");
    
}


// ---------------------------------------
//     Ethernet - Hilfsmethoden
// ---------------------------------------
/**
 * Zum auswerten der URL des Ã¼bergebenen Clients
 * (implementiert um angeforderte URL am lokalen Webserver zu parsen)
 */
char* readFromClient(EthernetClient client){
  char paramName[20];
  char paramValue[20];
  char pageName[20];
  
  if (client) {
  
    while (client.connected()) {
  
      if (client.available()) {
        memset(buffer,0, sizeof(buffer)); // clear the buffer

        client.find("/");
        
        if(byte bytesReceived = client.readBytesUntil(' ', buffer, sizeof(buffer))){ 
          buffer[bytesReceived] = '\0';

          if(serialOut){
            Serial.print(F("URL: "));
            Serial.println(buffer);
          }
          
          if(strcmp(buffer, "favicon.ico\0")){
            char* paramsTmp = strtok(buffer, " ?=&/\r\n");
            int cnt = 0;
            
            while (paramsTmp) {
            
              switch (cnt) {
                case 0:
                  strcpy(pageName, paramsTmp);
                  if(serialOut){
                    Serial.print(F("Domain: "));
                    Serial.println(buffer);
                  }
                  break;
                case 1:
                  strcpy(paramName, paramsTmp);
                
                  if(serialOut){
                    Serial.print(F("Parameter: "));
                    Serial.print(paramName);
                  }
                  break;
                case 2:
                  strcpy(paramValue, paramsTmp);
                  if(serialOut){
                    Serial.print(F(" = "));
                    Serial.println(paramValue);
                  }
                  pruefeURLParameter(paramName, paramValue);
                  break;
              }
              
              paramsTmp = strtok(NULL, " ?=&/\r\n");
              cnt=cnt==0?1:cnt==1?2:1;
            }
          }
        }
      }// end if Client available
      break;
    }// end while Client connected
  } 

  return buffer;
}


void pruefeURLParameter(char* tmpName, char* value){
  if(strcmp(tmpName, "schalte")==0 && strcmp(value, "")!=0){
    strcpy(rawCmdAnschluss, value);
    
    if(serialOut){
      Serial.print(F("Anschluss: "));
      Serial.println(rawCmdAnschluss);    
    }
  }  
  if(strcmp(tmpName, "dimm")==0 && strcmp(value, "")!=0){
    strcpy(rawCmdDimmLevel, value);
    
    if(serialOut){
      Serial.print(F("Dimm-Level: "));
      Serial.println(rawCmdAnschluss);    
    }
  }  
}

// ---------------------------------------
//     Allgemeine Hilfsmethoden
// ---------------------------------------
char* int2bin(unsigned int x){
  static char buffer[6];
  for (int i=0; i<5; i++) buffer[4-i] = '0' + ((x & (1 << i)) > 0);
  buffer[68] ='\0';
  return buffer;
}
