# Holiday Lights for Arduino
This is an application which drives various different patterns on a set of LED strands. For control, the application subscribes to a series of MQTT channels, looking for messages to update the patterns, colors, add effects, etc.

## Installation

### ESP8266
This is designed to use an ESP8266 board; development was done using a Wemos D1 Mini. As such, you will have to install the ESP8266 board manager. Instructions can be found here: https://github.com/esp8266/Arduino

### Library Requirements
These are the versions of the libraries which were used during development of this application. You will likely have to install most of them yourself. Fortunately, the Arduino IDE is super helpful at this for most of it.

| Library Name             | Version   | How to install                                          |
| ------------------------ | --------- | ------------------------------------------------------- |
| FastLED                  | 3.3.3     | Arduino IDE Library Manager                             |
| SimpleTimer              |           | Instructions found at https://playground.arduino.cc/Code/SimpleTimer/ |
| PubSubClient             | 2.8.0     | Arduino IDE Library Manager                             | 

### Configuration file
You will have to create your own secrets file to populate your MQTT server and your wifi credentials. To do so, copy
`holiday-lights/config.h.tmpl` and paste it, renaming it to `config.h`. Open this file and replace the values for the settings defined within.

## Operation

### Communications
This application is designed to consume from MQTT topics in order to control the lights. Specifically, this was designed to be used with a dashboard built in [Node-RED](https://nodered.org/) and hosted in [Home Assistant](https://www.home-assistant.io/). 

### Base MQTT Topic

The default base topic is named `holidayLights`. This can be changed in the configuration file. This application will subscribe to all MQTT channels underneath this one.

### MQTT Topics

By default, this application will listen on the following MQTT topics (note that each of them will be prepended with the base MQTT topic identified above)

| Topic             | Type      | Purpose                                                         |
| ----------------- | --------- | --------------------------------------------------------------- |
| brightness        | int       | how bright the lights should be (0-255)                         |
| color/primary     | string    | hex representation of the primary RGB color (e.g. FF00cc)       |
| color/secondary   | string    | hex representation of the secondary RGB color (e.g. FF00cc)     |
| color/tertiary    | string    | hex representation of the tertiary RGB color (e.g. FF00cc)      |
| effects/glitter   | ON or OFF | whether or not to show glitter                                  |
| effects/lightning | ON or OFF | whether or not to show lightning                                |
| ledPosition       | int       | index of the LED(s) to light up in the ledLocator pattern       |
| pattern           | string    | which of the patterns listed under Patterns to display          |
| power             | ON or OFF | whether to display the lights (ON) or not (OFF)                 |

### Patterns

The following patterns are supported. Note that these are the values which should be used in messages published to the `pattern` MQTT topic above

* `Color_Chase`
* `Color_Glitter`
* `RGB_Calibrate`
* `Single_Race`
* `Double_Crash`
* `Rainbow`
* `Blocked_Colors`
* `BPM`
* `Twinkle`
* `Cylon`
* `Spooky_Eyes`
* `Fire`
* `Alarm`
* `LED_Locator`

## Credits
This application was heavily inspired by [The Hook Up's Holiday LEDs code](https://github.com/thehookup/Holiday_LEDS); the major changes involve better generalizations and a few additional features.
