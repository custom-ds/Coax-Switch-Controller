/* Coax Switch uses: Sparkfun ESP32 Thing Plus C */


// Add buildflag ASYNCWEBSERVER_REGEX to enable the regex support

// For platformio: platformio.ini:
//  build_flags = 
//      -DASYNCWEBSERVER_REGEX

// For arduino IDE: create/update platform.local.txt
// Windows: C:\Users\(username)\AppData\Local\Arduino15\packages\espxxxx\hardware\espxxxx\{version}\platform.local.txt
// Linux: ~/.arduino15/packages/espxxxx/hardware/espxxxx/{version}/platform.local.txt
//
// compiler.cpp.extra_flags=-DASYNCWEBSERVER_REGEX=1
//
// Restart the IDE after making changes to the platform.local.txt file


#include "config.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>


#include <WiFiClient.h>
#include <Wire.h>
#include <SparkFun_Qwiic_Twist_Arduino_Library.h>
#include <SerLCD.h>

#include <Preferences.h>


/*
 * Wifi credentials are stored in config.h file. See the template.
 *
const char* ssid = "";
const char* password = "";
*/

//Defines
#define FIRMWARE_VERSION "1.0.1"
#define PAGE_TITLE "W0ZC Coax Controller"

#define DISPLAY_ADDRESS1 0x72 //This is the default address of the OpenLCD
#define MAX_CONFIG_SCREENS 6

//Global Variables
SerLCD lcd; // Initialize the library with default I2C address 0x72
TWIST twist;


Preferences prefs;
AsyncWebServer server(80);

int radio1 = 0;   //default both to disconnected
int radio2 = 0;   //default both to disconnected
int radioSelected = 1;

int displayMode = 1;    //1 = normal operation, 2 = configuration mode
int configScreen = 0;     //The current configuration screen being displayed

String sessionAPIKey = "";

// Define the labels for the 6 antennas + Disconnect
String coaxDescriptions[] = {"Disconnect", "Antenna 1", "Antenna 2", "Antenna 3", "Antenna 4", "Antenna 5", "Antenna 6"};



//Function Prototypes
void updateLCD();
void showSplash();
void updateConfigScreen(int diff);
void updateAntenna(int radio, int diff);
void connectAntenna(int radio, int antenna, bool incrOnCollision);
String getAPIKey(bool force);
void configureLCD(int r, int g, int b, int contrast);
void configureTwist(int r, int g, int b);
String generateKey();
int getRandom(int max);
bool checkAPIKey(String key);
void saveAntennaName(int antenna, String name);
void factoryReset();
void loadAntennas();
void displayConfiguration();

void notFound(AsyncWebServerRequest *request);
String getHeader();
String getFooter(String javascript);
String getPageInterface();
String getPageConfiguration();
String getJavascript();
String getCSS();






/******************************************************************************************/
void setup(void) {
  Serial.begin(115200);


  //Configure the digital I/O pins
  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);    //Disable the demultiplexer until we're fully configured

  //Multiplexer 1
  pinMode(12, OUTPUT);
  pinMode(27, OUTPUT);
  pinMode(33, OUTPUT);

  //Multiplexer 2
  pinMode(15, OUTPUT);
  pinMode(32, OUTPUT);
  pinMode(14, OUTPUT);



  prefs.begin("coax");
  radio1 = prefs.getInt("radio1");
  radio2 = prefs.getInt("radio2");

  //Do some sanity checking on the antennas selected
  if (radio1 < 0 || radio2 < 0) {
    radio1 = 0;
    radio2 = 0;
  }
  if (radio1 > 6 || radio2 > 6) {
    radio1 = 0;
    radio2 = 0;
  }
  if (radio1 == radio2 && radio1 > 0) {
    radio1 = 0;
    radio2 = 0;
  }


  //Send the reset command to the display - this forces the cursor to return to the beginning of the display
  Wire.begin();
  lcd.begin(Wire); //Set up the LCD for I2C communication
  showSplash();

  Serial.println("Starting Wifi");
  WiFi.mode(WIFI_STA);

  // Configures static IP address
  //see if we're configured for static IP
  if (useStaticIP) {
    Serial.println("Using static IP");
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("...STA Failed to configure static IP");
    }
  } else {
    Serial.println("Using DHCP");
  }



  WiFi.begin(ssid, password);
  Serial.println("");




  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }


  //Load the antenna names from eeprom
  loadAntennas();

  //default the relays to whatever is currently set (from eeprom)
  connectAntenna(1, radio1, true);
  connectAntenna(2, radio2, true);
  

  if (twist.begin() == false) {
    Serial.println("Twist is not connected.");

  }


  // ******* Server Route Handlers *******
  // Server Route Handler: /api/{key}/connect/{radio}/{antenna}
  server.on("^\\/api\\/([A-Z0-9]+)\\/connect\\/([0-9])\\/([0-9])$", HTTP_POST, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String radio = request->pathArg(1);
    String antenna = request->pathArg(2);
    int radioID = radio.toInt();
    int antennaID = antenna.toInt();

    if (checkAPIKey(apiKey) && radioID >= 1 && radioID <= 2 && antennaID >=0 && antennaID <=6) {

      connectAntenna(radioID, antennaID, true);
      request->send(200, "text/plain", "Radio " + radio + " connected to antenna " + antenna);
    } else {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
    }
  });

  // Server Route Handler: /api/{key}/set/knob/rgb/{r}/{g}/{b}
  server.on("^\\/api\\/([A-Z0-9]+)\\/set\\/knob\\/rgb\\/([0-9]+)\\/([0-9]+)\\/([0-9]+)$", HTTP_POST, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String tmp = request->pathArg(1);
    int r = tmp.toInt();
    tmp = request->pathArg(2);
    int g = tmp.toInt();
    tmp = request->pathArg(3);
    int b = tmp.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    } 

    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
      configureTwist(r, g, b);
      request->send(200, "text/plain", "Twist Knob configured");
    } else {
      request->send(400, "text/plain", "Invalid RGB values");
    }

  });

  // Server Route Handler: /api/{key}/set/lcd/rgb/{r}/{g}/{b}/{contrast}
  server.on("^\\/api\\/([A-Z0-9]+)\\/set\\/lcd\\/rgb\\/([0-9]+)\\/([0-9]+)\\/([0-9]+)\\/([0-9]+)$", HTTP_POST, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String tmp = request->pathArg(1);
    int r = tmp.toInt();
    tmp = request->pathArg(2);
    int g = tmp.toInt();
    tmp = request->pathArg(3);
    int b = tmp.toInt();
    tmp = request->pathArg(4);
    int contrast = tmp.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    } 

    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255 && contrast >= 0 && contrast <= 255) {
      configureLCD(r, g, b, contrast);
      updateLCD();    //return to whatever screen we were on.
      request->send(200, "text/plain", "LCD configured");
    } else {
      request->send(400, "text/plain", "Invalid RGB values");
    }

  });


  // Server Route Handler: /api/{key}/set/antenna/{antenna}/{name}
  server.on("^\\/api\\/([A-Z0-9]+)\\/set\\/antenna\\/([0-9]+)\\/(.+)$", HTTP_POST, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String antenna = request->pathArg(1);
    String name = request->pathArg(2);

    int antennaID = antenna.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    }

    //limit the antenna name to 13 characters
    if (name.length() > 13) {
      name = name.substring(0, 13);
    }

    if (antennaID >= 0 && antennaID <= 6) {
      saveAntennaName(antennaID, name);
      loadAntennas();   //populate the array of antenna names
      updateLCD();    //update the LCD display with the new name
      request->send(200, "text/plain", "Antenna " + antenna + " name saved");
    } else {
      request->send(400, "text/plain", "Invalid antenna number");
    }

  });

  // Server Route Handler: /api/{key}/get/antenna/{antenna}
  server.on("^\\/api\\/([A-Z0-9]+)\\/get\\/antenna\\/([0-9]+)$", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String antenna = request->pathArg(1);

    int antennaID = antenna.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    }

    if (antennaID >= 0 && antennaID <= 6) {
      request->send(200, "text/plain", coaxDescriptions[antennaID]);
    } else {
      request->send(400, "text/plain", "Invalid antenna number");
    }

  });

  // Server Route Handler: /api/{key}/get/radio/{radioID}
  server.on("^\\/api\\/([A-Z0-9]+)\\/get\\/radio\\/([0-9])$", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String radio = request->pathArg(1);

    int radioID = radio.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    }

    if (radioID == 1) {
      request->send(200, "text/plain", String(radio1));
    } else if (radioID == 2) {
      request->send(200, "text/plain", String(radio2));
    } else if (radioID == 0) {
      //return both radios separated by a :
      request->send(200, "text/plain", String(radio1) + ":" + String(radio2));
    } else {
      request->send(400, "text/plain", "Invalid radio number");
    }

  });

  // Server Route Handler: /
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getPageInterface());
  });

  // Server Route Handler: /config
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getPageConfiguration());
  });

  // Server Route Handler: /main.js
  server.on("/main.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/javascript", getJavascript());
  });

  // Server Route Handler: /main.css
  server.on("/main.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/css", getCSS());
  });
  

  // Server Route Handler: Not Found
  server.onNotFound(notFound);



  server.begin();

 //Generate the temporary session API key - Note that this should happen after Wifi has been established.
  sessionAPIKey = generateKey();  

  //Get the API key. Generate a new one if necessary
  String apiKey = getAPIKey(false);   //Don't force the creation of a new key if one already exists

  //Show the running configuration on the Serial port
  displayConfiguration();
}
/******************************************************************************************/
void loop(void) {
  
  int diff;
  static int pressCount = 0;

  if (twist.isPressed()) {
    pressCount++;
  }

  if (twist.isClicked()) {
    //the button was clicked

    if (pressCount < 5) {
      //short press - toggle the selected radio

      if (displayMode == 1) {
        //we're showing the connected antennas - normal operation mode
        if (radioSelected == 1) radioSelected = 2;
        else radioSelected = 1;
      } else {
        //we were in configuration mode - toggle the display mode
        displayMode = 1;
      }
    } else {
      //Long press - go into configuration mode
      displayMode = 2;
      configScreen = 0;   //start at the first configuration screen
    }


    pressCount = 0;     //reset the counter since we've released the button.
    updateLCD();



  }
  diff = twist.getDiff();
  if (diff != 0) {
    if (displayMode == 1) {
      //we're in the normal operation mode - just update the selected antenna
      updateAntenna(radioSelected, diff);
    } else {
      //we're in configuration mode - update the configuration screen
      updateConfigScreen(diff);
    }

    
  }
  

  delay(100);
}









/******************************************************************************************
                    Functions
******************************************************************************************/
void updateLCD() {
/* 
Shows the currently selected antennas that are connected to the radios. If we're in configuration mode, it will show the
appropriate configuration screen.
*/

  if (displayMode == 1) {
    //we're displaying the currently selected antennas

    lcd.clear(); //Clear the display - this moves the cursor to home position as well
    
    if (radioSelected == 1) {
      lcd.print("1>>");
    } else {
      lcd.print("1: ");
    }
    lcd.print(coaxDescriptions[radio1]);
    lcd.setCursor(0, 1);    //move to second line

    if (radioSelected == 2) {
      lcd.print("2>>");
    } else {
      lcd.print("2: ");
    }
    lcd.print(coaxDescriptions[radio2]);

  }
  if (displayMode == 2) {
    //We're in the configuration mode - show the appropriate setting

    lcd.clear();
    switch (configScreen) {
    case 0:
      lcd.print("Firmware Version");
      lcd.setCursor(0, 1);    //move to second line
      lcd.print(FIRMWARE_VERSION);
      break;
    case 1:
      lcd.print("API Key");
      lcd.setCursor(0, 1);    //move to second line
      lcd.print(getAPIKey(false));
      break;
    case 2:
      lcd.print("Wifi SSID");
      lcd.setCursor(0, 1);    //move to second line
      lcd.print(WiFi.SSID());
      break;
    case 3:
      lcd.print("IP Mode");
      lcd.setCursor(0, 1);    //move to second line
      if (useStaticIP) {
        lcd.print("Static");
      } else {
        lcd.print("DHCP");
      }
      break;
    case 4:
      lcd.print("IP Address");
      lcd.setCursor(0, 1);    //move to second line
      lcd.print(WiFi.localIP().toString());
      break;
    case 5:
      lcd.print("IP Mask");
      lcd.setCursor(0, 1);    //move to second line
      lcd.print(WiFi.subnetMask().toString());
      break;
    case 6:
      lcd.print("Gateway");
      lcd.setCursor(0, 1);    //move to second line
      lcd.print(WiFi.gatewayIP().toString());
      break;
    }
  }

  
}
/******************************************************************************************/
void showSplash() {
/*
Show the branded Splash screen. This is saved to the display's EEPROM so it will be displayed 
at power on, and again during the boot process.
*/
  lcd.clear(); //Clear the display - this moves the cursor to home position as well
  lcd.print("    W0ZC.com    ");
  lcd.print("Coax Controller");


}
/******************************************************************************************/
void updateConfigScreen(int diff) {
/*
Updates the currently displayed configuration screen based on the twist from the multi-purpose knob. If the
knob is twisted, the screen will change.
*/ 
  //set the max limits for how far we can scroll
  if (diff > 3) diff = 3;
  if (diff < -3) diff = -3;

  configScreen += diff;
  if (configScreen > MAX_CONFIG_SCREENS) configScreen = configScreen - (MAX_CONFIG_SCREENS + 1);
  if (configScreen < 0) configScreen = configScreen + (MAX_CONFIG_SCREENS + 1);

  updateLCD();

}
/******************************************************************************************/
void updateAntenna(int radio, int diff) {
/*
Updates the antenna selection for the specified radio based on the twist from the multi-purpose knob. If 
the antenna is already selected, it will move to the next
*/
  //cap out the maximum clicks that can be made in a cycle
  if (diff > 3) diff=3;
  if (diff < -3) diff=-3;


  if (radio == 1) {
    radio1 = radio1 + diff;
    if (radio1 > 6) {
      radio1 = radio1 - 7;
    } else {
      if (radio1 < 0) {
        radio1 = radio1 + 6;
      }
    }

    //Check to see if Radio 2 already has this antenna selected (unless we're disconnecting)
    if (radio1 == radio2 && radio1 > 0) {
      if (diff > 0) {
        //clicking up
        radio1++;
        if (radio1 > 6) {
          radio1 = radio1 - 7;
        }
      } else {
        //clicking down
        radio1--;
        if (radio1 < 0) {
          radio1 = radio1 + 7;
        }
      }
    }

    connectAntenna(1, radio1, (diff > 0));
  } 

  if (radio == 2) {
    radio2 = radio2 + diff;
    if (radio2 > 6) {
      radio2 = radio2 - 7;
    } else {
      if (radio2 < 0) {
        radio2 = radio2 + 7;
      }
    }

    //Check to see if Radio 1 already has this antenna selected (unless we're disconnecting)
    if (radio1 == radio2 && radio2 > 0) {
      if (diff > 0) {
        //clicking up
        radio2++;
        if (radio2 > 6) {
          radio2 = radio2 - 7;
        }
      } else {
        //clicking down
        radio2--;
        if (radio2 < 0) {
          radio2 = radio2 + 7;
        }
      }
    }
    connectAntenna(2, radio2, (diff > 0));
  }
}
/******************************************************************************************/
void connectAntenna(int radio, int antenna, bool incrOnCollision) {
/*
Connect the 'antenna' to the 'radio'. If incrOnCollision is true, then the antenna will be incremented if
there is a collision, otherwise it is decremented.
*/

  if (radio == 1) {
    radio1 = antenna;
  }
  if (radio == 2) {
    radio2 = antenna;
  }

  if (radio1 == radio2 && antenna > 0) {
    //we have a collision. Let both radios be in Discconect mode (0) though
    
    //Figure out which radio is being changed
    if (radio == 1) {
      //Adjust radio 1 to the next best choice
      if (incrOnCollision) {
        radio1++;
        if (radio1 > 6) {
          radio1 = 0;
        }
      } else {
        radio1--;
        if (radio1 < 0) {
          radio1 = 6;
        }
      }
    } else {
      //Adjust radio 2 to the next best choice
      if (incrOnCollision) {
        radio2++;
        if (radio2 > 6) {
          radio2 = 0;
        }
      } else {
        radio2--;
        if (radio2 < 0) {
          radio2 = 6;
        }
      }
    }
  }


  //Disable the demultiplexer
  digitalWrite(13, LOW);

  if (radio == 1) {

    switch (radio1) {
    case 1:
      digitalWrite(12, LOW);
      digitalWrite(27, LOW);
      digitalWrite(33, LOW);
      break;
    case 2:
      digitalWrite(12, HIGH);
      digitalWrite(27, LOW);
      digitalWrite(33, LOW);
      break;
    case 3:
      digitalWrite(12, LOW);
      digitalWrite(27, HIGH);
      digitalWrite(33, LOW);
      break;
    case 4:
      digitalWrite(12, HIGH);
      digitalWrite(27, HIGH);
      digitalWrite(33, LOW);
      break;
    case 5:
      digitalWrite(12, LOW);
      digitalWrite(27, LOW);
      digitalWrite(33, HIGH);
      break;
    case 6:
      digitalWrite(12, HIGH);
      digitalWrite(27, LOW);
      digitalWrite(33, HIGH);
      break;
    default:
      digitalWrite(12, HIGH);
      digitalWrite(27, HIGH);
      digitalWrite(33, HIGH);
      break;
    }

    //Save the current antenna selection to eeprom
    prefs.putInt("radio1", radio1);
  } 
  
  if (radio == 2) {


    switch (radio2) {
    case 1:
      digitalWrite(15, LOW);
      digitalWrite(32, LOW);
      digitalWrite(14, LOW);
      break;
    case 2:
      digitalWrite(15, HIGH);
      digitalWrite(32, LOW);
      digitalWrite(14, LOW);
      break;
    case 3:
      digitalWrite(15, LOW);
      digitalWrite(32, HIGH);
      digitalWrite(14, LOW);
      break;
    case 4:
      digitalWrite(15, HIGH);
      digitalWrite(32, HIGH);
      digitalWrite(14, LOW);
      break;
    case 5:
      digitalWrite(15, LOW);
      digitalWrite(32, LOW);
      digitalWrite(14, HIGH);
      break;
    case 6:
      digitalWrite(15, HIGH);
      digitalWrite(32, LOW);
      digitalWrite(14, HIGH);
      break;
    default:
      digitalWrite(15, HIGH);
      digitalWrite(32, HIGH);
      digitalWrite(14, HIGH);
      break;
    }




    //Save the current antenna selection to eeprom
    prefs.putInt("radio2", radio2);
  }
  digitalWrite(13, HIGH);

  updateLCD();


}
/******************************************************************************************/
String getAPIKey(bool force) {
/* 
Retrieves the API key from eeprom. If it doesn't exist, or if force is true, it will generate a new one. If a new
one is generated, it will be saved to eeprom.

*/


  //build an array of possible characters for the API key, exlucing look-alike characters
  char possibleChars[] = {'2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};

  bool foundValid = true;    //assume we have a valid one unless proven otherwise

  //check to see if a valid API key alredy exists in eeprom
  String storedKey = prefs.getString("apikey", "");

  if (storedKey.length() == 14) {
    //see if it's a valid key consisting only of possibleChars

    for (int i = 0; i < 14; i++) {
      bool validChar = false;
      for (int j = 0; j < 32; j++) {
        if (storedKey[i] == possibleChars[j]) {
          validChar = true;
        }
      }
      if (!validChar) {
        foundValid = false;
      }

    }
  } else {
    //it wasn't even 14 chars long
    foundValid = false;
  }

  if (foundValid && !force) {
    //we have a valid key already, and we're not forcing a new one
    return storedKey;
  }

  String key = generateKey();   //generate a new key

  prefs.putString("apikey", key);   //save the key to eeprom

  return key;
}
/******************************************************************************************/
String generateKey() {
/*
Generates a new 14 character key to be used as an API key. The key is generated from the possibleChars array which
excludes look-alike characters such as 0, O, 1, I, etc.

*/
  //build an array of possible characters for the API key, exlucing look-alike characters
  char possibleChars[] = {'2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};


  String key = "";
  for (int i = 0; i < 14; i++) {
    key += possibleChars[getRandom(32)];
  }
  return key;
}
/******************************************************************************************/
int getRandom(int max) {
/*
Get a random number between 0 and max
*/
  return (esp_random() % max);
}
/******************************************************************************************/
bool checkAPIKey(String key) {
/* 
Check the key argument against the short-term session API key (stored in RAM) and the 
long-term API key stored in eeprom. If either match, return true. Otherwise, return false.
*/
  
  if (key == sessionAPIKey) return true;    //The session key is good
  if (key == getAPIKey(false)) return true;    //The long-term key is good

  return false;
}
/******************************************************************************************/
void configureLCD(int r, int g, int b, int contrast) {
  //Configure the LCD display

 
  //Store the settings in eeprom so we can display them in the configuration screens
  prefs.putInt("lcdRed", r);
  prefs.putInt("lcdGreen", g);
  prefs.putInt("lcdBlue", b);
  prefs.putInt("lcdContrast", contrast);


  lcd.setBacklight(r, g, b); //colors from the pararmeters
  lcd.setContrast(contrast); //contrast from the parameter

  //lcd.disableSplash(); //Disable the boot-time splash screen

  showSplash(); //Show the splash screen
  lcd.saveSplash(); //Save this current text as the splash screen at next power on
  lcd.enableSplash(); //This will cause the splash to be displayed at power on
  lcd.clear();
}
/******************************************************************************************/
void configureTwist(int r, int g, int b) {
  //Configure the Twist Knob

  //Store the settings in eeprom so we can display them in the configuration screens
  prefs.putInt("knobRed", r);
  prefs.putInt("knobGreen", g);
  prefs.putInt("knobBlue", b);

  twist.setColor(r, g, b); //colors from the pararmeters
}
/******************************************************************************************/
void saveAntennaName(int antenna, String name) {
  //Save the name of the antenna to eeprom

  //limit the antenna name to 13 characters
  if (name.length() > 13) {
    name = name.substring(0, 13);
  }
  
  int szLen = name.length() + 1;
  char szName[szLen];	
	name.toCharArray(szName, szLen);

  String key = "ant" + String(antenna);
  szLen = key.length() + 1;
  char szKey[szLen];
  key.toCharArray(szKey, szLen);

  prefs.putString(szKey, szName);
}
/******************************************************************************************/
void factoryReset() {
/*
Reset the device to factory defaults. This will clear all settings and restore the device to the default.
*/

  //Clear the eeprom
  prefs.clear();

  //Reset the antenna names to the defaults
  prefs.putString("ant0", "Disconnect");
  prefs.putString("ant1", "Antenna 1");
  prefs.putString("ant2", "Antenna 2");
  prefs.putString("ant3", "Antenna 3");
  prefs.putString("ant4", "Antenna 4");
  prefs.putString("ant5", "Antenna 5");
  prefs.putString("ant6", "Antenna 6");

  //Set the default anntenna connections
  prefs.putInt("radio1", 1);
  prefs.putInt("radio2", 2);

  //Reset the display and knob colors
  configureLCD(250, 133, 5, 5);
  configureTwist(215, 117, 1);

  //Reset the API key
  getAPIKey(true);    //force the creation of a new API key

  //Reset the splash screen
  showSplash();
  lcd.saveSplash(); //Save this current text as the splash screen at next power on
  lcd.enableSplash(); //This will cause the splash to be displayed at power on
  lcd.clear();
}
/******************************************************************************************/
void loadAntennas() {
/*
Load the antenna names from eeprom
*/

  // pull the descriptions from eeprom
  coaxDescriptions[1] = prefs.getString("ant1", "");
  coaxDescriptions[2] = prefs.getString("ant2", "");
  coaxDescriptions[3] = prefs.getString("ant3", "");
  coaxDescriptions[4] = prefs.getString("ant4", "");
  coaxDescriptions[5] = prefs.getString("ant5", "");
  coaxDescriptions[6] = prefs.getString("ant6", "");

  //Check for invalid settings - reset to default
  if (coaxDescriptions[0].length() > 13) coaxDescriptions[0] = "Disconnect";
  if (coaxDescriptions[1].length() > 13) coaxDescriptions[1] = "Antenna 1";
  if (coaxDescriptions[2].length() > 13) coaxDescriptions[2] = "Antenna 2";
  if (coaxDescriptions[3].length() > 13) coaxDescriptions[3] = "Antenna 3";
  if (coaxDescriptions[4].length() > 13) coaxDescriptions[4] = "Antenna 4";
  if (coaxDescriptions[5].length() > 13) coaxDescriptions[5] = "Antenna 5";
  if (coaxDescriptions[6].length() > 13) coaxDescriptions[6] = "Antenna 6";

}
/******************************************************************************************/
void displayConfiguration() {
/*
Dumps all of the configuration information to the Serial port, including the API Key.
*/
  Serial.println("");
  Serial.println("");
  Serial.println("");

  Serial.println(PAGE_TITLE);
  Serial.println("Firmware:     " + String(FIRMWARE_VERSION));
  Serial.println("API Key:      " + getAPIKey(false));
  Serial.println("");

  Serial.println("Coax (none):  " + coaxDescriptions[0]);
  Serial.println("Coax 1:       " + coaxDescriptions[1]);
  Serial.println("Coax 2:       " + coaxDescriptions[2]);
  Serial.println("Coax 3:       " + coaxDescriptions[3]);
  Serial.println("Coax 4:       " + coaxDescriptions[4]);
  Serial.println("Coax 5:       " + coaxDescriptions[5]);
  Serial.println("Coax 6:       " + coaxDescriptions[6]);
  Serial.println("");

  Serial.println("Wifi SSID:    " + WiFi.SSID());
  Serial.println("IP Address:   " + WiFi.localIP());
  Serial.print("IP Mode:      ");
  if (useStaticIP) {
    Serial.println("Static");
  } else {
    Serial.println("DHCP");
  }
  // Serial.println("Gateway:      " + WiFi.gatewayIP());
  // Serial.println("Subnet Mask:  " + WiFi.subnetMask());
  // Serial.println("Primary DNS:  " + WiFi.dnsIP(0));
  // Serial.println("Secondary DNS:" + WiFi.dnsIP(1));
}
/******************************************************************************************/







/******************************************************************************************
                    Views
******************************************************************************************/
void notFound(AsyncWebServerRequest *request) {
/*
Respond with a 404 File Not Found error.
*/
    request->send(404, "text/plain", "Not found");
}
/******************************************************************************************/
String getHeader() {

  String html = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>%TITLE%</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css">
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.7.1/jquery.min.js"></script>

    <link rel="stylesheet" href="/main.css">   

    
</head>
<body>
    <header class="bg-primary text-white py-3">
        <div class="container d-flex justify-content-between align-items-center">
            <h1 class="mb-0">%TITLE%</h1>
            <a class="btn btn-light" href="/config">Configuration</a>
            <a class="btn btn-light" href="/">Control</a>
        </div>
    </header>

)"; 
  
    html.replace("%TITLE%", PAGE_TITLE);

    return html;
  }
/******************************************************************************************/
String getFooter(String javascript) {

  String html = R"(
<footer class="bg-dark text-white py-3 mt-5">
        <div class="container">
            <p class="mb-0">%TITLE%</br /><a href="http://www.w0zc.com/" target="_blank">W0ZC.com</a></p>
        </div>
    </footer>

    <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
    <script src="/main.js"></script>
    <script type="text/javascript">
%JAVASCRIPT%
    </script>

    
</body>
</html>    
    


  )"; 

  html.replace("%TITLE%", PAGE_TITLE);
  html.replace("%JAVASCRIPT%", javascript);

  return html;
}
/******************************************************************************************/
String getPageInterface() {

  String html = R"(
    <main class="container mt-4">
        <form>
            <h1>Connections</h1>
            <div class="container">
                <div class="row">
                    <div class="col-md-6">
                        <h2 class="text-center">Radio 1</h2>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(1, 1);" id="rad1ant1">%ANT1%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(1, 2);" id="rad1ant2">%ANT2%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(1, 3);" id="rad1ant3">%ANT3%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(1, 4);" id="rad1ant4">%ANT4%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(1, 5);" id="rad1ant5">%ANT5%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(1, 6);" id="rad1ant6">%ANT6%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(1, 0);" id="rad1ant0">%ANT0%</a>
                    </div>
                    <div class="col-md-6">
                        <h2 class="text-center">Radio 2</h2>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(2, 1);" id="rad2ant1">%ANT1%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(2, 2);" id="rad2ant2">%ANT2%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(2, 3);" id="rad2ant3">%ANT3%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(2, 4);" id="rad2ant4">%ANT4%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(2, 5);" id="rad2ant5">%ANT5%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(2, 6);" id="rad2ant6">%ANT6%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="connectAnt(2, 0);" id="rad2ant0">%ANT0%</a>
                    </div>
                </div>
            </div>
        </form>
    </main>
  )"; 


  //Replace placeholders with actual values
  html.replace("%ANT0%", coaxDescriptions[0]);
  html.replace("%ANT1%", coaxDescriptions[1]);
  html.replace("%ANT2%", coaxDescriptions[2]);
  html.replace("%ANT3%", coaxDescriptions[3]);
  html.replace("%ANT4%", coaxDescriptions[4]);
  html.replace("%ANT5%", coaxDescriptions[5]);
  html.replace("%ANT6%", coaxDescriptions[6]);

  String js = R"(
// Update the connected antenna every 500ms
setInterval(tmrRefresh, 500);
)";

  //Concat header, body, and footer
  String header = getHeader();
  String footer = getFooter(js);
  html = header + html + footer;

  return html;



}
/******************************************************************************************/
String getPageConfiguration() {

  String html = R"(
<main class="container mt-4">
  <div class="container">
    <div class="row">
        <div class="col-md-6">
        <form>
            <h2>Antenna Labels</h2>
            <div class="form-group">
                <label for="antenna1">Antenna 1:</label>
                <input type="text" class="form-control" id="antenna1" name="antenna1" maxlength="13" value="%ANT1%">
            </div>
            <div class="form-group">
                <label for="antenna2">Antenna 2:</label>
                <input type="text" class="form-control" id="antenna2" name="antenna2" maxlength="13" value="%ANT2%">
            </div>
            <div class="form-group">
                <label for="antenna3">Antenna 3:</label>
                <input type="text" class="form-control" id="antenna3" name="antenna3" maxlength="13" value="%ANT3%">
            </div>
            <div class="form-group">
                <label for="antenna4">Antenna 4:</label>
                <input type="text" class="form-control" id="antenna4" name="antenna4" maxlength="13" value="%ANT4%">
            </div>
            <div class="form-group">
                <label for="antenna5">Antenna 5:</label>
                <input type="text" class="form-control" id="antenna5" name="antenna5" maxlength="13" value="%ANT5%">
            </div>
            <div class="form-group">
                <label for="antenna6">Antenna 6:</label>
                <input type="text" class="form-control" id="antenna6" name="antenna6" maxlength="13" value="%ANT6%">
            </div>
            
            <div class="form-group">
                <button type="button" class="btn btn-primary w-100 mt-4" onclick="saveAntennaNames();">Save Antennas</button>
            </div>                
        </form>
      </div>
      <div class="col-md-6">
        
        <form>
            <h2>Front Panel Configuration</h2>
            <h4>LCD Display</h4>

            <div class="form-group">
                <label for="lcdRed">Red:</label>
                <input type="range" class="form-range" id="lcdRed" min="0" max="255" step="1" value="%LCDRED%">
            </div>
            <div class="form-group">
                <label for="lcdGreen">Green:</label>
                <input type="range" class="form-range" id="lcdGreen" min="0" max="255" step="1" value="%LCDGREEN%">
            </div>
            <div class="form-group">
                <label for="lcdBlue">Blue:</label>
                <input type="range" class="form-range" id="lcdBlue" min="0" max="255" step="1" value="%LCDBLUE%">
            </div>
            <div class="form-group">
                <label for="lcdContrast">LCD Contrast (lower value is more contrast):</label>
                <input type="range" class="form-range" id="lcdContrast" min="0" max="100" step="1" value="%LCDCONTRAST%">
            </div>


            <h4>Knob Color</h4>
            <div class="form-group">
                <label for="knobRed">Red:</label>
                <input type="range" class="form-range" id="knobRed" min="0" max="255" step="1" value="%KNOBRED%">
            </div>
            <div class="form-group">
                <label for="knobGreen">Green:</label>
                <input type="range" class="form-range" id="knobGreen" min="0" max="255" step="1" value="%KNOBGREEN%">
            </div>
            <div class="form-group">
                <label for="knobBlue">Blue:</label>
                <input type="range" class="form-range" id="knobBlue" min="0" max="255" step="1" value="%KNOBBLUE%">
            </div>


            <div class="form-group">
                <button type="button" class="btn btn-primary w-100 mt-4" onclick="saveColors();">Save Colors</button>
            </div> 
        </form>
        </div>
      </div>
    </div>

    <div class="row">
      <div class="col-md-12">
        <span class="text-center">Firmware Version: %FIRMWARE%</span>
      </div>
    </div>
    </main>    
  
  )";


  //Replace placeholders with actual values
  html.replace("%ANT1%", coaxDescriptions[1]);
  html.replace("%ANT2%", coaxDescriptions[2]);
  html.replace("%ANT3%", coaxDescriptions[3]);
  html.replace("%ANT4%", coaxDescriptions[4]);
  html.replace("%ANT5%", coaxDescriptions[5]);
  html.replace("%ANT6%", coaxDescriptions[6]);


  html.replace("%LCDRED%", String(prefs.getInt("lcdRed")));
  html.replace("%LCDGREEN%", String(prefs.getInt("lcdGreen")));
  html.replace("%LCDBLUE%", String(prefs.getInt("lcdBlue")));
  html.replace("%LCDCONTRAST%", String(prefs.getInt("lcdContrast")));

  html.replace("%KNOBRED%", String(prefs.getInt("knobRed")));
  html.replace("%KNOBGREEN%", String(prefs.getInt("knobGreen")));
  html.replace("%KNOBBLUE%", String(prefs.getInt("knobBlue")));

  html.replace("%FIRMWARE%", FIRMWARE_VERSION);


  //Concat header, body, and footer
  String header = getHeader();
  String footer = getFooter("");
  html = header + html + footer;

  return html;


}
/******************************************************************************************/
String getJavascript() {

  String html = R"(



var apiKey = "%APIKEY%";

function saveAntennaNames() {
    setAntennaName(1, document.getElementById('antenna1').value);
    setAntennaName(2, document.getElementById('antenna2').value);
    setAntennaName(3, document.getElementById('antenna3').value);
    setAntennaName(4, document.getElementById('antenna4').value);
    setAntennaName(5, document.getElementById('antenna5').value);
    setAntennaName(6, document.getElementById('antenna6').value);
}

async function setAntennaName(ant, name) {
try {
    var uri = '/api/' + apiKey + '/set/antenna/' + ant.toString() + '/' + name;
    //console.log(uri);

    const response = await fetch(uri, {
        method: 'POST'
    });

    if (response.ok) {
        const result = await response.text();
    } else {
        alert('Error in API call.');
    }

} catch (error) {
    console.log(error);
    //alert('An error occurred');
}
}

function saveColors() {
    lcdRed = document.getElementById('lcdRed').value;
    lcdGreen = document.getElementById('lcdGreen').value;
    lcdBlue = document.getElementById('lcdBlue').value;
    lcdContrast = document.getElementById('lcdContrast').value;

    knobRed = document.getElementById('knobRed').value;
    knobGreen = document.getElementById('knobGreen').value;
    knobBlue = document.getElementById('knobBlue').value;

    setColor('lcd', lcdRed, lcdGreen, lcdBlue, lcdContrast);
    setColor('knob', knobRed, knobGreen, knobBlue, 0);
    
}

async function setColor(destination, r, g, b, contrast) {
    try {
        

        var uri = '/api/' + apiKey + '/set/' + destination + '/rgb/' + r.toString() + '/' + g.toString() + '/' + b.toString();
        if (destination == 'lcd') {
            uri += '/' + contrast.toString();
        }
        //console.log(uri);

        const response = await fetch(uri, {
            method: 'POST'
        });

        if (response.ok) {
            const result = await response.text();
        } else {
            alert('Error in API call.');
        }
    } catch (error) {
        console.log(error);
        //alert('An error occurred');
    }
}

async function connectAnt(radio, antenna) {
    try {
        

        var uri = '/api/' + apiKey + '/connect/' + radio.toString() + '/' + antenna.toString();
        //console.log(uri);

        const response = await fetch(uri, {
            method: 'POST'
        });

        if (response.ok) {
            const result = await response.text();
        } else {
            alert('Error in API call.');
        }
    } catch (error) {
        console.log(error);
        //alert('An error occurred');
    }
}

var refreshRadio = 1;

async function tmrRefresh() {
  try {
  

  var uri = '/api/' + apiKey + '/get/radio/0';
  //console.log(uri);

        const response = await fetch(uri, {
            method: 'GET'
        });

        if (response.ok) {
            const antennas = await response.text();

            console.log("Get Results: " + antennas);

            var ants = antennas.split(":");

            //loop through all of the rad1ant buttons and set the class to btn-primary except for the one that is connected
            for (var i = 0; i < 7; i++) {
                var button = document.getElementById('rad1ant' + i);
                if (i == ants[0]) {
                    button.className = "btn btn-success w-100 mb-2";
                } else {
                    if (i == 0) {
                        button.className = "btn btn-secondary w-100 mb-2";
                    } else {
                        button.className = "btn btn-primary w-100 mb-2";
                    }
                }
            }   

            //loop through all of the rad1ant buttons and set the class to btn-primary except for the one that is connected
            for (var i = 0; i < 7; i++) {
                var button = document.getElementById('rad2ant' + i);
                if (i == ants[1]) {
                    button.className = "btn btn-success w-100 mb-2";
                } else {
                    if (i == 0) {
                        button.className = "btn btn-secondary w-100 mb-2";
                    } else {
                        button.className = "btn btn-primary w-100 mb-2";
                    }
                }
            }   


        } else {
            //alert('Error in API call.');
        }


  } catch (error) {
    console.log(error);
  }
}
  )";


  html.replace("%APIKEY%", sessionAPIKey);

  return html;
}
/******************************************************************************************/
String getCSS() {

  String html = R"(

body {
    width: 100%;
    height: 80%;
    margin: auto;
}
#main {
    height: 400px;
}
#color {
    margin-left: 10%;
    width: 50%;
}
  
  )";

  return html;

}