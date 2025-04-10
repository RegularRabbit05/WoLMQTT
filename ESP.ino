#define DEBUG_ESP_HTTP_UPDATE true
#define DEBUG_ESP_PORT Serial

#define CMD_PREFIX '>'

#include <SPI.h>
#include <Ethernet.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <EthernetUdp.h>
#include <PubSubClient.h>
#include <time.h>

const int current_version = 0;

const char *ssid PROGMEM = "wifi_name";
const char *password PROGMEM = "wifi_password";
const char *mqtt_broker PROGMEM = "mqtt.server"; 
const char *mqtt_topic PROGMEM = "wol/send";
const char *mqtt_topic_cmd PROGMEM = "wol/cmd";
const char *mqtt_username PROGMEM = "mqtt_username";  
const char *mqtt_password PROGMEM = "mqtt_password";
const int mqtt_port = 8883;

const char *ntp_server PROGMEM = "pool.ntp.org";
const long gmt_offset_sec = 0;
const int daylight_offset_sec = 0;

byte w5500_mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
const int w5500_cs_pin = 2;
//FROM: https://gist.github.com/straker/81b59eecf70da93af396f963596dfdc5
const char w5500_progmem_game[] PROGMEM = R"(<!doctypehtml><title>Basic Pong HTML Game</title><meta charset=UTF-8><style>body,html{height:100%;margin:0}body{background:#000;display:flex;align-items:center;justify-content:center}</style><canvas height=585 id=game width=750></canvas><script>const canvas=document.getElementById("game"),context=canvas.getContext("2d"),grid=15,paddleHeight=5*grid,maxPaddleY=canvas.height-grid-paddleHeight;var paddleSpeed=6,ballSpeed=2;const leftPaddle={x:2*grid,y:canvas.height/2-paddleHeight/2,width:grid,height:paddleHeight,dy:0},rightPaddle={x:canvas.width-3*grid,y:canvas.height/2-paddleHeight/2,width:grid,height:paddleHeight,dy:0},ball={x:canvas.width/2,y:canvas.height/2,width:grid,height:grid,resetting:!1,dx:ballSpeed,dy:-ballSpeed};function collides(d,l){return d.x<l.x+l.width&&d.x+d.width>l.x&&d.y<l.y+l.height&&d.y+d.height>l.y}function loop(){requestAnimationFrame(loop),context.clearRect(0,0,canvas.width,canvas.height),leftPaddle.y+=leftPaddle.dy,rightPaddle.y+=rightPaddle.dy,leftPaddle.y<grid?leftPaddle.y=grid:leftPaddle.y>maxPaddleY&&(leftPaddle.y=maxPaddleY),rightPaddle.y<grid?rightPaddle.y=grid:rightPaddle.y>maxPaddleY&&(rightPaddle.y=maxPaddleY),context.fillStyle="white",context.fillRect(leftPaddle.x,leftPaddle.y,leftPaddle.width,leftPaddle.height),context.fillRect(rightPaddle.x,rightPaddle.y,rightPaddle.width,rightPaddle.height),ball.x+=ball.dx,ball.y+=ball.dy,ball.y<grid?(ball.y=grid,ball.dy*=-1):ball.y+grid>canvas.height-grid&&(ball.y=canvas.height-2*grid,ball.dy*=-1),(ball.x<0||ball.x>canvas.width)&&!ball.resetting&&(ball.resetting=!0,setTimeout(()=>{ball.resetting=!1,ball.x=canvas.width/2,ball.y=canvas.height/2},400)),collides(ball,leftPaddle)?(ball.dx*=-1,ball.x=leftPaddle.x+leftPaddle.width):collides(ball,rightPaddle)&&(ball.dx*=-1,ball.x=rightPaddle.x-ball.width),context.fillRect(ball.x,ball.y,ball.width,ball.height),context.fillStyle="lightgrey",context.fillRect(0,0,canvas.width,grid),context.fillRect(0,canvas.height-grid,canvas.width,canvas.height);for(let d=grid;d<canvas.height-grid;d+=2*grid)context.fillRect(canvas.width/2-grid/2,d,grid,grid)}document.addEventListener("keydown",function(d){38===d.which?rightPaddle.dy=-paddleSpeed:40===d.which&&(rightPaddle.dy=paddleSpeed),87===d.which?leftPaddle.dy=-paddleSpeed:83===d.which&&(leftPaddle.dy=paddleSpeed)}),document.addEventListener("keyup",function(d){(38===d.which||40===d.which)&&(rightPaddle.dy=0),(83===d.which||87===d.which)&&(leftPaddle.dy=0)}),requestAnimationFrame(loop);</script>)";

const char* ota_update_idx PROGMEM = "https://server/update.idx";

static const char ca_cert[]
PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----

WRITE HERE YOUR CERTIFICATE PUBLIC KEY FOR THE MQTT SERVER

-----END CERTIFICATE-----
)EOF";

BearSSL::WiFiClientSecure espClient;
PubSubClient mqtt_client(espClient);
EthernetServer server(80);
EthernetUDP Udp;

void hex_decode(char *in, size_t len, uint8_t *out) {
  unsigned int i, t, hn, ln;
  for (t = 0,i = 0; i < len; i+=2,++t) {
    hn = in[i] > '9' ? toupper(in[i]) - 'A' + 10 : in[i] - '0';
    ln = in[i+1] > '9' ? toupper(in[i+1]) - 'A' + 10 : in[i+1] - '0';
    out[t] = (hn << 4 ) | ln;
  }
}

void performOTA(char* updateURL) {
  WiFiClient client;
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, updateURL);
  switch (ret) {
    case HTTP_UPDATE_FAILED: Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str()); break;
    case HTTP_UPDATE_NO_UPDATES: Serial.println("HTTP_UPDATE_NO_UPDATES"); break;
    case HTTP_UPDATE_OK: Serial.println("HTTP_UPDATE_OK"); break;
    default: break;
  }
  delay(1000);
  ESP.reset();
}

void checkPerformOTA() {
  HTTPClient http;
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, ota_update_idx);
  int httpCode = http.GET();
  if (httpCode >= 400) {
    Serial.println("OTA Failed");
    return;
  }
  Serial.print("OTA idx: ");
  String body = http.getString();
  Serial.println(body);
  if (!body.startsWith("http")) {
    Serial.println("No update found!");
    return;
  }
  char urlBuf[128];
  char numBuf[8];
  numBuf[0] = 0;
  body.getBytes((unsigned char*) urlBuf, sizeof(urlBuf));
  int i;
  for (i = 0; i < strlen(urlBuf)-1; i++) {
    if (urlBuf[i] == '@') {
      urlBuf[i] = 0;
      strncpy(numBuf, urlBuf+i+1, sizeof(numBuf));
      break;
    }
  }
  if (numBuf[0] == 0) {
    Serial.println("Update error 1");
    return;
  }
  Serial.print("Update version: ");
  i = atoi(numBuf);
  Serial.println(i);
  if (current_version >= i) {
    Serial.println("You are on the latest version!");
    return;
  }
  Serial.print("Update url: ");
  Serial.println(urlBuf);
  client.stop();
  performOTA(urlBuf);
}

void cmdCallback(byte *payload, unsigned int length) {
  char cmd[128];
  for (int i = 0; i < length && i < 128; i++) cmd[i] = payload[i];
  cmd[length] = 0;
  cmd[127] = 0;
  if (cmd[0] == 0 || cmd[0] == CMD_PREFIX) return;
  Serial.printf("Received command: %s\n", cmd);
  if (strcmp(cmd, "reboot") == 0) {
    snprintf(cmd, 128, "%cByeBye! :)", CMD_PREFIX);
    mqtt_client.publish(mqtt_topic_cmd, cmd);
    yield();
    ESP.reset();
    return;
  }
  if (strcmp(cmd, "ver") == 0) {
    snprintf(cmd, 128, "%c%d", CMD_PREFIX, current_version);
    mqtt_client.publish(mqtt_topic_cmd, cmd);
    return;
  }
  
  Serial.printf("Unknown command: %s\n", cmd);
  snprintf(cmd, 128, "%c%s", CMD_PREFIX, "Unknown command");
  mqtt_client.publish(mqtt_topic_cmd, cmd);
}

void mqttCallback(const char *topic, byte *payload, unsigned int length) {
  if (strcmp(topic, mqtt_topic_cmd) == 0) { cmdCallback(payload, length); return; }
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
  Serial.setDebugOutput(true);
  Serial.printf("Running version: %d\n", current_version);
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
  OTAs: {
    checkPerformOTA();
  }
  ETHs: {
    Ethernet.init(w5500_cs_pin);
    Serial.println("Initializing Ethernet...");
    IPAddress w5500_ip(192, 168, 69, 1);
    Ethernet.begin(w5500_mac, w5500_ip);
    delay(1000);
    Udp.begin(7);
    server.begin();
    Serial.print("Ethernet ready on: ");
    Serial.println(Ethernet.localIP());
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
        mqtt_client.subscribe(mqtt_topic_cmd);
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
  
  bool currentLineIsBlank = true;
  EthernetClient client = server.available();
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      Serial.write(c);
      if (c == '\n' && currentLineIsBlank) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=utf-8");
        client.println("Connection: close");
        client.println();
        client.println(w5500_progmem_game);
        break;
      }
      if (c == '\n') {
        currentLineIsBlank = true;
      } else if (c != '\r') {
        currentLineIsBlank = false;
      }
    }
  }
  delay(1);
  client.stop();
}
