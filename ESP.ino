#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <time.h>

const char *ssid = "";
const char *password = "";
const char *mqtt_broker = ""; 
const char *mqtt_topic = "";
const char *mqtt_username = "";  
const char *mqtt_password = "";
const int mqtt_port = 8883;

const char *ntp_server = "";
const long gmt_offset_sec = 0;    
const int daylight_offset_sec = 0;

static const char ca_cert[]
PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----

WRITE HERE YOUR CERTIFICATE PUBLIC KEY FOR THE MQTT SERVER

-----END CERTIFICATE-----
)EOF";

BearSSL::WiFiClientSecure espClient;
PubSubClient mqtt_client(espClient);
WiFiUDP Udp;

void hex_decode(char *in, size_t len, uint8_t *out) {
  unsigned int i, t, hn, ln;
  for (t = 0,i = 0; i < len; i+=2,++t) {
    hn = in[i] > '9' ? toupper(in[i]) - 'A' + 10 : in[i] - '0';
    ln = in[i+1] > '9' ? toupper(in[i+1]) - 'A' + 10 : in[i+1] - '0';
    out[t] = (hn << 4 ) | ln;
  }
}

void mqttCallback(const char *topic, byte *payload, unsigned int length) {
    if (strcmp(topic, mqtt_topic) != 0) return;
    Serial.print("]: ");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
    uint8_t macAddress[length/2];
    hex_decode((char*) payload, length, macAddress);
    uint8_t wolPacket[6+(16*(length/2))] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (int i = 0; i < 16; i++) {
      for (int j = 0; j < length/2; j++) {
        wolPacket[6+(i*length/2)+j] = macAddress[j];
      }
    }
    IPAddress remoteIP(255, 255, 255, 255);
    int remotePort = 7;
    Udp.beginPacket(remoteIP, remotePort);
    Udp.write(wolPacket, sizeof(wolPacket));
    Udp.endPacket();
}

void setup() {
  Serial.begin(74880);
  WIFIs: {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int retries = 0;
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      yield();
      delay(1000);
      Serial.print('.');
      retries++;
      if (retries>60) {
        Serial.println("\nWiFi failed");
        ESP.reset();
      }
    }
    Serial.println("\nConnected to WiFi");
  }
  NTPs: {
    configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);
    Serial.print("Waiting for NTP time sync: ");
    while (time(nullptr) < 8 * 3600 * 2) {
        delay(1000);
        yield();
        Serial.print(".");
    }
    Serial.println("\nTime synchronized");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        Serial.print("Current time: ");
        Serial.println(asctime(&timeinfo));
    } else {
        Serial.println("Failed to obtain local time");
        ESP.reset();
    }
  }
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  MQTTs: {
    BearSSL::X509List serverTrustedCA(ca_cert);
    espClient.setTrustAnchors(&serverTrustedCA);
    yield();
    while (!mqtt_client.connected()) {
      yield();
      String client_id = "esp8266-client-" + String(WiFi.macAddress());
      Serial.printf("Connecting to MQTT Broker as %s.....\n", client_id.c_str());
      if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
        Serial.println("Connected to MQTT broker");
        mqtt_client.subscribe(mqtt_topic);
      } else {
        char err_buf[128];
        espClient.getLastSSLError(err_buf, sizeof(err_buf));
        Serial.print("Failed to connect to MQTT broker, rc=");
        Serial.println(mqtt_client.state());
        Serial.print("SSL error: ");
        Serial.println(err_buf);
        ESP.reset();
      }
    }
  }
}

void loop() {
  if (!mqtt_client.connected()) {
    Serial.println("Disconnected, restarting...");
    ESP.reset();
  }
  mqtt_client.loop();
}
