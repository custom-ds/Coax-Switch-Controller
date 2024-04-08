

#include "config.h"
/* Running on EPS32S3 Dev Module */

#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebSrv.h>
#include <Wire.h>
//include "SparkFun_Qwiic_Relay.h"


#include <SparkFun_Qwiic_Twist_Arduino_Library.h>
#include <SerLCD.h>

#include <Preferences.h>


TWIST twist;
//SerLCD lcd(0x72);

/*
 * Wifi credentials are stored in config.h file. See the template.
 *
const char* ssid = "";
const char* password = "";
*/
#define DISPLAY_ADDRESS1 0x72 //This is the default address of the OpenLCD

Preferences prefs;

AsyncWebServer server(80);

int radio1 = 0;
int radio2 = 1;
int radioSelected = 0;

// Relay descriptions are configured in the config.h file

//Relay Pin definitions
//21 and 22 are used for the SCL/SDA connector (i2c/Qwiic)
int radio1Antennas[] = {13, 32, 14, 15, 33, 35};
int radio2Antennas[] = {27, 12, 26, 25, 23, 18};
// int radio1Antennas[] = {13, 12, 27, 33, 15, 32};
// int radio2Antennas[] = {14, 14, 14, 14, 14, 14};


hw_timer_t *timer0 = NULL;

void IRAM_ATTR onTimer0();
void notFound(AsyncWebServerRequest *request);
// bool setRelay(int relay, bool status);
// bool getRelay(int relay);
void toggleSystem(bool status);


void lcdShowAntennas(int radioSelected) {
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

void updateAntenna(int radio, int diff) {

  //cap out the maximum clicks that can be made in a cycle
  if (diff > 3) diff=3;
  if (diff < -3) diff=-3;


  if (radio == 1) {
    radio1 = radio1 + diff;
    if (radio1 > 5) {
      radio1 = radio1 - 6;
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
        if (radio1 > 5) {
          radio1 = radio1 - 6;
        }
      } else {
        //clicking down
        radio1--;
        if (radio1 < 0) {
          radio1 = radio1 + 6;
        }
      }
    }
  } 

  if (radio == 2) {
    radio2 = radio2 + diff;
    if (radio2 > 5) {
      radio2 = radio2 - 6;
    } else {
      if (radio2 < 0) {
        radio2 = radio2 + 6;
      }
    }

    //Check to see if Radio 1 already has this antenna selected
    if (radio1 == radio2) {
      if (diff > 0) {
        //clicking up
        radio2++;
        if (radio2 > 5) {
          radio2 = radio2 - 6;
        }
      } else {
        //clicking down
        radio2--;
        if (radio2 < 0) {
          radio2 = radio2 + 6;
        }
      }
    }
  }

  //set the relays
  digitalWrite(13, LOW);

  if (radio == 1) {
    // for (int i=0;i<6;i++) {
    //   digitalWrite(radio1Antennas[i], LOW);
    // }
    // digitalWrite(radio1Antennas[radio1], HIGH);

switch (radio1) {
    case 0:
      digitalWrite(12, LOW);
      digitalWrite(27, LOW);
      digitalWrite(33, LOW);
      break;
    case 1:
      digitalWrite(12, HIGH);
      digitalWrite(27, LOW);
      digitalWrite(33, LOW);
      break;
    case 2:
      digitalWrite(12, LOW);
      digitalWrite(27, HIGH);
      digitalWrite(33, LOW);
      break;
    case 3:
      digitalWrite(12, HIGH);
      digitalWrite(27, HIGH);
      digitalWrite(33, LOW);
      break;
    case 4:
      digitalWrite(12, LOW);
      digitalWrite(27, LOW);
      digitalWrite(33, HIGH);
      break;
    case 5:
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
    // Serial.println("Going Low");
    // for (int i=0;i<6;i++) {
    //   Serial.println(i);
    //   digitalWrite(radio2Antennas[i], LOW);
    // }
    // Serial.println("Setting high");
    // digitalWrite(radio2Antennas[radio2], HIGH);

    

    switch (radio2) {
    case 0:
      digitalWrite(15, LOW);
      digitalWrite(32, LOW);
      digitalWrite(14, LOW);
      break;
    case 1:
      digitalWrite(15, HIGH);
      digitalWrite(32, LOW);
      digitalWrite(14, LOW);
      break;
    case 2:
      digitalWrite(15, LOW);
      digitalWrite(32, HIGH);
      digitalWrite(14, LOW);
      break;
    case 3:
      digitalWrite(15, HIGH);
      digitalWrite(32, HIGH);
      digitalWrite(14, LOW);
      break;
    case 4:
      digitalWrite(15, LOW);
      digitalWrite(32, LOW);
      digitalWrite(14, HIGH);
      break;
    case 5:
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


}


void setup(void) {
  Wire.begin();
  Serial.begin(115200);

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


  Serial.println("Init'ing the relay pins");
  for (int i=0;i<6;i++) {
    //Set the relay pins to output
    Serial.print(i);
    Serial.print(": ");
    Serial.println(radio1Antennas[i]);
    pinMode(radio1Antennas[i], OUTPUT);

    Serial.print(i);
    Serial.print(": ");
    Serial.println(radio2Antennas[i]);
    pinMode(radio2Antennas[i], OUTPUT);

    Serial.println("Setting low");
  }


  Serial.println("Done init'ing the relay pins");

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
  updateAntenna(1, 0);
  updateAntenna(2, 0);

  if (twist.begin() == false) {
    Serial.println("Twist is not connected.");

  }

  lcdShowAntennas(radioSelected);

}



void loop(void) {

  int diff;
  bool refreshDisplay = false;

  if (twist.isClicked()) {
    //the button was clicked
    Serial.println("Button Clicked");

    radioSelected++;
    if (radioSelected > 2) {
      radioSelected = 1;
    }

    refreshDisplay = true;
  }
  diff = twist.getDiff();
  if (diff != 0) {
    updateAntenna(radioSelected, diff);
    refreshDisplay = true;
  }
  
  

  if (refreshDisplay) {
    lcdShowAntennas(radioSelected);
  }

  delay(250);
}





/******************************************************************************************/
/******************************************************************************************/
