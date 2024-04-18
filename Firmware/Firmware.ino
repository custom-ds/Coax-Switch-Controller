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
#define DISPLAY_ADDRESS1 0x72 //This is the default address of the OpenLCD


//Global Variables
TWIST twist;
Preferences prefs;
AsyncWebServer server(80);

int radio1 = 0;
int radio2 = 1;
int radioSelected = 0;





// hw_timer_t *timer0 = NULL;
// void IRAM_ATTR onTimer0();

//Function Prototypes
void updateLCD();
void updateAntenna(int radio, int diff);
void connectAntenna(int radio, int antenna, bool incrOnCollision);
String getOrCreateAPIKey(bool force);
int getRandom(int max);
bool checkAPIKey(String key);

void notFound(AsyncWebServerRequest *request);
String getWebpage();






/******************************************************************************************/
void setup(void) {
  Wire.begin();
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
    radio1 = 1;
    radio2 = 2;
  }
  if (radio1 > 5 || radio2 > 5) {
    radio1 = 1;
    radio2 = 2;
  }
  if (radio1 == radio2) {
    radio1 = 1;
    radio2 = 2;
  }


  //Send the reset command to the display - this forces the cursor to return to the beginning of the display
  Wire.beginTransmission(DISPLAY_ADDRESS1);
  Wire.write('|'); //Put LCD into setting mode
  Wire.write('-'); //Send clear display command
  Wire.endTransmission();

  Wire.beginTransmission(DISPLAY_ADDRESS1);
  Wire.print("  2x6 Antenna       Selector");
  Wire.endTransmission();


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
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  //default the relays to whatever is currently set (from eeprom)
  connectAntenna(1, radio1, true);
  connectAntenna(2, radio2, true);
  

  if (twist.begin() == false) {
    Serial.println("Twist is not connected.");

  }


  // ******* Server Route Handlers *******

  // Server Route Handler: /
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getWebpage());
  });

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





  // Server Route Handler: Not Found
  server.onNotFound(notFound);



  server.begin();
  Serial.println("HTTP server started");

  //Get the API key. Generate a new one if necessary
  String apiKey = getOrCreateAPIKey(false);   //Don't force the creation of a new key if one already exists
  Serial.println("API Key: " + apiKey);

}
/******************************************************************************************/
void loop(void) {
  
  int diff;

  if (twist.isClicked()) {
    //the button was clicked
    Serial.println("Button Clicked");

    radioSelected++;
    if (radioSelected > 2) {
      radioSelected = 1;
    }
    updateLCD();
  }
  diff = twist.getDiff();
  if (diff != 0) {
    updateAntenna(radioSelected, diff);
  }
  

  delay(250);
}

/******************************************************************************************/
void updateLCD() {
  // Shows the currently selected antennas that are set up.

  Wire.beginTransmission(DISPLAY_ADDRESS1);
  Wire.write('|'); //Put LCD into setting mode
  Wire.write('-'); //Send clear display command
  Wire.endTransmission();

  Wire.beginTransmission(DISPLAY_ADDRESS1);
  if (radioSelected == 1) {
    Wire.print("1>>");
  } else {
    Wire.print("1: ");
  }
  Wire.print(coaxDescriptions[radio1]);

  if (radioSelected == 2) {
    Wire.print("2>>");
  } else {
    Wire.print("2: ");
  }
  Wire.print(coaxDescriptions[radio2]);
  Wire.endTransmission();
}
/******************************************************************************************/
void updateAntenna(int radio, int diff) {

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

    //Check to see if Radio 2 already has this antenna selected
    if (radio1 == radio2) {
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

    //Check to see if Radio 1 already has this antenna selected
    if (radio1 == radio2) {
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

  if (radio == 1) {
    radio1 = antenna;
  }
  if (radio == 2) {
    radio2 = antenna;
  }
  //TODO: Need to verify that the antenna being selected isn't already coupled to the other radio

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
String getOrCreateAPIKey(bool force) {
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

  String key = "";
  for (int i = 0; i < 14; i++) {
    key += possibleChars[getRandom(32)];
  }

  prefs.putString("apikey", key);   //save the key to eeprom

  return key;
}
/******************************************************************************************/
int getRandom(int max) {
  return (esp_random() % max);
}
/******************************************************************************************/
bool checkAPIKey(String key) {
  //Check the API key against the stored key from eeprom
  String storedKey = prefs.getString("apikey", "");
  if (key == storedKey) {
    return true;
  } 


  return false;
}
/******************************************************************************************/
void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}
/******************************************************************************************/
String getWebpage() {

  String html = R"(<!DOCTYPE html>
<html>

<head>
  <title>%TITLE%</title>
</head>

<body>
  <h1>%TITLE%</h1>

  <form id='relayForm'>
    <label for='systemon'>System On:</label>
    <button type='button' id='systemon' onclick='toggleSystem(true)'>System On</button><br>    
  
    <label for='systemoff'>System Off:</label>
    <button type='button' id='systemoff' onclick='toggleSystem(false)'>System Off</button><br>  

    <label for='relay1'>Relay 1 :: %RELAY1DESC%</label>
    <button type='button' id='relay1' onclick='toggleRelay(1)'>Toggle</button><br>

    <label for='relay2'>Relay 2 :: %RELAY2DESC%</label>
    <button type='button' id='relay2' onclick='toggleRelay(2)'>Toggle</button><br>

    <label for='relay3'>Relay 3 :: %RELAY3DESC%</label>
    <button type='button' id='relay3' onclick='toggleRelay(3)'>Toggle</button><br>

    <label for='relay4'>Relay 4 :: %RELAY4DESC%</label>
    <button type='button' id='relay4' onclick='toggleRelay(4)'>Toggle</button><br>

    <label for='relay5'>Relay 5 :: %RELAY5DESC%</label>
    <button type='button' id='relay5' onclick='toggleRelay(5)'>Toggle</button><br>


  </form>

  <script>
    async function toggleRelay(relayNumber) {
      try {
        var uri = '/api/relay/toggle?relay=' + relayNumber.toString();
        console.log(uri);

        const response = await fetch(uri, {
          method: 'POST'
        });

        if (response.ok) {
          const result = await response.text();
          //alert(result); // Display the result (e.g., Relay 1 is ON/OFF)
        } else {
          alert('Error toggling relay');
        }
      } catch (error) {
        console.error(error);
        alert('An error occurred');
      }
    }

    async function toggleSystem(systemOn) {
      try {
        var uri ='';
        if (systemOn) {
          uri = '/api/relay/system/on';
        } else {
          uri = '/api/relay/system/off';
        }
      
        console.log(uri);

        const response = await fetch(uri, {
          method: 'POST'
        });

        if (response.ok) {
          const result = await response.text();
          //alert(result); // Display the result (e.g., Relay 1 is ON/OFF)
        } else {
          alert('Error toggling relay');
        }
      } catch (error) {
        console.error(error);
        alert('An error occurred');
      }
    }

  </script>
</body>

</html>
)"; 

  // Replace placeholders with actual values
  html.replace("%TITLE%", "Antenna Switch");//"pageTitle");
  html.replace("%RELAY1DESC%", "relayDescriptions[0]");
  html.replace("%RELAY2DESC%", "relayDescriptions[1]");
  html.replace("%RELAY3DESC%", "relayDescriptions[2]");
  html.replace("%RELAY4DESC%", "relayDescriptions[3]");
  html.replace("%RELAY5DESC%", "relayDescriptions[4]");

  return html;



}