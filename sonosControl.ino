/*
  Sonos Controller
  BDJ
  
*/

#include <WemosButton.h>
#include <WemosSonos.h>
#include <WemosSetup.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include <LiquidCrystal_I2C.h>


#include "settings.h"


unsigned long lastButton = millis();


//length must not exceed WFS_MAXBODYLENGTH in WemosSetup.cpp, currently 1500
#define MAXBODYCHLENGTH 1024

//AP definitions
const char* ssid = "WiFi SSID";
const char* password = "*********";

IPAddress ip(192, 168, 1, 239); // hardcode ip for esp
IPAddress gateway(192, 168, 1, 222); //router gateway
IPAddress subnet(255, 255, 252, 0); //lan subnet

IPAddress SONOSIP(192, 168, 1, 242);

//Sonos discovery crap
int noOfDiscoveries = 0;
bool discoveryInProgress = false;
int device = -1;
unsigned long timeToFirstDiscovery = 0;


//Teh buttonz
const int BTN_PLAY = 12;
const int BTN_PREV = 13;
const int BTN_NEXT = 0;
const int BTN_VU = 2;
const int BTN_VD = 14;


boolean goTime = false;

unsigned int lastCheck = millis();

int kitchenSonos = -1;

WemosSetup wifisetup;
WemosSonos sonos;
WemosButton knobButton;

String roomNames[SNSESP_MAXNROFDEVICES];


const unsigned long checkRate =  1000 * 32; //how often main loop performs periodical task. NOTE-this also updates read volume
const unsigned long checkRate2 =  1000 * 6; //how often main loop performs periodical task. NOTE-this also updates read volume

int discoverSonosTimeout = 2; //seconds

boolean isAsleep = false;



int volume;

//LCD Stuff init
LiquidCrystal_I2C lcd(0x27, 16, 2);

// the setup function runs once when you press reset or power the board
void setup() {
   // lcd.begin(16,2);
   lcd.begin();
    lcd.backlight();

  
  //Serial. Slow as shit.

  WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setPhyMode(WIFI_PHY_MODE_11N);
  Serial.begin(9600);
  Serial.println("");
  Serial.println("");
  Serial.println("LOLSONOS");

  lcd.print("NuimoSucks v0.1a");
  lcd.setCursor(0,1);
  lcd.print("Initial Boot!");

  // Connect to WiFi network
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(". ");
    delay(300);
  }

  // Start the server
  Serial.println("  CONNECTED ");
  Serial.print("IP:  " );
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Connected");
  
  lcd.setCursor(0,1);
  lcd.print(WiFi.localIP());

  //wifisetup.begin(WIFI_STA, 0, BUILTIN_LED);//try to connect

  wifisetup.afterConnection("/search");

  wifisetup.server.on("/search", handleSearch);
  wifisetup.server.on("/setup", handleSetup);

  
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BTN_PLAY, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_VU, INPUT_PULLUP);
  pinMode(BTN_VD, INPUT_PULLUP);

  //Gotta have an LED ofc.
   digitalWrite(LED_BUILTIN, HIGH); 


  Serial.println("Start discover sonos");
  discoverSonos(discoverSonosTimeout);
  Serial.println("Stop discover sonos");


  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Sonos Found:");
  lcd.setCursor(0,1);
  String room = sonos.roomName(device);
  lcd.print(room );
  lastButton = millis();

  goTime = true;

  volume = sonos.getVolume(device);
  playInfo();

}

// the loop function runs over and over again forever
void loop() {
  
  if(!digitalRead(BTN_PLAY)) {
    buttonPressed();
    togglePlay(device);
    Serial.println("PLAY");      
  }  
  if(!digitalRead(BTN_NEXT)) {
    buttonPressed();
    sonos.next(device);    
  playInfo();
    Serial.println("NEXT");      
  }
  if(!digitalRead(BTN_PREV)) {
    buttonPressed();
    sonos.previous(device);
  playInfo();
    Serial.println("PREV");      
  }
  if(!digitalRead(BTN_VU)) {
    buttonPressed();
    volume = volume + 10;
    sonos.setVolume(volume, device);
    Serial.println("VolUP");      
  }
  if(!digitalRead(BTN_VD)) {
    buttonPressed();
    volume = volume - 10;
    sonos.setVolume(volume, device);
    Serial.println("VolDn");      
  }



  wifisetup.inLoop();



  if ((lastButton + checkRate) <= millis()) {
    //put any code here that should run periodically
    //wifisetup.printInfo();
    goToSleep();
  
    //update oldVolume regularly, as other devices might change the volume as well
    if (device >= 0) {
      //oldVolume = sonos.getVolume(device);
    }
  }


  
}

void playInfo() {
  String artist = sonos.getArtist(device);
  String title = sonos.getTitle(device);
  lcd.clear();
  lcd.print(artist);
  lcd.setCursor(0,1);
  lcd.print(title);
  
}


void buttonPressed() {  
  lastButton = millis();
  Serial.println(lastButton);
  Serial.println(checkRate);
  if (isAsleep) {
    wakeUp();
  }
}

void wakeUp() {
     lcd.clear();
    lcd.print("WiFi Waking...");
    lcd.backlight();
    WiFi.forceSleepWake();
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(". ");
      delay(500);
    }
    isAsleep = false;
    lcd.clear();
    lcd.print("WiFi Awake!");
    playInfo();
}

void goToSleep() {
     lcd.clear();
    lcd.print("WiFi zzz");
    lcd.noBacklight();
    isAsleep = true;
    WiFi.forceSleepBegin();    
}


void togglePlay(int device) {
  //if the device is a stand alone device, togglePlay is as simple as checking transport info.
  //If it is playing, pause it, if not, play it. However, if the device is controlled by a
  //coordinator, it is much more complicated. The procedure below starts to assume that the
  //device is stand alone, and tries to pause or play. If it does not work, it checks for
  //the coordiniator, and pauses or plays the coordinator instead. Each step is explained.

  //step 1: check transport info of device
  String deviceTransportInfo = sonos.getTransportInfo(device);
  String deviceTransportInfoAfter;
  String coordinatorTransportInfo;

  //step 2: check if status is playing
  if (deviceTransportInfo == "PLAYING") {
    //step 3: try to pause device if it is playing
    sonos.pause(device);
    deviceTransportInfoAfter = sonos.getTransportInfo(device);
    if (deviceTransportInfoAfter == "PAUSED_PLAYBACK") {
      //step 4: if it succeded, everything is done
    } else {
      //step 5: if not, check who is the coordinator
      int coordinator = sonos.getCoordinator(device);
      //step 6: check transport info of coordinator
      if (coordinator >= 0) {
        coordinatorTransportInfo = sonos.getTransportInfo(coordinator);
      } else {
        coordinatorTransportInfo = "UNKNOWN";
      }
      if (coordinatorTransportInfo == "PLAYING") {
        //step 7: pause coordinator if it is playing
        sonos.pause(coordinator);
      } else  if (coordinatorTransportInfo == "PAUSED_PLAYBACK" || coordinatorTransportInfo == "STOPPED") {
        //step 8: play coordinator instead, if it is paused or stopped
        sonos.play(coordinator);
      }
    }
  } else if (deviceTransportInfo == "PAUSED_PLAYBACK") {
    //step 9: try to play device if it is paused
    sonos.play(device);
    deviceTransportInfoAfter = sonos.getTransportInfo(device);
    if (deviceTransportInfoAfter == "PLAYING") {
      //step 10: if it succeded, everything is done 
    } else {
      //step 11: if not, something probably went wrong, maybe there is no queue to play
    }
  } else if (deviceTransportInfo == "STOPPED") {
    //step 12: try to play device if is stopped
    sonos.play(device);
    //step 13: check if device has coordinator, because it must play too
    int coordinator = sonos.getCoordinator(device);
    if (coordinator < 0) {
      //step 14: if there is NO coordinator, everything is done
      //done, no need to do anything
    } else {
      //step 15: check transport info of coordinator if ther is one
      coordinatorTransportInfo = sonos.getTransportInfo(coordinator);
      if (coordinatorTransportInfo == "PLAYING") {
        //step 16: if coordinator is playing, everything is done. However, it might take several minutes before the device starts playing
      } else if (coordinatorTransportInfo == "PAUSED_PLAYBACK" || coordinatorTransportInfo == "STOPPED") {
        //step 17. play coordinator if it paused or stopped
        sonos.play(coordinator);
      }
    }
  } else {
    //step 18: something went wrong, this should not happen
  }
}



void handleSearch() {
  wfs_debugprintln("handleSearch - searches for Sonos devices");

  long reloadTime = discoverSonosTimeout * 1000 + 4000 + 5000;
  sprintf(wifisetup.onload, "i1=setInterval(function() {location.href='/setup';},%i)", reloadTime);
  sprintf(wifisetup.body, "<h2>Wifi connection</h2><p>Searching for Sonos devices...</p>");
  wifisetup.sendHtml(wifisetup.body, wifisetup.onload);
  //give time to show page before discovery
  for (int i = 0; i < 2000; i++) { //2000 takes ca 4 s xxx could be made into a delay function
    wifisetup.inLoop();
  }
  wfs_debugprintln("discover sonos 3 (in handleSearch)");
  discoverSonos(discoverSonosTimeout);
  wifisetup.timeToChangeToSTA = millis() + 10 * 60 * 1000;
}

void handleSetup() {
  wfs_debugprintln("handleSetup");
  char bodych[MAXBODYCHLENGTH];
  int selectedDevice = -1;

  if (wifisetup.server.hasArg("room")) {
    String roomStr = wifisetup.server.arg("room");
    device = roomStr.toInt();

    if (device >= 0 && sonos.getNumberOfDevices() > device) {
      settings.roomname = roomNames[device];
      wfs_debugprintln(settings.roomname);
      wfs_debugprint("Saving settings... ");
      settings.save();
    }

    wifisetup.timeToChangeToSTA = millis() + 5 * 60 * 1000;

    wfs_debugprint("setting device to ");
    wfs_debugprintln(device);
  }

  String body;
  body = "<h2>Wifi connection</h2>";
  if (WiFi.SSID().length() == 0) {
    body = body + "<p>No network selected</p>";
  } else {
    body = body + "<p>Selected network: ";
    body = body + WiFi.SSID(); + "</p>";
  }
  body = body + "<p>Connection status: ";
  if (WiFi.status() != WL_CONNECTED) {
    body = body + "not connected</p>";
  } else {
    body = body + "connected</p>";
  }
  body = body + "<p><a href='/'>Change network</a></p>";
  body = body + "<h2>Sonos devices</h2>";
  if (sonos.getNumberOfDevices() == 0) {
    body = body + "<p>No sonos devices found</p>";
  } else {
    body = body + "<p>Click to select Sonos device: <ul>";
    for (int i = 0; i < sonos.getNumberOfDevices(); i++) {
      if (body.length() < MAXBODYCHLENGTH - 300) { //truncate if very many devices
        body = body + "<li><a href=setup?room=" + i + ">" + roomNames[i] + "</a>";
        if (roomNames[i] == settings.roomname && roomNames[i] != "") {
          body = body + " (selected)";
          selectedDevice = i;
        }
        body = body + "</li>";
      }
    }
    body = body + "</ul></p>";
  }
  if (selectedDevice == -1) {
    body = body + "<p>No Sonos device selected</p>";
  }
  body = body + "<p><a href='/search'>Search for Sonos devices</a></p>";

  body.toCharArray(bodych, MAXBODYCHLENGTH);
  sprintf(wifisetup.body, "%s", bodych);
  sprintf(wifisetup.onload, "");
  wifisetup.sendHtml(wifisetup.body, wifisetup.onload);
}

void discoverSonos(int timeout) {
  if (!discoveryInProgress) {
    discoveryInProgress = true;
    noOfDiscoveries++;
    wifisetup.stopWebServer();//unpredicted behaviour if weserver contacted during discovery multicast
    Serial.println("discover sonos in library");
    sonos.discoverSonos(timeout);
    wifisetup.startWebServer();
    Serial.print("Found devices: ");
    Serial.println(sonos.getNumberOfDevices());
    for (int i = 0; i < sonos.getNumberOfDevices(); i++) {
      Serial.print("Device ");
      Serial.print(i);
      Serial.print(": ");
      Serial.print(sonos.getIpOfDevice(i));
      Serial.print(" ");
      String roomName = sonos.roomName(i);
      roomNames[i] = roomName;
      Serial.print(roomName);
      if (roomName == "Portable" && roomName != "") {
        Serial.print(" *");
        device = i;
      }
      Serial.println("");
    }
    Serial.println("discoverReady");
    //wifisetup.startWebServer();
    discoveryInProgress = false;
  }
}
