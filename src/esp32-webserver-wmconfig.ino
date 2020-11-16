/*************************************************************************************************************************
* File name: esp32-webserver-wmconfig.ino
* Auther: AprialBrother  Version: V1.0   Data: 2019-10-22
* Description: The program is used to drive ESP32-button to connect to WiFi,
connect to HTTP server or MQTT server,send URL Request or send message to MQTT server
ther ESP32-button enters deep-sleep state.
* Hardware features:
 1.LED function: The power on red light flashes 3 times.
                 After connecting WiFi green light flashes 3 times.
                 After connecting server blue light flashes 3 times.
                 when successful sending data to server green light flashes once.
                 when the ESP32-button wakes up,if the network is disconnected,the red light will flash
                 continuously 10 times and then enter the deep-sleep state.
 2.Button function: short press --- wake up ESP32-button
                    long press(more 4S) ---- restore factory setting

* The specific config process:
   The first time power on, the red light flashes 3 times into the web page configuration mode.
   The ESP32-button will issue a hotspot named ESP-config to connect using the mobile phone or computer,
   password is 12345678.
   Enter 192.168.4.1 in the address bar to enter the parameter configuration page,you can configure
   parameters such as wifi ssid, password, http url, mqtt hostname, mqtt port, mqtt usernaem, mqtt password,etc.
   when HTTP and MQTT are both configured,the default connection is only HTTP.
  
   After configuring the parameters and correct,automatically connect to WiFi,connect to the server and send the message.

* Error handling
   1.when the WiFi parameter is incorrect, the factory setting will be automatically restored and 
   reconfigured in web page.
   2.when the server parameter is incorrect,you need to press the button more than 3S to restore the factory settings.

*************************************************************************************************************************/

/* Header Files */
#include <FS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include "config.h"
#include "strings_en.h"
#include <PubSubClient.h>
#include <FastLED.h>

#include <driver/rtc_io.h>
#include "driver/adc.h"
#include <Ticker.h>
#include "esp_bt_main.h"
#include "esp_bt.h"
#include "esp_wifi.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>

#ifdef ESP32
  #include <SPIFFS.h>
#endif

#include <WiFiManager.h>

/* flags */
bool shouldSaveConfig = false;
bool wifi_connected = false;
bool mqtt_connected = false;

/* wifi  ssid and password */
char app_ssid[SSID_LEN];
char app_password[PASD_LEN];

/* http url */
char http_url[URL_LEN];

/* mqtt client */
char mqtt_host[HOST_LEN];
char mqtt_port[PORT_LEN];
char mqtt_username[MQTTUSRN_LEN];
char mqtt_password[MQTTPASS_LEN];

/* ESPID MAC */
#define BD_ADDR_HEX(addr)   addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]
char clientId[32];
char mac_str[20];

uint8_t chip_mac[6];
uint64_t chipid;

/* class */
WiFiManager wm; // global wm instance
WiFiClient wificlient;
PubSubClient mqttclient(wificlient);

WebServer server(80);

/* the array of leds */
CRGB leds[NUM_LEDS];

/* the button gpio_pin */
#define BUTTON_GPIO  GPIO_NUM_25
#define BUTTON_GPIO_BITMASK 0x2000000
//const uint64_t BUTTON_GPIO_BITMASK = (1ULL < BUTTON_GPIO);

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int wmconfig_flag = 0;

/************************************
 * Function: saveConfigCallback()
 * Description: set the wifimanager configuration parameter flag
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void saveConfigCallback()
{
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

/************************************
 * Function: setupSpiffs()
 * Description: start SPIFFS
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/

void setupSpiffs(){
    //clean FS, for testing
    //SPIFFS.format();

    //read configuration from FS json
    Serial.println("mounting FS...");

    if (SPIFFS.begin()) {
        Serial.println("mounted file system");

    } else {
        Serial.println("failed to mount FS");
    }
    //end read
}

/************************************
 * Function: readConfigFile()
 * Description: read the contents of config.json in the file system
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void readConfigFile()
{
    if (!SPIFFS.exists("/config.json")) {
        Serial.println("There is not config.json File!");
        return ;     
    } else {
        //file exists, reading and loading
        Serial.println("reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
            Serial.println("opened config file");
            size_t size = configFile.size();
            // Allocate a buffer to store contents of the file.
            std::unique_ptr<char[]> buf(new char[size]);

            configFile.readBytes(buf.get(), size);
            DynamicJsonBuffer jsonBuffer;
            //StaticJsonBuffer jsonBuffer;
            JsonObject& json = jsonBuffer.parseObject(buf.get());
            json.printTo(Serial);
            if (json.success()) {
                Serial.println("\nparsed json");
                //strcpy(app_ssid,json["ssid"]);
                //strcpy(app_password,json["password"]);
            } else {
                Serial.println("failed to load json config");
            }
        }
        configFile.close();
    }
}

/************************************
 * Function: readWiFiDataFromFFS()
 * Description: read the wifi parameters from config.json in the file system
 *              into the program
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void readWiFiDataFromFFS()
{
    Serial.println("Start Read WiFi Data!");
    if(!SPIFFS.exists("/config.json")){
        Serial.println("There is not config.json File!");
        return ;
    } else {
        //file exists, reading and loading
        Serial.println("reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
            Serial.println("opened config file");
            size_t size = configFile.size();
            // Allocate a buffer to store contents of the file.
            std::unique_ptr<char[]> buf(new char[size]);

            configFile.readBytes(buf.get(), size);
            DynamicJsonBuffer jsonBuffer;
            //StaticJsonBuffer jsonBuffer;
            JsonObject& json = jsonBuffer.parseObject(buf.get());
            json.printTo(Serial);
            if (json.success()) {
                Serial.println("\nparsed json");
                strcpy(app_ssid,json["ssid"]);
                strcpy(app_password,json["password"]);
            } else {
                Serial.println("failed to load json config");
            }
        }
        configFile.close();
    }
}

/************************************
 * Function: readHttpDataFromFFS()
 * Description: read the HTTP URL parameter from config.json in the file system
 *              into the program
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void readHttpDataFromFFS()
{
    Serial.println("Start Read Http Data!");
    if(!SPIFFS.exists("/config.json")){
        Serial.println("There is not config.json File!");
        return ;
    } else {
        //file exists, reading and loading
        Serial.println("reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
            Serial.println("opened config file");
            size_t size = configFile.size();
            // Allocate a buffer to store contents of the file.
            std::unique_ptr<char[]> buf(new char[size]);

            configFile.readBytes(buf.get(), size);
            DynamicJsonBuffer jsonBuffer;
            //StaticJsonBuffer jsonBuffer;
            JsonObject& json = jsonBuffer.parseObject(buf.get());
            json.printTo(Serial);
            if (json.success()) {
                Serial.println("\nparsed json");
                strcpy(http_url,json["http_url"]);

            } else {
                Serial.println("failed to load json config");
            }
        }
        configFile.close();
    }
}

/************************************
 * Function: readMqttDataFromFFS()
 * Description: read the MQTT's parameters from config.json in the file system
 *              into the program
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void readMqttDataFromFFS()
{
    Serial.println("Start Read Mqtt Data!");
    if(!SPIFFS.exists("/config.json")){
        Serial.println("There is not config.json File!");
        return ;
    } else {
        //file exists, reading and loading
        Serial.println("reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
            Serial.println("opened config file");
            size_t size = configFile.size();
            // Allocate a buffer to store contents of the file.
            std::unique_ptr<char[]> buf(new char[size]);

            configFile.readBytes(buf.get(), size);
            DynamicJsonBuffer jsonBuffer;
            //StaticJsonBuffer jsonBuffer;
            JsonObject& json = jsonBuffer.parseObject(buf.get());
            json.printTo(Serial);
            if (json.success()) {
                Serial.println("\nparsed json");
                strcpy(mqtt_host,json["mqtt_host"]);
                strcpy(mqtt_port,json["mqtt_port"]);
                //strcpy(mqtt_topic,json["mqtt_topic"]);
                strcpy(mqtt_username,json["mqtt_username"]);
                strcpy(mqtt_password,json["mqtt_password"]);
            } else {
                Serial.println("failed to load json config");
            }
        }
        configFile.close();
    }
}

/************************************
 * Function: ledBlink()
 * Description: control LED flashing
 * Input: int blinknum --- number of flashing
 *        int ledcolor --- colors of LED （LED_RED, LED_GREEN, LED_BLUE)
 *        int ms       --- milliseconds of flashing
 * Output: NULL
 * Return: NULL
************************************/
void ledBlink(int blinknum,int ledcolor,int ms)
{
    ledPowerOn();
    int counti = 0;
    switch(ledcolor){
        case LED_RED:
            for(counti=0;counti<blinknum;counti++){
                leds[0] = CRGB::Red;
                FastLED.show();
                delay(ms);
                leds[0] = CRGB::Black;
                FastLED.show();
                delay(ms);
            }
            leds[0] = CRGB::Black;
        break;

        case LED_GREEN:
            for(counti=0;counti<blinknum;counti++){
                leds[0] = CRGB::Green;
                FastLED.show();
                delay(ms);
                leds[0] = CRGB::Black;
                FastLED.show();
                delay(ms);
            }
        break;

        case LED_BLUE:
            for(counti=0;counti<blinknum;counti++){
                leds[0] = CRGB::Blue;
                FastLED.show();
                delay(ms);
                leds[0] = CRGB::Black;
                FastLED.show();
                delay(ms);
            }
        break;

        default:
        break;
    }
    ledPowerOff();
}

/************************************
 * Function: wmConfig()
 * Description: Add configuration options on the wifimanager web page,
 *              run autoconnect function to issue "ESP32-Config" hotspot(password:12345678),
 *              configure related parameters on the web page and save the parameters to the SPIFFS
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void wmConfig()
{
    Serial.println("AP Mode");

    // custom menu via array or vector
    //
    // menu tokens, "wifi","wifinoscan","info","param","close","sep","erase","restart","exit" (sep is seperator) (if param is in menu, params will not show up in wifi page!)
    // const char* menu[] = {"wifi","info","param","sep","restart","exit"};
    // wm.setMenu(menu,6);
    std::vector<const char *> menu = {"wifinoscan","sep","restart","exit"};
    wm.setMenu(menu);

    //setupSpiffs();

    //Add HTTP parameters
    //WiFiManagerParameter custom_http_host("http_host","HTTP_Host(default)",http_host,HOST_LEN);
    //WiFiManagerParameter custom_http_port("http_port","HTTP_Port",http_port,PORT_LEN);
    WiFiManagerParameter custom_http_url("http_url","HTTP_URL",http_url,URL_LEN);

    //Add MQTT parameters
    WiFiManagerParameter custom_mqtt_host("mqtt_host","MQTT_Host",mqtt_host,HOST_LEN);
    WiFiManagerParameter custom_mqtt_port("mqtt_port","MQTT_Port",mqtt_port,PORT_LEN);
    //WiFiManagerParameter custom_mqtt_topic("mqtt_topic","MQTT_Topic",mqtt_topic,TOPIC_LEN);
    WiFiManagerParameter custom_mqtt_username("mqtt_username","MQTT_Username(optional)",mqtt_username,MQTTUSRN_LEN);
    WiFiManagerParameter custom_mqtt_password("mqtt_password","MQTT_Password(optional)",mqtt_password,MQTTPASS_LEN);

    wm.setSaveConfigCallback(saveConfigCallback);
   
    //http
    //wm.addParameter(&custom_http_host);
    //wm.addParameter(&custom_http_port);
    wm.addParameter(&custom_http_url);

    //mqtt
    wm.addParameter(&custom_mqtt_host);
    wm.addParameter(&custom_mqtt_port);
    //wm.addParameter(&custom_mqtt_topic);
    wm.addParameter(&custom_mqtt_username);
    wm.addParameter(&custom_mqtt_password);
   
    //wm.setConfigPortalBlocking(false);
    wm.setConfigPortalBlocking(true);

    //wm.setConnectTimeout(120);
    //wm.setConfigPortalTimeout(180);
    bool res;


    res = wm.autoConnect("ESP32-Config","12345678"); // password protected ap

    if(!res) {

        Serial.println("Failed to connect or hit timeout");
        //wifi_connected = false;
        // ESP.restart();
        
    }
    else {
        //wm.setConfigPortalBlocking(false);
        //if you get here you have connected to the WiFi
        //wifi_connected = true;
        wmconfig_flag = 1;
        Serial.println("connected...yeey :)");
        ledBlink(3,LED_GREEN,300);
    }
    wm.setConfigPortalBlocking(false);
    //save wifimanager's parameters to FFS

    //save wifi parameters
    strcpy(app_ssid,WiFi.SSID().c_str());
    strcpy(app_password,WiFi.psk().c_str());

    //save http parameters
    //strcpy(http_host, custom_http_host.getValue());
    //strcpy(http_port, custom_http_port.getValue());
    strcpy(http_url, custom_http_url.getValue());
    
    //save mqtt parameters
    strcpy(mqtt_host, custom_mqtt_host.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    //strcpy(mqtt_topic, custom_mqtt_topic.getValue());
    strcpy(mqtt_username, custom_mqtt_username.getValue());
    strcpy(mqtt_password, custom_mqtt_password.getValue());

    if (shouldSaveConfig) {
        Serial.println("saving config");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.createObject();
        
        //wifi
        json["ssid"] = app_ssid;
        json["password"] = app_password;

        //http
        //json["http_host"] = http_host;
        //json["http_port"] = http_port;
        json["http_url"] = http_url;

        //mqtt
        json["mqtt_host"] = mqtt_host;
        json["mqtt_port"] = mqtt_port;
        //json["mqtt_topic"] = mqtt_topic;
        json["mqtt_username"] = mqtt_username;
        json["mqtt_password"] = mqtt_password;

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
            Serial.println("failed to open config file for writing");
        }

        json.prettyPrintTo(Serial);
        json.printTo(configFile);
        configFile.close();
        //end save
        shouldSaveConfig = false;
    }
}

/************************************
 * Function: factoryReset()
 * Description: restore factory setting
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void factoryReset()
{
    wm.resetSettings();
    delay(1000);
    ESP.restart();
}

/************************************
 * Function: mqttConnect()
 * Description: connect to MQTT and return the connection flag
 * Input: NULL
 * Output: NULL
 * Return: mqtt_connected --- true   connect to MQTT success
 *         mqtt_connected --- false  connect to MQTT failed
************************************/
bool mqttConnect()
{
    Serial.println("Attemping MQTT connect...");
    if(mqtt_username && mqtt_password){
        mqttclient.connect(clientId,mqtt_username,mqtt_password);
    } else {
        mqttclient.connect(clientId);
    }

    if(mqttclient.connected()){
        Serial.println("mqtt connected");
        ledBlink(3,LED_BLUE,300);
        mqtt_connected = true;

    } else {
        Serial.print("failed with state ");
        Serial.println(mqttclient.state());
        factoryReset();
        delay(500);
        mqtt_connected = false;
    }
    return mqtt_connected;
}

/************************************
 * Function: mqttConnect()
 * Description: mqtt callback function
 * Input: char *topic   --- topic
 *        byte *payload --- message
 *        unsigned int length --- length of message
 * Output: NULL
 * Return: NULL
************************************/
void mqttCallback(char *topic,byte *payload,unsigned int length){

    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    Serial.print("Message: ");
    for(int i=0;i<length;i++){
        Serial.print((char)payload[i]);
    }
    Serial.println("");
    Serial.println("---------------------");
}

/************************************
 * Function: connectToWiFi()
 * Description: connect to WiFi
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void connectToWiFi()
{
    readWiFiDataFromFFS();
    WiFi.disconnect(true,true);
    Serial.println("start connect WIFI");
    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin(app_ssid, app_password);
    // wait for connection
    int count_t = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(F("."));
        count_t++;
        if(count_t>10){
            delay(1000);
            Serial.println("Connected wifi failed");
            //wifi_connected = false;
            break;
        }
    }
    if(WiFi.status() == WL_CONNECTED){
        Serial.print(F("Connected to "));
        Serial.println(app_ssid);
        Serial.print(F("IP address: "));
        Serial.println(WiFi.localIP());
        ledBlink(3,LED_GREEN,300);
        Serial.println("ESP32 Connect To WiFI Success");
        //wifi_connected = true;
    }
}

/************************************
 * Function: printWakeupReason()
 * Description: print the wakeup reason of the ESP32-button
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void printWakeupReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason){
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("Wakeup caused by external signal using RTC_IO");
        break;

        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("Wakeup caused by external signal using RTC_CNTL");
        break;

        default :
            Serial.printf("Wakeup was not caused by deep sleep:%d\n",wakeup_reason);
        break;
    }
}

/************************************
 * Function: keyInit()
 * Description: button init,configure the BUTTON_GPIO to INPUT_PULLDOWN
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void pinInit()
{
    pinMode(BUTTON_GPIO,INPUT_PULLDOWN);
    pinMode(LED_POWER_PIN,OUTPUT);
    //rtc_gpio_pulldown_en(BUTTON_GPIO);
    //attachInterrupt(digitalPinToInterrupt(BUTTON_GPIO),checkButton,CHANGE);    
}

void ledPowerOn()
{
    digitalWrite(LED_POWER_PIN,LOW);
    delay(500);
}

void ledPowerOff()
{
    digitalWrite(LED_POWER_PIN,HIGH);
    delay(500);
}
/************************************
 * Function: checkButton()
 * Description: check the button's state,long press to restore factory settings
 * Input: NULL
 * Output: NULL
 * Return: NULL
************************************/
void checkButton()
{
    while(digitalRead(BUTTON_GPIO) == HIGH){
        delay(3000);
        if(digitalRead(BUTTON_GPIO) == HIGH){
            wmconfig_flag = 0;
            Serial.println("BUTTON PRESS");
            factoryReset();
        }
    }
}

/************************************
 * Function: WiFiEvent()
 * Description: Events of WiFi
 * Input: WiFiEvent_t event
 * Output: NULL
 * Return: NULL
************************************/
void WiFiEvent(WiFiEvent_t event)
{
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch (event)
    {
    case SYSTEM_EVENT_WIFI_READY:
        Serial.println("WiFi interface ready!");
        break;
        
    case SYSTEM_EVENT_STA_CONNECTED:
        Serial.println("");
        Serial.println("WiFi Connected");
        wifi_connected = true;
        Serial.println(app_ssid);
        delay(100);
        Serial.println(app_password);
        delay(100);
        break;
    
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.println("");
        Serial.println("WiFi GOT IP");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("");
        wifi_connected = false;
        Serial.println("Disconnected WiFi");
        break;
  
    default:
        break;
    }
}

void setup() {

    Serial.begin(115200);
    delay(500);

    pinInit();
    
    WiFi.onEvent(WiFiEvent);

    checkButton();

    FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);

    ledBlink(3,LED_RED,300);
    setupSpiffs();
    readConfigFile();

    if(!wmconfig_flag){
        wmConfig();
    } else {
        connectToWiFi();
    }
    
    if(!wifi_connected){
        
        Serial.println("Connected wifi failed!");
        ledBlink(10,LED_RED,200);
    }
    
    readHttpDataFromFFS();
    readMqttDataFromFFS();

    String id="AB_";

    chipid = ESP.getEfuseMac();
    for(int i=0;i<6;i++){
        chip_mac[i] = (uint8_t)(chipid>>8*i);
    }
    sprintf(mac_str,"%02X%02X%02X%02X%02X%02X",BD_ADDR_HEX(chip_mac));
    id += String(mac_str);

    strcpy(clientId,id.c_str());

    if(wifi_connected){    
        delay(1000);
        if(strlen(http_url)){

            HTTPClient http;
            http.begin(http_url);

            Serial.print("[HTTP] GET ...\n");

            int httpCode = http.GET();
            delay(1000);

            if(httpCode > 0){
                Serial.printf("[HTTP] GET ...code:%d\n",httpCode);

                if(httpCode == HTTP_CODE_OK){
                    Serial.println("get true data");
                    String payload = http.getString();
                    Serial.println(payload);
                    ledBlink(3,LED_BLUE,300);
                } else {
                    //factoryReset();
                }
            } else {
                Serial.printf("[HTTP] GET ... failed,error:%s\n",http.errorToString(httpCode).c_str());
            }
            http.end();
        } else if(strlen(mqtt_host)){
            Serial.print("Request server: ");
            Serial.println(mqtt_host);

            mqttclient.setServer(mqtt_host,atoi(mqtt_port));
            mqttclient.setCallback(mqttCallback);

            mqttConnect();

            char message[128];
            sprintf(message,"{\"id\":\"%s\",\"state\":\"pushed\"}",clientId);

            if(!mqttclient.publish("button",message)){
                Serial.printf("[MQTT] Publish failed\n");
            } else {
                Serial.printf("[MQTT] Publish success\n");

                delay(1000);
                ledBlink(1,LED_GREEN,300);
                
            }
        }       
    }

    //esp_sleep_enable_ext0_wakeup(BUTTON_GPIO,ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_sleep_enable_ext1_wakeup(BUTTON_GPIO_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH);

    if (MDNS.begin("esp32")) {
        Serial.println("MDNS responder started");
    }

    ++bootCount;
    Serial.println("Boot number: " + String(bootCount));

    printWakeupReason();

    Serial.println("Entering deep sleep now");

    delay(500);

    adc_power_off();
    delay(500);

    esp_bluedroid_disable();
    delay(500);
    esp_bt_controller_disable();
    delay(500);

    esp_wifi_stop();
    delay(500);
#if 0
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);
#endif
    leds[0] = CRGB::Black;
    FastLED.show();
    delay(500);

    Serial.print("WiFi connect status: ");
    Serial.println(WiFi.status());
    esp_deep_sleep_start();
    
    server.begin();
}

void loop() {

    server.handleClient();

    mqttclient.loop();
}

