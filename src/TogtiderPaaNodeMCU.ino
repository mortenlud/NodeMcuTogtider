#include <Arduino.h>

#include <Time.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <PubSubClient.h>

const char* ssid     = "your_ssid";
const char* password = "your_password";

const byte mqtt_broker[] = {192, 168, 1, 146};
const int mqtt_port = 1883;

const char* host = "reisapi.ruter.no";
const int httpPort = 80;
String url = "/StopVisit/GetDepartures/2190400";
// stoppested Sandvika: 2190400
// stoppested Frydendal: 2200451
// For full oversikt: http://reisapi.ruter.no/Place/GetStopsRuter

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(8, 8, RX,
                            NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +
                            NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE,
                            NEO_GRB            + NEO_KHZ800);

WiFiClient client;
PubSubClient mqttClient(client);

class TrainArrival
{
  public:
    char train[80];
    char arrivalDate[40];
    char trainDelay[40];
};

void setup() {
  Serial.begin(9600);
  delay(10);

  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(20);
  matrix.setTextColor(matrix.Color(0, 255, 0));

  mqttClient.setServer(mqtt_broker, mqtt_port);
}

void loop() {

  TrainArrival trainArrival = findTrainArrival(client);

  int arivalTimeInSec = parseTrainTime(trainArrival.arrivalDate);
  int timeInSec = getClock(client);

  int timeToArrivalMin = timeToArrival(arivalTimeInSec,timeInSec);

  if(timeToArrivalMin > 3){
    matrix.setTextColor(matrix.Color(0, 255, 0));
  }else{
    matrix.setTextColor(matrix.Color(255, 0, 0));
  }

  if (!mqttClient.connected()) {
    Serial.println("Connecting mqtt");
    reconnect();
  }

  String data = "{\"name\": \"Neste tog\", \"value\": \"";
  data = data + String(timeToArrivalMin) + " " + trainArrival.train;
  data = data + "\"}";

  char payload[200];
  String(data).toCharArray(payload,200);

  mqttClient.publish("pepperkakebyen", payload);


  if(timeToArrivalMin < 10 && timeToArrivalMin >= 0){
    printStaticText(String(timeToArrivalMin));
    delay(2000);
  }else{
    printRollingText(String(timeToArrivalMin));
    delay(500);
  }

  printRollingText(trainArrival.train);
}

void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

TrainArrival findTrainArrival(WiFiClient client){
  TrainArrival trainArrival;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    return trainArrival;
  }
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  delay(10);

  return parseJson(client);
}

int timeToArrival(int arrivalTimeInSec, int timeInSec){
  int timeToArrivalSec = arrivalTimeInSec - timeInSec;
  int timeToArrivalMin = timeToArrivalSec / 60;
  return timeToArrivalMin;
}

TrainArrival parseJson(WiFiClient client){
  TrainArrival trainArrival;
  char tag[80];
  char destinationName[40];
  char expectedArrivalTime[40];
  char directionRef[40];
  char delayTag[40];
  char publishedLineName[40];

  int charNr = 0;

  boolean isTag = false;
  boolean isDestinationName = false;
  boolean isExpectedArivalTime = false;
  boolean isDirectionRef = false;
  boolean isDelay = false;
  boolean isPublishedLineName = false;
  int connectionLoop = 0;
  while (client.connected()){
    Serial.print(".");
    while (client.available()) {
      char c = client.read();
      //Serial.print(c);

      if (isDestinationName){
        if (isNotSpecialChar(c)){
          destinationName[charNr++] = c;
          continue;
        }
      }
      if (isExpectedArivalTime){
        if (isNotSpecialChar(c)){
          expectedArrivalTime[charNr++] = c;
          continue;
        }
      }
      if (isDirectionRef){
        if (isNotSpecialChar(c)){
          directionRef[charNr++] = c;
          continue;
        }
      }
      if (isDelay){
        if (isNotSpecialChar(c)){
          delayTag[charNr++] = c;
          continue;
        }
      }
      if (isPublishedLineName){
        if (isNotSpecialChar(c)){
          publishedLineName[charNr++] = c;
          continue;
        }
      }
      if (isEndParameterTag(c)){
        isTag = false;
        charNr = 0;
        if (equal(tag, "DestinationName", 14)){
          isDestinationName = true;
        }
        if (equal(tag, "ExpectedArrivalTime", 18)){
          isExpectedArivalTime = true;
        }
        if (equal(tag, "DirectionRef", 12)){
          isDirectionRef = true;
        }
        if (equal(tag, "Delay", 5)){
          isDelay = true;
        }
        if (equal(tag, "PublishedLineName", 17)){
          isPublishedLineName = true;
        }
      }

      if (isTag){
        if (c != '"' && charNr < 79){
          tag[charNr++] = c;
        }
      }

      if (isStartTag(c)){
        if (isPublishedLineName){
          publishedLineName[charNr] = '\0';
        }
        if (isDestinationName){
          destinationName[charNr] = '\0';
        }
        if (isDirectionRef){
          directionRef[charNr] = '\0';
        }
        if (isDelay){
          delayTag[charNr] = '\0';
        }
        if (isExpectedArivalTime){
          expectedArrivalTime[charNr] = '\0';

          //retning drammen
          if (directionRef[0] == '2' && !isFlytog(publishedLineName)){
            strcpy(trainArrival.train, publishedLineName);
            strcat(trainArrival.train, " ");
            strcat(trainArrival.train, destinationName);
            strcpy(trainArrival.arrivalDate,expectedArrivalTime);

            if (delayTag[0] != '\0'){
              strcpy(trainArrival.trainDelay, delayTag);
            }

            client.stop();
            return trainArrival;
          }
        }

        isDestinationName = false;
        isExpectedArivalTime = false;
        isDirectionRef = false;
        isDelay = false;
        isPublishedLineName = false;

        isTag = true;
        charNr = 0;
      }
    }
    delay(1000);
    connectionLoop++;
    if (connectionLoop > 30){
      client.stop();
    }
  }
  return trainArrival;
}

boolean isFlytog(char publishedLineName[]){
  if ('F' == publishedLineName[0] && 'T' == publishedLineName[1]){
    return true;
  }
  return false;
}

boolean isStartTag(char c){
  return c == ',' || c == '{' || c == '[';
}

boolean isEndParameterTag(char c){
  return c == ':';
}

boolean isNotSpecialChar(char c){
  return c != '"' && c != ',';
}

boolean equal(char* str1, String str2, int len){
  for (int i = 0; i < len; i++){
    if (str1[i] != str2[i]){
      return false;
    }
  }
  return true;
}

int getClock(WiFiClient client){
  if (!client.connect("google.no", 80)) {
    Serial.println("connection failed");
    return now();
  }
  client.print("HEAD / HTTP/1.1\r\n\r\n");
  while(!client.available()) {
     delay(1000);
  }
  while(client.available()){
    if (client.read() == '\n') {
      if (client.read() == 'D') {
        if (client.read() == 'a') {
          if (client.read() == 't') {
            if (client.read() == 'e') {
              if (client.read() == ':') {
                client.read();
                //21 Sep 2016 21:04:31 GMT
                String theDate = client.readStringUntil('\r');
                Serial.println("dato" + theDate);
                client.stop();
                int myhour = parseHour(theDate);
                int myminute = parseMinute(theDate);
                int myDay = parseDay1(theDate);
                setTime(myhour,myminute,0,1,1,2016);

                Serial.println("time now:");
                Serial.println(hour());
                Serial.println(minute());
                Serial.println(second());
                Serial.println(day());
                Serial.println(month());
                Serial.println(year());
              }
            }
          }
        }
      }
    }
  }
  return now();
}

int parseTrainTime(String dateTime){
  //2016-09-22T00:16:58+02:00
  int myhour = parseHour(dateTime);
  int myminute = parseMinute(dateTime);
  int timeZone = parseTimezone(dateTime);
  int myDay = parseDay2(dateTime);
  if(myhour - timeZone > 0){
    setTime(myhour-timeZone,myminute,0,1,1,2016);
  }else{
    int tmpHour = 24;
    tmpHour = tmpHour + myhour;
    tmpHour = tmpHour - timeZone;
    setTime(tmpHour,myminute,0,1,1,2016);
  }
  Serial.println("time train:");
  Serial.println(hour());
  Serial.println(minute());
  Serial.println(day());
  Serial.println(month());
  Serial.println(year());
  return now();
}

int parseDay1(String dateTime){
  String timeStr = dateTime.substring(0, 3);
  return timeStr.toInt();
}

int parseDay2(String dateTime){
  int indexTime = dateTime.indexOf(":");
  String timeStr = dateTime.substring(indexTime -5 , indexTime-3);
  return timeStr.toInt();
}

int parseHour(String dateTime){
  int indexTime = dateTime.indexOf(":");
  String timeStr = dateTime.substring(indexTime - 2, indexTime);
  return timeStr.toInt();
}

int parseMinute(String dateTime){
  int indexTime = dateTime.indexOf(":");
  String minStr = dateTime.substring(indexTime + 1, indexTime + 3);
  return minStr.toInt();
}

int parseTimezone(String dateTime){
  int indexTime = dateTime.indexOf("+");
  String timeZone = dateTime.substring(indexTime + 1, indexTime + 3);
  return timeZone.toInt();
}

void printStaticText(String text) {
    matrix.fillScreen(0);
    matrix.setCursor(0, 0);
    matrix.print(text);
    matrix.show();
}

void printRollingText(String text) {
  int x    = matrix.width();
  int size = ( text.length() * 6 ) + matrix.width();

  while(--x > -size){
    matrix.fillScreen(0);
    matrix.setCursor(x, 0);
    //matrix.setTextColor(matrix.Color(x*5, 255 -5*x, 0));
    matrix.print(text);
    matrix.show();
    delay(100);
  }
}
