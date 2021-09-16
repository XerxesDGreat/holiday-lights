// There are four potential output channels for light strands on this board. On a Wemos D1 Mini, the
// following channels use the following pins:
//
// Channel 0 uses pin 1 in code, which is the pin labeled TX
// Channel 1 uses pin 14 in code, which is the pin labeled D5
// Channel 2 uses pin 12 in code, which is the pin labeled D6
// Channel 3 uses pin 13 in code, which is the pin labeled D7
//
// However, even with the ESP8266 board numbering, the various constants don't _actually_ work either
// so we'll be using the pin numbers in the application below


//************************************************************************************************************
// INCLUDES, LIBRARIES, TYPEDEFS
//************************************************************************************************************
//#define FASTLED_INTERRUPT_RETRY_COUNT 0
#define FASTLED_ALLOW_INTERRUPTS 0
#define FASTLED_ESP8266_RAW_PIN_ORDER

#include <FastLED.h>
#include <SimpleTimer.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "config.h"

struct pattern {
	char *name;
	void (*patternOp)();
	uint8_t fps; // 0 is "no delay; as many FPS as you can churn"
};

struct callbackFn {
	String topic;
	void (*callbackOp)(char *);
};


//************************************************************************************************************
// LED LAYOUT AND SETUP
// 
// Specifies various LED strands involved with which we'll initialize FastLED
//************************************************************************************************************
#define ROOFLINE_LEDS            279 // strip in room is 100, roofline is 279, 144 strip is 144
// 0 / 48 \ 98 L 100 - 158 L 159 - 279
//first floor is actually 279
//first floor layout
#define ROOFLINE_PIN             14
#define ROOFLINE_COLOR_ORDER     RGB //strip of 100 is BRG, roofline is RGB, 144 strip is GRB
#define ROOFLINE_LED_TYPE        WS2811 // outside is WS2811, 144 strip is WS2812B

//************************************************************************************************************
// CONFIGURATION
// 
// 
//************************************************************************************************************

// Wifi 
const char* ssid = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASS;
WiFiClient espClient;

// MQTT
const char* mqtt_server = SECRET_MQTT_HOST;
const int mqtt_port = SECRET_MQTT_PORT;
const char *mqtt_user = SECRET_MQTT_USER;
const char *mqtt_pass = SECRET_MQTT_PASS;
const char *mqtt_client_name = "xmasLightController"; // Client connections can't have the same connection name
PubSubClient client(espClient);

// Sensible defaults
const uint8_t DEFAULT_GLITTER_CHANCE = 100;
const uint8_t DEFAULT_FRAMES_PER_SECOND = 30; // (no limitation)

SimpleTimer timer;

CRGB firstFloor[ROOFLINE_LEDS];

// Globals
uint8_t mark = 0;
uint8_t gHue = 0;
uint8_t startPosition = 0;
int lastPosition = 1;
int lightning = 1;
int ledIndex = 0;

bool boot = true;

String effect = "None";
void (*currentPattern)();
bool showGlitter = false;
uint8_t glitterChance = DEFAULT_GLITTER_CHANCE;
bool showLightning = false;
bool audioEffects = false;
bool showLights = false;

// Frames per second, aka how many times per second to redraw the strand(s). Individual patterns will
// override this because it makes sense for them to
uint8_t framesPerSecond = DEFAULT_FRAMES_PER_SECOND;

// Relative speed adjustment. This can be controlled separately to the FPS in order to globally speed
// up or slow down the patterns. Unclear how this will be accomplished as yet as we want a fairly
// small and distinct number of steps like 1/4x, 1/2x, 1x, 2x, 4x
uint8_t framesPerSecondMultiplier = 1;


CRGB primaryColor = CRGB::Red;
CRGB secondaryColor = CRGB::Green;
CRGB tertiaryColor = CRGB::Blue;

static const uint8_t numColors = 3;
// this is _probably_ the way we want to deal with colors in the future
CRGB colorList[numColors] = {
	primaryColor,
	secondaryColor,
	tertiaryColor
};

byte brightness = 100;

const String BASE_TOPIC            = SECRET_MQTT_BASE_TOPIC;
const String ALL_RELEVANT_TOPICS   = BASE_TOPIC + "/#";
const String LED_POSITION_TOPIC    = BASE_TOPIC + "/ledPosition";
const String PATTERN_TOPIC         = BASE_TOPIC + "/pattern";
const String STATE_TOPIC           = BASE_TOPIC + "/state";
const String PRIMARY_COLOR_TOPIC   = BASE_TOPIC + "/color/primary";
const String SECONDARY_COLOR_TOPIC = BASE_TOPIC + "/color/secondary";
const String TERTIARY_COLOR_TOPIC  = BASE_TOPIC + "/color/tertiary";
const String POWER_TOPIC           = BASE_TOPIC + "/power";
const String BRIGHTNESS_TOPIC      = BASE_TOPIC + "/brightness";
const String GLITTER_TOPIC         = BASE_TOPIC + "/effects/glitter";
const String LIGHTNING_TOPIC       = BASE_TOPIC + "/effects/lightning";

// NOTE! When adding a new pattern, declare the function here
// forward declarations for the patterns because apparently it's not getting done automatically
void colorChase();
void colorGlitter();
void rgbCalibrate();
void singleRace();
void doubleCrash();
void rainbow();
void blockedColors();
void bpm();
void twinkle();
void cylon();
void spookyEyes();
void fire();
void alarm();
void ledLocator();

struct pattern patterns[] = {
	{"Color_Chase", colorChase, 1},
	{"Color_Glitter", colorGlitter, 100},
	{"RGB_Calibrate", rgbCalibrate, 1},
	{"Single_Race", singleRace, 0},
	{"Double_Crash", doubleCrash, 0},
	{"Rainbow", rainbow, 0},
	{"Blocked_Colors", blockedColors, 1},
	{"BPM", bpm, 0},
	{"Twinkle", twinkle, 0},
	{"Cylon", cylon, 0},
	{"Spooky_Eyes", spookyEyes, 0},
	{"Fire", fire, 0},
	{"Alarm", alarm, 2},
	{"LED_Locator", ledLocator, 60},
	{0, 0, 0}
};

// Did you declare the function above?

const char *ON_PAYLOAD = "ON";
const char *OFF_PAYLOAD = "OFF";

//************************************************************************************************************
// SYSTEM FUNCTIONS
// 
// Do system-level operations like setup, connections, etc.
//************************************************************************************************************

void setup_wifi() {
	// We start by connecting to a WiFi network
	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
}

void reconnect() {
	// Loop until we're reconnected
	int retries = 0;
	while (!client.connected()) {
		if (retries < 150) {
			Serial.print("Attempting MQTT connection...");
			// Attempt to connect
			if (client.connect(mqtt_client_name, mqtt_user, mqtt_pass)) {
				Serial.println("connected");
				// Once connected, publish an announcement...
				if (boot == true) {
					client.publish("checkIn/LightMCU", "Rebooted");
					boot = false;
				} else {
					client.publish("checkIn/LightMCU", "Reconnected");
				}
				// ... and resubscribe
				mqttSubscribe();
			} else {
				Serial.print("failed, rc=");
				Serial.print(client.state());
				Serial.println(" try again in 5 seconds");
				retries++;
				// Wait 5 seconds before retrying
				delay(5000);
			}
		} else {
			ESP.restart();
		}
	}
}

void mqttSubscribe() {
	client.subscribe(ALL_RELEVANT_TOPICS.c_str());
}

void sendStatusMessage(String message) {
	// perhaps the messages should be constants?
	client.publish("holidayLightStatus", message.c_str());
}

// What's this do?
void checkIn() {
	EVERY_N_SECONDS(30) {
		Serial.println("sending ok status");
		sendStatusMessage("OK");
	}
}

//************************************************************************************************************
// MQTT CALLBACKS
//
// Responsible for processing incoming messages on the various channels to which we're subscribed
//************************************************************************************************************

long hexCharsToLong (char* hexString) {
	return strtol(hexString, NULL, 16);
}

/**
 * Returns a boolean value calculated from the provided payload. If the payload does not match the defined
 * boolean state representations, returns the provided currentValue
 */
bool getBooleanFromCallbackPayload (char* payload, bool currentValue) {
	bool response = currentValue;
	if (strcmp(payload, ON_PAYLOAD) == 0) {
		response = true;
	} else if (strcmp(payload, OFF_PAYLOAD) == 0) {
		response = false;
	}
	return response;
}

int getIntFromCallbackPayload (char* payload) {
	String payloadStr = String(payload);
	return payloadStr.toInt();
}

// @todo maybe adding some messaging?
void callbackGlitter (char* payload) {
	showGlitter = getBooleanFromCallbackPayload(payload, showGlitter);
}

void callbackLightning (char* payload) {
	showLightning = getBooleanFromCallbackPayload(payload, showLightning);
}

void callbackPower (char* payload) {
	showLights = getBooleanFromCallbackPayload(payload, showLights);
	String statusMessage = showLights ? "REINITIALIZING" : "STATUS_MODE";
	sendStatusMessage(statusMessage);
}

void callbackBrightness (char* payload) {
	brightness = getIntFromCallbackPayload(payload);
}

void callbackLEDPosition (char* payload) {
	int p = String(payload).toInt();
	Serial.printf("lighting LED at position [%d]", p);
	ledIndex = p;
}

void callbackPrimaryColor (char* payload) {
	primaryColor = CRGB(hexCharsToLong(payload));
	colorList[0] = primaryColor;
}

void callbackSecondaryColor (char* payload) {
	secondaryColor = CRGB(hexCharsToLong(payload));
	colorList[1] = secondaryColor;
}

void callbackTertiaryColor (char* payload) {
	tertiaryColor = CRGB(hexCharsToLong(payload));
	colorList[2] = tertiaryColor;
}

void callbackPattern (char* payload) {
	struct pattern *scan;
	char *patternName;
	for (scan = patterns; scan->patternOp; scan++) {
		if (strcmp(payload, scan->name) == 0) {
			Serial.printf("setting new pattern [%s]\n", payload);
			currentPattern = scan->patternOp;
			effect = scan->name;
			framesPerSecond = scan->fps == 0 ? DEFAULT_FRAMES_PER_SECOND : scan->fps;
			return;
		}
	}
	
	Serial.printf("no pattern found called %s\n", payload);
}

struct callbackFn callbackRegistry[] = {
	{LED_POSITION_TOPIC, callbackLEDPosition},
	{PATTERN_TOPIC, callbackPattern},
	{PRIMARY_COLOR_TOPIC, callbackPrimaryColor},
	{SECONDARY_COLOR_TOPIC, callbackSecondaryColor},
	{TERTIARY_COLOR_TOPIC, callbackTertiaryColor},
	{POWER_TOPIC, callbackPower},
	{BRIGHTNESS_TOPIC, callbackBrightness},
	{GLITTER_TOPIC, callbackGlitter},
	{LIGHTNING_TOPIC, callbackLightning},
};

void handleMQTTMessage (char* topic, byte* payloadBytes, unsigned int length) {
	// add a management mode?

	char *payload = (char *) payloadBytes;
	payload[length] = '\0';
	
	Serial.printf("Message arrived on topic [%s] with payload [%s]\n", topic, payload);
	Serial.println();

	struct callbackFn *scan;
	for (scan = callbackRegistry; scan->callbackOp; scan++) {
		if (scan->topic == topic) {
			scan->callbackOp(payload);
			return;
		}
	}

	// No callback was found in the registry
	Serial.printf("not subscribed to %s topic\n", topic);
}

//************************************************************************************************************
// PATTERN MANAGEMENT
//
// High-level pattern management for lights
//************************************************************************************************************

void runPattern() {
	if (showLights == false) { // not using ! in order to make this more obvious
		// this is not power efficient as it just makes the LEDS show no color; it _doesn't_
		// mean there is _actually_ no power going to the thing
		solidColor(CRGB::Black);
		FastLED.show();
		return;
	}
	
	FastLED.setBrightness(brightness);

	// lights are being shown
	currentPattern();
	
	FastLED.show();
	doFramesPerSecondDelay();
	
	EVERY_N_MILLISECONDS( 20 ) {
		gHue++;
	}
}

void addEffects () {
	addGlitter(100);
	addLightning();
}

void doFramesPerSecondDelay () {
	if (framesPerSecond > 0) {
		uint16_t delayMS = 1000 / framesPerSecond;
		// apply multiplier
		delay(delayMS);
	}
}

/*********************** PATTERN MODIFIERS ***************************/
/*********************** PATTERN MODIFIERS ***************************/
/*********************** PATTERN MODIFIERS ***************************/
/*********************** PATTERN MODIFIERS ***************************/
/*********************** PATTERN MODIFIERS ***************************/

void addLightning()
{
	if (showLightning == true)
	{
		unsigned int chance = random8();
		if ( chance == 255)
		{
			//wreathPumpkinLightning();
			fill_solid(firstFloor, ROOFLINE_LEDS, CRGB::White);
			//fill_solid(secondFloor, SECONDFLOOR_LEDS, CRGB::White);
			lightning = 20;
			//      if(audioEffects == true)
			//      {
			//        int thunder = random8();
			//        if( thunder > 128)
			//        {
			//          client.publish("Audio","2");
			//        }
			//        if( thunder < 127)
			//        {
			//          client.publish("Audio","1");
			//        }
			//      }
		}
		if (lightning != 1)
		{
			if (lightning > 15)
			{
				fadeToBlackBy( firstFloor, ROOFLINE_LEDS, 150);
				//        fadeToBlackBy( secondFloor, SECONDFLOOR_LEDS, 150);
				//        fadeToBlackBy( wreath, WREATH_LEDS, 150);
				lightning--;
			}
			if (lightning == 15)
			{
				//wreathPumpkinLightning();
				fill_solid(firstFloor, ROOFLINE_LEDS, CRGB::White);
				//fill_solid(secondFloor, SECONDFLOOR_LEDS, CRGB::White);
				lightning--;
			}
			if (lightning > 5 && lightning < 15)
			{
				fadeToBlackBy( firstFloor, ROOFLINE_LEDS, 150);
				//        fadeToBlackBy( secondFloor, SECONDFLOOR_LEDS, 150);
				//        fadeToBlackBy( wreath, WREATH_LEDS, 150);
				lightning--;
			}
			if (lightning == 5)
			{
				//wreathPumpkinLightning();
				fill_solid(firstFloor, ROOFLINE_LEDS, CRGB::White);
				//fill_solid(secondFloor, SECONDFLOOR_LEDS, CRGB::White);
				lightning--;
			}
			if (lightning > 0 && lightning < 5)
			{
				fadeToBlackBy( firstFloor, ROOFLINE_LEDS, 150);
				//        fadeToBlackBy( secondFloor, SECONDFLOOR_LEDS, 150);
				//        fadeToBlackBy( wreath, WREATH_LEDS, 150);
				lightning--;
			}
		}
	}
}

void addGlitter( fract8 chanceOfGlitter)
{
	if (showGlitter == true)
	{
		if ( random8() < chanceOfGlitter)
		{
			firstFloor[ random16(ROOFLINE_LEDS) ] += CRGB::White;
		}
		//    if( random8() < chanceOfGlitter)
		//    {
		//      secondFloor[ random16(SECONDFLOOR_LEDS) ] += CRGB::White;
		//    }
		//    if( random8() < chanceOfGlitter)
		//    {
		//      wreath[ random16(WREATH_LEDS) ] += CRGB::White;
		//    }
		//    if( random8() < chanceOfGlitter)
		//    {
		//      candyCanes[ random16(CANDYCANE_LEDS) ] += CRGB::White;
		//    }
	}
}



/*****************  SETUP FUNCTION  ****************************************/
/*****************  SETUP FUNCTION  ****************************************/
/*****************  SETUP FUNCTION  ****************************************/
/*****************  SETUP FUNCTION  ****************************************/
/*****************  SETUP FUNCTION  ****************************************/


void setup() {
	Serial.begin(115200);
	while (!Serial) {};

	// @todo create a strand config struct which contains these things.
	FastLED.addLeds<ROOFLINE_LED_TYPE, ROOFLINE_PIN, ROOFLINE_COLOR_ORDER>(firstFloor, ROOFLINE_LEDS);
	// add more LEDs here

	WiFi.setSleepMode(WIFI_NONE_SLEEP);
	WiFi.mode(WIFI_STA);
	setup_wifi();
	client.setServer(mqtt_server, mqtt_port);
	client.setCallback(handleMQTTMessage);

	setupOTA();

	pinMode(D3, OUTPUT);
	pinMode(LED_BUILTIN, OUTPUT);

	currentPattern = colorChase;
}

void setupOTA() {
	// @todo report state of update
	// ArduinoOTA = Over The Air for updates
	ArduinoOTA.setHostname(SECRET_OWA_HOSTNAME);
	ArduinoOTA.setPassword(SECRET_OWA_PASSWORD);
	ArduinoOTA.onStart([]() {
		Serial.println("Starting OTA update");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnding OTA update");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});
	ArduinoOTA.begin();
	Serial.println("OTA READY");
}

/*****************  MAIN LOOP  ****************************************/
/*****************  MAIN LOOP  ****************************************/
/*****************  MAIN LOOP  ****************************************/
/*****************  MAIN LOOP  ****************************************/
/*****************  MAIN LOOP  ****************************************/

void loop()
{
	if (!client.connected()) {
		reconnect();
	}
	client.loop();
	
	checkIn();

	if (showLights == false) {
		ArduinoOTA.handle();
	}
	
	// we don't _really_ need timer.... do we?
	// @todo update, no we don't. We'll use FPS directly here
	timer.run();

	EVERY_N_MILLISECONDS (10000) {
		toggleLED();
	}

	runPattern();
}

bool showLED;
void toggleLED () {
	showLED = !showLED;
	int state = showLED ? HIGH : LOW;
	digitalWrite(LED_BUILTIN, state);
}



//************************************************************************************************************
// PATTERN DEFINITIONS
//
// This is where we actually define how the patterns will be rendered.
// 
// @todo for all of these, we should find a way to parameterize the led string and led count
// Note: For each pattern, we will likely want to have two different functions. The first is 
// the "public" name of the function (e.g. "colorChase") (but what does public mean in this
// context anyway). The second will be the public name prepended with an underscore
// (e.g. "_colorChase"). The reason we have both is so we can reuse the brains of the function
// with additional strands of lights.
//************************************************************************************************************

// solidColor: it's a solid color. No mystery
void solidColor (CRGB color) {
	_solidColor(color, firstFloor, ROOFLINE_LEDS);
}

void _solidColor (CRGB color, CRGB *lightStrand, int numLEDs) {
	fill_solid(lightStrand, numLEDs, color);
}

// colorChase: alternating the primary, secondary, and tertiary colors with black:
// ... - P - B - S - B - T - B - P - B - S - B - T - B - ...
// The colors then advance by one pixel each tick.

void colorChase() {
	_colorChase(firstFloor, ROOFLINE_LEDS);
	// this only exists to potentially expand the same pattern to additional strands
}

int colorChaseOffset = 0;
const int colorChase_numColorsInIteration = 6;
void _colorChase(CRGB *lightStrand, int numLEDs) {
	// we have 6 potential offsets, and 3 potential colors. Since we show the colors
	// in the order color - black - color - black ..., if the offset is 1, 3, 5, we
	// should show black. After we show black, we increment to the next color. So, it's
	// something like "red-no-black, red-show-black, green-no-black, green-show-black"...
	bool showBlack = colorChaseOffset % 2 == 1;
	int currentColorOffset = (int) colorChaseOffset / 2;
	for (int i = 0; i < numLEDs; i ++) {
		CRGB color;
		if (showBlack) {
			color = CRGB::Black;
			currentColorOffset = (currentColorOffset + 1) % 3; // @todo constant for 3?
		} else {
			color = colorList[currentColorOffset];
		}
		lightStrand[i] = color;
		showBlack = !showBlack;
	}
	colorChaseOffset = (colorChaseOffset + 1 ) % colorChase_numColorsInIteration;
}



// colorGlitter: Twinkling all the lights randomly whilst rotating through the six
// main rainbow colors

const fract8 defaultChanceOfGlitter = 255;
void colorGlitter () {
	_colorGlitter(firstFloor, ROOFLINE_LEDS, defaultChanceOfGlitter);
}

const CRGB glitterColors[6] = {
	CRGB::Red,
	CRGB(173, 78, 27),
	CRGB::Yellow,
	CRGB::Green,
	CRGB::Blue,
	CRGB(91,44,111)
};

// You may be asking yourself "what the fuck is fract8"? Put.... moderately simply,
// fract8 is the numerator in the fraction x / 256; a value of 64 is equivalent to
// 64 / 256 or one quarter. This is useful for chance calculations. The maximum
// allowed value is 255 because _really_ fract8 is a uint8 underneath; also, since
// fract8 is not _actually_ useful beyond the fractional operations in FastLED, it's
// kinda useless. However, its intention is good, so we'll keep it.
void _colorGlitter (CRGB *lightStrand, int numLEDs, fract8 chanceOfGlitter) {
	fadeToBlackBy(lightStrand, numLEDs, 2);
	if (random8() < chanceOfGlitter) {
		int glitterColorIdx = startPosition > 5 ? 5 : startPosition;
		lightStrand[random16(numLEDs)] += glitterColors[glitterColorIdx];
	}
}



// rgbCalibrate:  Assist in determining the color order of an RGB strand. According to FastLED
// (from whence I stole this):
// 
// You should see six leds on.  If the RGB ordering is correct, you should see 1 red led, 2 green 
// leds, and 3 blue leds.  If you see different colors, the count of each color tells you what the 
// position for that color in the rgb orering should be.  So, for example, if you see 1 Blue, and 2
// Red, and 3 Green leds then the rgb ordering should be BRG (Blue, Red, Green).  

// You can then test this ordering by setting the RGB ordering in the addLeds line below to the new ordering
// and it should come out correctly, 1 red, 2 green, and 3 blue.

void rgbCalibrate() {
	_rgbCalibrate(firstFloor);
}

void _rgbCalibrate(CRGB *lightStrand) {
	lightStrand[0] = CRGB(255, 0, 0);
	// skip 1
	lightStrand[2] = CRGB(0, 255, 0);
	lightStrand[3] = CRGB(0, 255, 0);
	// skip 4
	lightStrand[5] = CRGB(0, 0, 255);
	lightStrand[6] = CRGB(0, 0, 255);
	lightStrand[7] = CRGB(0, 0, 255);
}



// singleRace: Primary and Secondary colors alternate in blocks across the length of
// the strand in a flowy fashion

void singleRace() {
	_singleRace(firstFloor, ROOFLINE_LEDS);
}

uint8_t singleRaceStartingPosition = 0;
const accum88 singleRace_beatsPerMinute = 5;
uint8_t singleRace_colorOffset = 0;
int previousPos = -1;
void _singleRace(CRGB *lightStrand, int numLEDs) {
	fadeToBlackBy(lightStrand, numLEDs, 10);
	int pos = beatsin16(singleRace_beatsPerMinute, 0, numLEDs);
	if (pos == 0 || pos == (numLEDs - 1)) {
		Serial.println("at an end");
	}
	if ((pos > previousPos && previousPos == 0) || (pos < previousPos && previousPos == (numLEDs))) {
		// since we'll have many iterations where we are at one of the ends and no good way to say "I'm moving
		// away from the end next turn", we have to change the color directly after we have moved away from the
		// end. The logic above does that
		singleRace_colorOffset = (++singleRace_colorOffset) % numColors;
		Serial.println("changing color");
	}
	firstFloor[pos] = colorList[singleRace_colorOffset];
	previousPos = pos;
}



// doubleCrash: Primary and Secondary colors start at relevant points in the strand, 
// crossing over each other to reach the other endpoint in the strand section

// This will be a bit harder to parameterize, so we're just going to do some simple stuff
// and, should we need it, fix it up later
void doubleCrash() {
	fadeToBlackBy(firstFloor, ROOFLINE_LEDS, 15);
	// For this we'll have four equal sections
	// We need a separate `pos` var for every differently-sized segment
	int pos1 = beatsin8(16, 0, 48);
	firstFloor[0 + pos1] = primaryColor;
	firstFloor[48 - pos1] = secondaryColor;
	int pos2 = beatsin8(16, 0, 49);
	firstFloor[49 + pos2] = secondaryColor;
	firstFloor[98 - pos2] = primaryColor;
	int pos3 = beatsin8(16, 0, 58);
	firstFloor[100 + pos3] = primaryColor;
	firstFloor[158 - pos3] = secondaryColor;
	int pos4 = beatsin8(16, 0, 120);
	firstFloor[159 + pos4] = secondaryColor;
	firstFloor[279 - pos4] = primaryColor;
}

// rainbow: Gradually changing rainbow

void rainbow() {
	_rainbow(firstFloor, ROOFLINE_LEDS);
}

void _rainbow (CRGB *lightStrand, int numLEDs) {
	fill_rainbow(lightStrand, numLEDs, gHue, 7);
}



// blockedColorsAlternating: blocks of 9 pixels in alternating colors separated
// by a single black pixel. This is very similar to colorChase, only instead of
// being one pixel per color it's 9 pixels, and the black pixels don't move
void blockedColors() {
	_blockedColors(firstFloor, ROOFLINE_LEDS);
}

int defaultBlockSize = 9;
void _blockedColors (CRGB *lightStrand, int numLEDs) {
	_blockedColors(lightStrand, numLEDs, defaultBlockSize);
}

uint8_t blockedColors_colorIdx = 0;
void _blockedColors (CRGB *lightStrand, int numLEDs, int blockSize) {
	int blockPlusSpaceSize = blockSize + 1; // if the block size is 9, every 10th pixels is black
	int iterationSize = blockPlusSpaceSize * 3;

	// this should be made global as a constant or static
	CRGB colors[] = {
		primaryColor,
		secondaryColor,
		tertiaryColor
	};
	
	fadeToBlackBy( lightStrand, numLEDs, 2);
    
	for ( int mark = 0; mark < numLEDs; mark += iterationSize) {
		for ( int i = 0; i < blockSize; i++) {
			lightStrand[i + mark] = colorList[blockedColors_colorIdx];
			lightStrand[i + blockPlusSpaceSize + mark] = colorList[(blockedColors_colorIdx + 1) % 3];
			lightStrand[i + (blockPlusSpaceSize * 2) + mark] = colorList[(blockedColors_colorIdx + 2) % 3];
		}
	}

	// we'll do a low FPS; that will make the colors move slowly
	blockedColors_colorIdx = (blockedColors_colorIdx + 1) % numColors;
}



// bpm: combination of a rotating rainbow and a bouncing/oscillating brightness application.
// When looking at this effect, there is a definite bright to dark sawtooth going on, with a
// gradual drop off, then a sudden increase in brightness. This slides back and forth at the
// frequency defined in _bpm. That is a decorator to the underlying foundational color which
// is a standard revolving rainbow.

void bpm () {
	_bpm(firstFloor, ROOFLINE_LEDS);
}

void _bpm (CRGB *lightStrand, int numLEDs) {
	uint8_t beatsPerMinute = 62; // different to refresh rate
	uint8_t lowestHue = 0;
	uint8_t highestHue = 255;
	CRGBPalette16 palette = PartyColors_p;
	uint8_t beat = beatsin8( beatsPerMinute, lowestHue, highestHue);
	for ( int i = 0; i < numLEDs; i++)
	{
		lightStrand[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
	}
}



// twinkle: every fifth led sparkles

void twinkle () {
	_twinkle(firstFloor, ROOFLINE_LEDS);
}

const CRGB twinkle_brightColor = CRGB::White;
const CRGB twinkle_dimColor = CRGB(150, 100, 40);
void _twinkle (CRGB *lightStrand, int numLEDs) {
	fadeToBlackBy( lightStrand, numLEDs, 80);
	for ( int i = 0; i < numLEDs - 5; i += 5) {
		lightStrand[i] = random8() > 250 ? twinkle_brightColor : twinkle_dimColor;
	}
	delay(40);
}



// cylon: sweeping rainbow worm across the strand

void cylon () {
	_cylon(firstFloor, ROOFLINE_LEDS);
}

void fadeall(CRGB *lightStrand, int numLEDs) {
	for (int i = 0; i < numLEDs; i++) {
		lightStrand[i].nscale8(250);
	}
}

void _cylon(CRGB *lightStrand, int numLEDs) {
	static uint8_t hue = 0;
	for (int i = 0; i < numLEDs; i ++) {
		lightStrand[i] = CHSV(hue++, 255, 255);
		FastLED.show();
		fadeall(lightStrand, numLEDs);
		delay(10);
	}

	for (int i = (numLEDs - 1); i >= 0; i --) {
		lightStrand[i] = CHSV(hue++, 255, 255);
		FastLED.show();
		fadeall(lightStrand, numLEDs);
		delay(10);
	}
}



// spookyEyes: make what looks like red eyes appear in random spots/

void spookyEyes () {
	_spookyEyes(firstFloor, ROOFLINE_LEDS);
}

// @todo, maybe make it so there are only x number of eyes on at any given time, then fade
// those after a given amount of time
void _spookyEyes (CRGB *lightStrand, int numLEDs) {
	unsigned int chance = random8();
	if ( chance > 248) {
		unsigned int eye = random16(numLEDs);
		unsigned int secondEye = eye > (numLEDs - 4) ? eye - 4 : eye + 4;
		lightStrand[eye] = CRGB::Red;
		lightStrand[secondEye] = CRGB::Red;
	}
	if ( chance > 39) {
		fadeToBlackBy( lightStrand, numLEDs, 10);
	}
}



// There used to be some patterns here called "wreathPumpkin", "candyCaneEachBounce",
// and "wreathCrazy". As I don't have a wreath or candy canes, it's just clutter code.
// To restore the original patterns, find them here:
// https://github.com/thehookup/Holiday_LEDS/blob/master/LightsMCU_Customize_CONFIGURE.ino



// alarm: blink all LEDs red

void alarm () {
	_alarm(firstFloor, ROOFLINE_LEDS);
}

bool alarm_colorOn = true;
void _alarm (CRGB *lightStrand, int numLEDs) {
	CRGB color = alarm_colorOn ? CRGB::Red : CRGB:: Black;
	fill_solid(lightStrand, numLEDs, color);
	alarm_colorOn = !alarm_colorOn;
}



// ledLocator: Light up one LED at a time in orer to find which numbers specific
// LEDs are in the installation

void ledLocator () {
	_ledLocator(firstFloor, ROOFLINE_LEDS);
}

void _ledLocator (CRGB *lightStrand, int numLEDs) {
	fadeToBlackBy(lightStrand, numLEDs, 64);
	if (ledIndex < numLEDs) {
		lightStrand[ledIndex] = primaryColor;
	}
}



// fire: makes an effect that looks like fire. Each strand is divided up into
// sections going between nodes. Best way to visualize the nodes is corners on
// a roofline; each corner is a node. The fire effect spreads from the corner
// onto each strand of lights which is connected at that node

void fire () {
	_fireFirstFloor();
}

void _fireFirstFloor () {
	// need to define where the fire is coming from and which direction it is
	// going for each node in the strand
	// @todo there's gotta be a better way to do this. But, this _already_
	// is better than before, so...
	_fire(firstFloor, 0, true, 47);
	_fire(firstFloor, 98, false, 49);
	_fire(firstFloor, 100, true, 57);
	_fire(firstFloor, 158, false, 57);
	_fire(firstFloor, 159, true, 100); // actually > 100, but this is fine
	_fire(firstFloor, 279, false, 100);
}


const CRGBPalette16 HEAT_PALETTE = HeatColors_p;
int SPARKING = 85;
int COOLING =  120;
const int NUM_FIRE_LEDS = 100;
void _fire (CRGB *lightStrand, int nodeIdx, bool shouldUseAddForSpread, int segmentSize) {
	// @todo based on what we see in this algorithm, we probably want to build
	// in some concept of segments for the roofline where we have a starting
	// point, an endpoint, a length, and a "verticality" or something; while
	// other patterns can make use of the idea of nodes and which way the nodes
	// are going (implying segments), fire needs to also spread up, not down.
	// For example, going up the sides of a peaked roof, but not from the
	// peak downward.
	
	// Array of temperature readings at each simulation cell
	// note: NUM_LEDS is a very large number, bigger than our segments. It _might_ work
	static byte heat[NUM_FIRE_LEDS];

	// Step 1.  Cool down every cell a little
	for ( int i = 0; i < NUM_FIRE_LEDS; i++) {
		heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_FIRE_LEDS) + 2));
	}

	// Step 2.  Heat from each cell drifts 'up' and diffuses a little
	for ( int k = NUM_FIRE_LEDS - 1; k >= 2; k--) {
		heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
	}

	// Step 3.  Randomly ignite new 'sparks' of heat near the bottom
	if ( random8() < SPARKING ) {
		int y = random8(7);
		heat[y] = qadd8( heat[y], random8(160, 255) );
	}

	// Step 4.  Map from heat cells to LED colors
	for ( int j = 0; j < NUM_FIRE_LEDS; j++) {
		// Scale the heat value from 0-255 down to 0-240
		// for best results with color palettes.
		byte colorindex = scale8( heat[j], 240);
		CRGB color = ColorFromPalette(HEAT_PALETTE, colorindex);
		int pixelnumber = j;
		// segment size is the entire length of the segment. We want fire coming from
		// nodes to not fuck with each other, so we'll make sure the fire only goes
		// halfway
		int flat = map(pixelnumber, 0, NUM_FIRE_LEDS, 0, (int)(segmentSize / 2));
		int idx = shouldUseAddForSpread ? nodeIdx + flat : nodeIdx - flat;
		if (idx < 0 || idx > ROOFLINE_LEDS) {
			Serial.printf("going to set idx %d\n", idx);
		}
		
		lightStrand[idx] = color;
	}
}
