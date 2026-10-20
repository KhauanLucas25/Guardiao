#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <cmath>
#include <cstring>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "guard.hpp"

const int GREEN=18, AMBER=19, RED=23, FAN=25, COMP=26, HEAT=33;
const int ATTACK=13, DOOR=14, SENSOR=27, ACK=32;

// DEFINIÇÃO EXATA DOS TÓPICOS
const char* ALERT_TOPIC="autotemp/f01/alerts";
const char* ALERT_ACK_TOPIC="autotemp/f01/ack"; // <- É EXATAMENTE NESTE TÓPICO QUE A AWS DEVE PUBLICAR
const char* CMD="autotemp/f01/command";

Plant plant;
Guard guard;
SemaphoreHandle_t mutex;
String endpoint, cert, key, ca;
String serialLine;
char snapshot[768]="{}";
bool configured=false, online=true, pending=false, pendingBad=false;
volatile bool connected=false;
double requested=-19, pendingTarget=-19;
bool requestDefrost=false, pendingDefrost=false;
unsigned long requestAt=0;
uint32_t lastCommand=0;
String bootId;
WiFiClientSecure tls;
PubSubClient mqtt(tls);

struct AlertEvent {
  char id[80]={0};
  char reason[40]={0};
  double simSeconds=0;
  long long timestamp=0;
};

constexpr unsigned ALERT_CAPACITY=8;
AlertEvent alerts[ALERT_CAPACITY];
unsigned alertHead=0, alertCount=0;
uint32_t alertSequence=0, droppedAlerts=0;

void recordProtection(const Output& state) {
  static bool wasLocked=false;
  bool rising=state.locked && !wasLocked;
  wasLocked=state.locked;
  if(!rising) return;
  AlertEvent e;
  snprintf(e.id,sizeof(e.id),"f01-%s-%lu",bootId.c_str(),
           (unsigned long)++alertSequence);
  snprintf(e.reason,sizeof(e.reason),"%s",state.reason.c_str());
  e.simSeconds=plant.t;
  time_t now=time(nullptr);
  e.timestamp=now>=1700000000 ? (long long)now : 0;
  xSemaphoreTake(mutex,portMAX_DELAY);
  bool full=alertCount==ALERT_CAPACITY;
  if(!full) {
    alerts[(alertHead+alertCount)%ALERT_CAPACITY]=e;
    ++alertCount;
  } else ++droppedAlerts;
  xSemaphoreGive(mutex);
  if(full) Serial.println("ALERTA_FILA_CHEIA_EVENTO_DESCARTADO");
  else {
    Serial.print("ALERTA_CRIADO=");
    Serial.println(e.id);
  }
}

void sendPendingAlert() {
  static String activeId="";
  static uint32_t attempts=0;
  static unsigned long lastAttempt=0, waitMs=0;
  AlertEvent e;
  xSemaphoreTake(mutex,portMAX_DELAY);
  bool available=alertCount>0;
  if(available) e=alerts[alertHead];
  xSemaphoreGive(mutex);
  if(!available) { activeId=""; attempts=0; return; }
  if(activeId!=e.id) {
    activeId=e.id; attempts=0; waitMs=0;
  }
  if(attempts && millis()-lastAttempt<waitMs) return;
  JsonDocument d;
  d["device"]="f01";
  d["event_id"]=e.id;
  d["event"]="PROTECTION_ACTIVATED";
  d["reason"]=e.reason;
  d["blocked"]=true; 
  d["message"]="Protecao acionada. Bloqueio local ativo na ocorrencia.";
  d["sim_s"]=e.simSeconds;
  if(e.timestamp) d["ts"]=e.timestamp;
  else d["ts"]=nullptr;
  String payload;
  serializeJson(d,payload);
  bool sent=mqtt.publish(ALERT_TOPIC,payload.c_str(),false);
  lastAttempt=millis();
  if(attempts<4) ++attempts;
  waitMs=attempts==1 ? 30000UL :
         attempts==2 ? 60000UL :
         attempts==3 ? 120000UL : 300000UL;
  Serial.print(sent ? "ALERTA_ENVIADO_AGUARDANDO_ACK=" : "ALERTA_ENVIO_FALHOU=");
  Serial.println(e.id);
}

void receiveAlertAck(byte* payload,unsigned int length) {
  JsonDocument d;
  if(length>256 || deserializeJson(d,payload,length) ||
     !d["event_id"].is<const char*>() ||
     !d["received"].is<bool>() || !d["received"].as<bool>()) {
    Serial.println("ALERTA_ACK_INVALIDO"); return;
  }
  const char* id=d["event_id"];
  xSemaphoreTake(mutex,portMAX_DELAY);
  bool matches=alertCount>0 && strcmp(alerts[alertHead].id,id)==0;
  if(matches) {
    alertHead=(alertHead+1)%ALERT_CAPACITY;
    --alertCount;
  }
  xSemaphoreGive(mutex);
  if(matches) { Serial.print("ALERTA_CONFIRMADO="); Serial.println(id); }
  else { Serial.print("ALERTA_ACK_IGNORADO (ID nao bate): "); Serial.println(id); }
}

constexpr int OLED_SDA=21, OLED_SCL=22;
constexpr uint8_t OLED_ADDRESS=0x3C;
Adafruit_SSD1306 oled(128,64,&Wire,-1);
bool oledReady=false;

void initDisplay() {
  Wire.begin(OLED_SDA,OLED_SCL);
  Wire.setTimeOut(50);
  Wire.beginTransmission(OLED_ADDRESS);
  if(Wire.endTransmission()!=0) {
    Serial.println("OLED_NAO_ENCONTRADO");
    return;
  }
  oledReady=oled.begin(SSD1306_SWITCHCAPVCC,OLED_ADDRESS,false,false);
  if(!oledReady) {
    Serial.println("OLED_FALHA_MEMORIA");
    return;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  oled.setCursor(0,0);
  oled.print("AUTOTEMP GUARD");
  oled.setCursor(0,16);
  oled.print("Iniciando...");
  oled.display();
  Serial.println("OLED_OK");
}

void updateDisplay(const Output& state) {
  if(!oledReady) return;
  static unsigned long lastDisplay=0;
  static bool first=true;
  if(!first && millis()-lastDisplay<500UL) return;
  first=false;
  lastDisplay=millis();
  bool ready, enabled;
  xSemaphoreTake(mutex,portMAX_DELAY);
  ready=configured; enabled=online;
  xSemaphoreGive(mutex);

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0,0);
  oled.print("AUTOTEMP GUARD");
  oled.drawFastHLine(0,9,128,SSD1306_WHITE);
  oled.setCursor(0,12);
  oled.print("Ar:       ");
  if(state.valid && std::isfinite(plant.air)) oled.print(plant.air,1);
  else oled.print("---");
  oled.print(" C");
  oled.setCursor(0,22);
  oled.print("Alimento: ");
  if(state.valid && std::isfinite(plant.food)) oled.print(plant.food,1);
  else oled.print("---");
  oled.print(" C");
  oled.setCursor(0,32);
  oled.print("E:");
  if(!state.valid) oled.print("FALHA SENSOR");
  else if(state.mode=="RECOVERY_FAILED") oled.print("FALHA RECUPERACAO");
  else oled.print(state.locked ? "PROTECAO" : "NORMAL");
  oled.setCursor(0,42);
  oled.print(state.reason.c_str());
  oled.setCursor(0,54);
  oled.print("AWS:");
  if(!enabled) oled.print("OFFLINE MANUAL");
  else if(!ready) oled.print("SEM CONFIG");
  else if(WiFi.status()!=WL_CONNECTED) oled.print("SEM WIFI");
  else oled.print(connected ? "CONECTADO" : "DESCONECTADO");
  oled.display();
}

// CORRIGIDO: A leitura do payload bruto agora ocorre EXATAMENTE dentro do callback!
void onMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("\n[MQTT RX] Tópico recebido: "); 
  Serial.println(topic);
  
  Serial.print("[MQTT RX] Payload bruto: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if(strcmp(topic, ALERT_ACK_TOPIC) == 0) {
    receiveAlertAck(payload, length); 
    return;
  }
  
  if(strcmp(topic, CMD) != 0) return;
  
  JsonDocument d;
  bool bad=length>256 || deserializeJson(d,payload,length);
  double target=-19; bool heat=false;
  uint32_t id=0;
  if(!bad) {
    bad=d.size()!=3 || !d["id"].is<uint32_t>() ||
        !d["target"].is<double>() || !d["defrost"].is<bool>();
    if(!bad) {
      id=d["id"]; target=d["target"]; heat=d["defrost"];
      bad=!std::isfinite(target);
    }
  }
  xSemaphoreTake(mutex,portMAX_DELAY);
  if(bad) pendingBad=true;
  else if(id>lastCommand) {
    lastCommand=id; pending=true;
    pendingTarget=target; pendingDefrost=heat;
  }
  xSemaphoreGive(mutex);
}

void cloudTask(void*) {
  bool started=false, wifiReported=false, ntpStarted=false, ntpReported=false;
  bool mqttReported=false;
  unsigned long wifiAttemptAt=0, wifiReportAt=0, ntpAttemptAt=0;
  unsigned long mqttAttemptAt=0, lastMqttLoop=0;
  unsigned wifiAttempts=0;
  bool mqttAttempted=false;

  tls.setHandshakeTimeout(60);
  tls.setTimeout(3000);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(30);
  mqtt.setKeepAlive(60);
  mqtt.setCallback(onMessage); // O callback correto foi setado aqui.

  for(;;) {
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool ready=configured, enabled=online;
    xSemaphoreGive(mutex);
    if(!ready || !enabled) {
      if(mqtt.connected()) mqtt.disconnect();
      if(connected) Serial.println("MQTT_PAUSADO_LOCALMENTE");
      connected=false; mqttReported=false; lastMqttLoop=0;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if(!started) {
      Serial.println("TLS_CONFIGURANDO");
      tls.setCACert(ca.c_str());
      tls.setCertificate(cert.c_str());
      tls.setPrivateKey(key.c_str());
      mqtt.setServer(endpoint.c_str(),8883);
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(false);
      WiFi.setSleep(false);
      Serial.println("WIFI_INICIANDO_CANAL_6");
      WiFi.begin("Wokwi-GUEST","",6);
      wifiAttemptAt=millis(); wifiReportAt=millis();
      wifiAttempts=1; started=true;
    }

    if(WiFi.status()!=WL_CONNECTED) {
      if(wifiReported) {
        Serial.println("WIFI_CONEXAO_PERDIDA");
        tls.stop();
        wifiReported=false;
        ntpStarted=false;
        wifiAttemptAt=millis();
      }
      connected=false; mqttReported=false; lastMqttLoop=0;
      if(millis()-wifiReportAt>=5000UL) {
        wifiReportAt=millis();
        Serial.print("WIFI_AGUARDANDO_STATUS=");
        Serial.print((int)WiFi.status());
        Serial.print(" TENTATIVA=");
        Serial.println(wifiAttempts);
      }
      if(millis()-wifiAttemptAt>=30000UL) {
        WiFi.disconnect(false,false); 
        vTaskDelay(pdMS_TO_TICKS(100));
        ++wifiAttempts;
        Serial.println("WIFI_NOVA_TENTATIVA_BUSCA_AUTOMATICA");
        WiFi.begin("Wokwi-GUEST",""); 
        wifiAttemptAt=millis();
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if(!wifiReported) {
      Serial.print("WIFI_OK IP=");
      Serial.println(WiFi.localIP());
      wifiReported=true;
      mqttAttempted=false;
    }
    if(!ntpStarted) {
      configTime(0,0,"pool.ntp.org","time.google.com");
      ntpStarted=true; ntpAttemptAt=millis();
      Serial.println("NTP_INICIANDO");
    }
    if(time(nullptr)<1700000000) {
      if(millis()-wifiReportAt>=5000UL) {
        wifiReportAt=millis(); Serial.println("NTP_AGUARDANDO");
      }
      if(millis()-ntpAttemptAt>=60000UL) {
        configTime(0,0,"pool.ntp.org","time.google.com");
        ntpAttemptAt=millis();
        Serial.println("NTP_NOVA_TENTATIVA");
      }
      connected=false;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if(!ntpReported) { ntpReported=true; Serial.println("NTP_OK"); }

    if(!mqtt.connected()) {
      connected=false; lastMqttLoop=0;
      if(mqttReported) {
        Serial.print("MQTT_DESCONECTADO_STATE=");
        Serial.println(mqtt.state());
        mqttReported=false;
        mqttAttemptAt=millis(); 
        mqttAttempted=true;
      }
      if(!mqttAttempted || millis()-mqttAttemptAt>=10000UL) {
        tls.stop(); 
        Serial.println("MQTT_TENTATIVA KEEPALIVE=60");
        bool ok=mqtt.connect("autotemp-f01");
        mqttAttemptAt=millis(); mqttAttempted=true;
        Serial.print("MQTT_STATE="); Serial.println(mqtt.state());
        if(ok) {
          connected=true; mqttReported=true; lastMqttLoop=millis();
          Serial.println("MQTT_CONECTADO");
          
          Serial.print("[INFO] TOPICO DE ACK DEFINIDO COMO: ");
          Serial.println(ALERT_ACK_TOPIC);
          
          Serial.println(mqtt.subscribe(CMD) ?
                         "MQTT_SUBSCRIBE_ENVIADO" : "MQTT_SUBSCRIBE_FALHOU");
          Serial.println(mqtt.subscribe(ALERT_ACK_TOPIC,1) ?
                         "ACK_SUBSCRIBE_ENVIADO" : "ACK_SUBSCRIBE_FALHOU");
        } else {
          char description[256]={0};
          int error=tls.lastError(description,sizeof(description));
          Serial.print("TLS_ERRO_CODIGO="); Serial.println(error);
          Serial.print("TLS_ERRO_DESCRICAO="); Serial.println(description);
        }
      }
    }

    if(mqtt.connected()) {
      unsigned long now=millis();
      if(lastMqttLoop && now-lastMqttLoop>2000UL) {
        Serial.print("MQTT_LOOP_ATRASO_MS=");
        Serial.println(now-lastMqttLoop);
      }
      lastMqttLoop=now;
      unsigned long loopStarted=millis();
      bool loopOk=mqtt.loop(); 
      if(millis()-loopStarted>2000UL) {
        Serial.print("MQTT_LOOP_DURACAO_MS=");
        Serial.println(millis()-loopStarted);
      }
      connected=loopOk && mqtt.connected();
      if(connected) sendPendingAlert();
      else if(mqttReported) {
        Serial.print("MQTT_DESCONECTADO_STATE=");
        Serial.println(mqtt.state());
        mqttReported=false; mqttAttemptAt=millis(); mqttAttempted=true;
        lastMqttLoop=0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool handleLine(String line) {
  line.trim();
  if(line.startsWith("{")) {
    if(configured) { Serial.println("CONFIG_JA_CARREGADA"); return false; }
    JsonDocument d;
    if(deserializeJson(d,line) || !d["endpoint"].is<const char*>() ||
       !d["cert"].is<const char*>() || !d["key"].is<const char*>() ||
       !d["ca"].is<const char*>()) {
      Serial.println("CONFIG_INVALIDA"); return false;
    }
    String host=d["endpoint"].as<String>();
    if(!host.endsWith(".iot.us-east-2.amazonaws.com")) {
      Serial.println("ENDPOINT_INVALIDO_USE_US_EAST_2"); return false;
    }
    xSemaphoreTake(mutex,portMAX_DELAY);
    endpoint=host; cert=d["cert"].as<String>(); key=d["key"].as<String>();
    ca=d["ca"].as<String>(); configured=true;
    xSemaphoreGive(mutex);
    Serial.println("CONFIG_OK_SEM_IMPRIMIR_SEGREDOS"); return false;
  }
  line.toUpperCase();
  if(line=="ATTACK") { requested=20; requestDefrost=true; requestAt=millis(); }
  else if(line=="NORMAL") { requested=-19; requestDefrost=false; requestAt=millis(); }
  else if(line=="DEFROST") { requestDefrost=true; requestAt=millis(); }
  else if(line=="DOOR") plant.door=!plant.door;
  else if(line=="FAIL") plant.failure=true;
  else if(line=="SENSOR") plant.sensor=true;
  else if(line=="HOT") plant.air=-11;
  else if(line=="COLD") plant.air=-30;
  else if(line=="REPAIR") { plant.sensor=plant.failure=plant.door=false; }
  else if(line=="RESET") plant.reset();
  else if(line=="OFFLINE" || line=="ONLINE") {
    xSemaphoreTake(mutex,portMAX_DELAY); online=(line=="ONLINE");
    xSemaphoreGive(mutex);
  }
  else if(line=="ACK") return true;
  else Serial.println("COMANDO_DESCONHECIDO");
  return false;
}

void setup() {
  Serial.setRxBufferSize(16384);
  Serial.begin(115200);
  Serial.println("AUTOTEMP_BUILD_R6_WIFI_KEEPALIVE60_US_EAST_2");
  for(int pin : {GREEN,AMBER,RED,FAN,COMP,HEAT}) {
    pinMode(pin,OUTPUT); digitalWrite(pin,LOW);
  }
  for(int pin : {ATTACK,DOOR,SENSOR,ACK}) pinMode(pin,INPUT_PULLUP);
  mutex=xSemaphoreCreateMutex();
  if(!mutex) {
    Serial.println("ERRO_MEMORIA_MUTEX");
    while(true) delay(1000);
  }
  initDisplay();
  char uniqueBoot[48];
  snprintf(uniqueBoot,sizeof(uniqueBoot),"%012llx-%08lx%08lx",
      (unsigned long long)ESP.getEfuseMac(),
      (unsigned long)esp_random(),(unsigned long)esp_random());
  bootId=uniqueBoot;
  serialLine.reserve(12000);
  if(xTaskCreatePinnedToCore(cloudTask,"cloud",16384,nullptr,tskIDLE_PRIORITY,nullptr,0)!=pdPASS)
    Serial.println("ERRO_CRIAR_CLOUD_TASK");
  Serial.println("AUTOTEMP_LOCAL_OK. Comandos ou CONFIG JSON no monitor serial.");
}

void loop() {
  static unsigned long lastTick=0,lastPrint=0;
  static bool was[4]={false,false,false,false};
  static bool ack=false;
  int pins[4]={ATTACK,DOOR,SENSOR,ACK};
  for(int i=0;i<4;i++) {
    bool down=digitalRead(pins[i])==LOW;
    if(down && !was[i]) {
      if(i==0) handleLine("ATTACK");
      if(i==1) handleLine("DOOR");
      if(i==2) handleLine("SENSOR");
      if(i==3) ack=true;
    }
    was[i]=down;
  }
  for(int n=0;n<256 && Serial.available();n++) {
    char ch=Serial.read();
    if(ch=='\n') { ack=handleLine(serialLine)||ack; serialLine=""; }
    else if(ch!='\r') {
      if(serialLine.length()<12000) serialLine+=ch;
      else { serialLine=""; Serial.println("LINHA_LONGA_REJEITADA"); }
    }
  }
  if(millis()-lastTick>=200) {
    lastTick=millis();
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool bad=pendingBad; pendingBad=false;
    if(pending) {
      requested=pendingTarget; requestDefrost=pendingDefrost;
      requestAt=millis(); pending=false;
    }
    xSemaphoreGive(mutex);
    if(millis()-requestAt>60000) { requested=-19; requestDefrost=false; }
    Output o=guard.update(plant,requested,requestDefrost,bad,ack);
    ack=false;
    recordProtection(o);
    plant.comp=o.comp; plant.fan=o.fan; plant.heat=o.heat;
    digitalWrite(GREEN,!o.locked && o.valid);
    digitalWrite(AMBER,!connected); digitalWrite(RED,o.locked);
    digitalWrite(COMP,o.comp); digitalWrite(FAN,o.fan); digitalWrite(HEAT,o.heat);
    JsonDocument d;
    d["device"]="f01"; d["air"]=plant.air; d["food"]=plant.food;
    d["sim_s"]=plant.t; d["rate"]=o.rate;
    d["mode"]=o.mode; d["reason"]=o.reason;
    d["locked"]=o.locked; d["valid"]=o.valid;
    d["compressor"]=o.comp; d["fan"]=o.fan; d["defrost"]=o.heat;
    d["door"]=plant.door;
    xSemaphoreTake(mutex,portMAX_DELAY);
    serializeJson(d,snapshot,sizeof(snapshot));
    xSemaphoreGive(mutex);
    if(millis()-lastPrint>=2000) {
      lastPrint=millis(); serializeJson(d,Serial); Serial.println();
    }
    updateDisplay(o);
    plant.step();
  }
  delay(2);
}