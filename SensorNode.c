#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "wifi_secrets.h"
#include "sensor_node_1_mqtt_secrets.h"

#define MQT_SEND_DELAY_MS 500 
#define MQTT_RETRY_DELAY_S 1
#define MQTT_SERVER "mqtt3.thingspeak.com"
#define MQTT_ENC 1883 //TCP, no encryption https://www.mathworks.com/help/thingspeak/mqtt-basics.html 							
#define MQTT_CHANNELID 2868666 // Update to your channel

#define SCK 18
#define MISO 19
#define MOSI 23
#define CS 5

#define PATH "/IoTSensorStream.csv"
#define LOG "/log.txt"

#define DEBUG 1 // Set to 0 to disable Serial Print

SPIClass spi = SPIClass(VSPI);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

inline bool mountSD(){
  spi.begin(SCK, MISO, MOSI, CS);
  if(!SD.begin(CS,spi,8000000)){
    #if DEBUG
    Serial.println("Card Mount Failed");
    #endif
    return false;
  }

  if(SD.cardType() == CARD_NONE){
    #if DEBUG
    Serial.println("No SD card attached");
    #endif
    return false;
  }
  #if DEBUG
  Serial.println("SD card mounted");
  #endif
  return true;
}

inline void appendFile(fs::FS &fs, const char * path, String message){
  #if DEBUG
  Serial.printf("Appending to file: %s\r\n", path);
  #endif
  File file = fs.open(path, FILE_APPEND);
  if(!file){
    #if DEBUG
    Serial.println("Failed to open file for appending");
    #endif
    return;
  }
  #if DEBUG
  if(file.print(message)){
      Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  #else
  file.print(message)
  #endif
  file.close();
}

inline void deleteFile(fs::FS &fs, const char * path){
  Serial.printf("Deleting file: %s\n", path);
  if(fs.remove(path)){
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

inline void connectWiFi(){
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!!!");
  Serial.println(WiFi.localIP());
}

inline void connectMQTT() {
  // Loop until connected.
  while ( !mqttClient.connected() )
  {
    if (mqttClient.connect(SECRET_MQTT_CLIENT_ID, SECRET_MQTT_USERNAME, SECRET_MQTT_PASSWORD)) 
    {
        Serial.println("MQTT successful." );
    }
    else {
        Serial.print("MQTT connection failed, rc = " );
        Serial.print(mqttClient.state()); //https://pubsubclient.knolleary.net/api#state 
        Serial.print("\r\n");
        delay(MQTT_RETRY_DELAY_S*1000);
    }
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(500);
  
  if(!mountSD()) {return;}
  connectWiFi();
  mqttClient.setServer(MQTT_SERVER, MQTT_ENC); 
  connectMQTT();
  deleteFile(SD,LOG); //delete previous log
  appendFile(SD,LOG,"hello from ESP32 - MQTT\r\n");
}

void loop() {
  // put your main code here, to run repeatedly:
  File file = SD.open(PATH);
  String result[5],Idx;
  static char topic_buffer[50];
  if(!file){
    Serial.printf("Failed to open file for reading: %s\r\n", PATH);
    return;
  }
  snprintf(topic_buffer, sizeof(topic_buffer),
        "channels/%d/publish",
        MQTT_CHANNELID);
  file.readStringUntil('\n'); // Ignore the header
  while(file.available()){
    Idx = file.readStringUntil(','); // Ignore timestep
    result[0] = file.readStringUntil(','); //volt
    result[1] = file.readStringUntil(','); //rotate
    result[2] = file.readStringUntil(','); //pressure
    result[3] = file.readStringUntil(','); //vibration
    result[4] = file.readStringUntil('\r'); //label
    file.readStringUntil('\n'); // remove the '\n'
    
    char payload[120] = "";
    
    for(int i =0; i<5;i++){
      snprintf(payload+strlen(payload), sizeof(payload)-strlen(payload),
          "field%d=%s&",
          i+1,result[i]);
    }
    snprintf(payload+strlen(payload), sizeof(payload)-strlen(payload),"status=MQTTPUBLISH");
    #if DEBUG
    Serial.println(payload);
    #endif
    while (!mqttClient.publish(topic_buffer, payload)){
      Serial.println("retry MQTT again");
      appendFile(SD,LOG,Idx+','+payload+",Failure\r\n");
      connectMQTT();
    }
    appendFile(SD,LOG, Idx+','+payload+",Success\r\n");
    delay(MQT_SEND_DELAY_MS);
    
  }
  file.close();
  #if DEBUG
  Serial.printf("end of file %s \r\n", PATH);
  #endif
  delay(MQT_SEND_DELAY_MS);

}
