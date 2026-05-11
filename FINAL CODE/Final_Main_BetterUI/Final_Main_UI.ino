#include <esp_now.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>

#define PUMP_ON LOW
#define PUMP_OFF HIGH

// --- NETWORK CONFIG ---
const char* wifi_ssid = "eduroam";
const char* eap_identity = "yka106@sfu.ca.ac.kr";
const char* eap_password = "Kyj65130327!";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "sfu/capstone/storm_surge";

const char* ap_ssid = "ESP32-StormHub";
const char* ap_password = "password123";

WiFiClient espClient;
PubSubClient client(espClient);
WebServer server(80);

// --- HARDWARE & CALIBRATION ---
const int PUMP_A = 25;
const int PUMP_B = 26;
const int threshold = 70;
const float FLOW_RATE = 0.051;        // 0.051L/s
const float TARGET_TOTAL_L = 4.89;   // total water target
const unsigned long CYCLE_TOTAL = 660000; // 11 min in ms (1 on + 10 off)
const unsigned long PUMP_DURATION = 60000; // 1 min pump on

// --- EXPERIMENT VARIABLES ---
bool stormMode = false;
unsigned long cycleStartTime = 0;
float currentTotalL = 0.0;

RTC_DATA_ATTR float baseAdcA = 0.0;
RTC_DATA_ATTR float baseAdcB = 0.0;

float currentAdcA = 0.0, currentAdcB = 0.0;
float deltaAdcA = 0.0, deltaAdcB = 0.0;
float currentPcA = 0.0, currentPcB = 0.0;

volatile bool newDataReady = false;

typedef struct struct_message {
  int id;
  float temp;
  float hum;
  int battery;
  float batteryVoltage;
  int moistureRaw;
  int soilmoisture;
} struct_message;

struct_message sensors[4];

// --- MQTT RECONNECT ---
void reconnectMQTT() {
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("SFU_StormHub_yka106")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
    }
  }
}

// --- MQTT PUBLISH ---
void sendToMQTT() {
  if (client.connected()) {
    String p = "{";
    p += "\"mode\":\"" + String(stormMode ? "STORM" : "NORMAL") + "\",";
    p += "\"s1_adc\":" + String(sensors[0].moistureRaw) + ",\"s1_pc\":" + String(sensors[0].soilmoisture) + ",";
    p += "\"s2_adc\":" + String(sensors[1].moistureRaw) + ",\"s2_pc\":" + String(sensors[1].soilmoisture) + ",";
    p += "\"s3_adc\":" + String(sensors[2].moistureRaw) + ",\"s3_pc\":" + String(sensors[2].soilmoisture) + ",";
    p += "\"s4_adc\":" + String(sensors[3].moistureRaw) + ",\"s4_pc\":" + String(sensors[3].soilmoisture) + ",";
    p += "\"deltaA_adc\":" + String(deltaAdcA, 2) + ",\"deltaB_adc\":" + String(deltaAdcB, 2) + ",";
    p += "\"totalL\":" + String(currentTotalL, 2) + ",";
    p += "\"pA\":\"" + String(digitalRead(PUMP_A) == PUMP_ON ? "ON" : "OFF") + "\",";
    p += "\"pB\":\"" + String(digitalRead(PUMP_B) == PUMP_ON ? "ON" : "OFF") + "\"";
    p += "}";
    client.publish(mqtt_topic, p.c_str());
  }
}

// --- ESP-NOW CALLBACK ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  struct_message incoming;
  memcpy(&incoming, incomingData, sizeof(incoming));
  int idx = incoming.id - 1;
  if (idx >= 0 && idx < 4) sensors[idx] = incoming;

  currentAdcA = (sensors[0].moistureRaw + sensors[1].moistureRaw) / 2.0;
  currentAdcB = (sensors[2].moistureRaw + sensors[3].moistureRaw) / 2.0;
  deltaAdcA = currentAdcA - baseAdcA;
  deltaAdcB = currentAdcB - baseAdcB;
  currentPcA = (sensors[0].soilmoisture + sensors[1].soilmoisture) / 2.0;
  currentPcB = (sensors[2].soilmoisture + sensors[3].soilmoisture) / 2.0;

  if (!stormMode) {
    float avgA = (sensors[0].soilmoisture + sensors[1].soilmoisture) / 2.0;
    float avgB = (sensors[2].soilmoisture + sensors[3].soilmoisture) / 2.0;
    digitalWrite(PUMP_A, avgA < threshold ? PUMP_ON : PUMP_OFF);
    digitalWrite(PUMP_B, avgB < threshold ? PUMP_ON : PUMP_OFF);
  }

  newDataReady = true;
}

// --- JSON DATA ENDPOINT ---
void handleData() {
  unsigned long elapsedMs = 0;
  unsigned long cycleElapsed = 0;
  int cycleNum = 0;
  bool isPumping = false;
  unsigned long phaseRemainMs = 0;

  if (stormMode) {
    elapsedMs = millis() - cycleStartTime;
    cycleElapsed = elapsedMs % CYCLE_TOTAL;
    cycleNum = (elapsedMs / CYCLE_TOTAL) + 1;
    isPumping = cycleElapsed < PUMP_DURATION;
    phaseRemainMs = isPumping
      ? (PUMP_DURATION - cycleElapsed)
      : (CYCLE_TOTAL - cycleElapsed);
  }

  int estTotalCycles = (int)((TARGET_TOTAL_L / FLOW_RATE) / 60) + 1;

  String j = "{";
  j += "\"storm\":" + String(stormMode ? "true" : "false") + ",";
  j += "\"totalL\":" + String(currentTotalL, 2) + ",";
  j += "\"targetL\":" + String(TARGET_TOTAL_L, 2) + ",";
  j += "\"elapsedSec\":" + String(elapsedMs / 1000) + ",";
  j += "\"cycleNum\":" + String(cycleNum) + ",";
  j += "\"estCycles\":" + String(estTotalCycles) + ",";
  j += "\"pumping\":" + String(isPumping ? "true" : "false") + ",";
  j += "\"phaseRemainSec\":" + String(phaseRemainMs / 1000) + ",";
  j += "\"adcA\":" + String(currentAdcA, 1) + ",";
  j += "\"adcB\":" + String(currentAdcB, 1) + ",";
  j += "\"deltaA\":" + String(abs(deltaAdcA), 1) + ",";
  j += "\"deltaB\":" + String(abs(deltaAdcB), 1) + ",";
  j += "\"baseA\":" + String(baseAdcA, 0) + ",";
  j += "\"baseB\":" + String(baseAdcB, 0) + ",";
  j += "\"pcA\":" + String(currentPcA, 1) + ",";
  j += "\"pcB\":" + String(currentPcB, 1) + ",";
  j += "\"s1_pc\":" + String(sensors[0].soilmoisture) + ",\"s1_adc\":" + String(sensors[0].moistureRaw) + ",";
  j += "\"s2_pc\":" + String(sensors[1].soilmoisture) + ",\"s2_adc\":" + String(sensors[1].moistureRaw) + ",";
  j += "\"s3_pc\":" + String(sensors[2].soilmoisture) + ",\"s3_adc\":" + String(sensors[2].moistureRaw) + ",";
  j += "\"s4_pc\":" + String(sensors[3].soilmoisture) + ",\"s4_adc\":" + String(sensors[3].moistureRaw) + ",";
  j += "\"pumpA\":\"" + String(digitalRead(PUMP_A) == PUMP_ON ? "ON" : "OFF") + "\",";
  j += "\"pumpB\":\"" + String(digitalRead(PUMP_B) == PUMP_ON ? "ON" : "OFF") + "\"";
  j += "}";

  server.send(200, "application/json", j);
}

// --- DASHBOARD HTML ---
String getHTML() {
  String h = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>HydroSoilSense</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@500;700&display=swap" rel="stylesheet">
<style>
:root{--bg:#f5f4f1;--sf:#fff;--bd:#e5e3df;--t1:#1a1a1a;--t2:#5c5650;--t3:#948d85;--gn:#1a4d2e;--gl:#e6efe9;--am:#946200;--al:#fdf6e8;--st:#9e3b08;--sl:#fdf0ea;--r:8px}
*{margin:0;padding:0;box-sizing:border-box}
body{background:var(--bg);color:var(--t1);font-family:'Inter',-apple-system,sans-serif;font-size:14px;line-height:1.5;-webkit-font-smoothing:antialiased}
.w{max-width:960px;margin:0 auto;padding:40px 24px}
header{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:28px;padding-bottom:20px;border-bottom:1px solid var(--bd)}
.ti{font-size:20px;font-weight:700;letter-spacing:-.3px;color:var(--gn)}
.su{font-size:13px;color:var(--t3);margin-top:3px}
.mp{display:inline-flex;align-items:center;gap:7px;padding:6px 14px;border-radius:100px;font-size:12px;font-weight:600;flex-shrink:0;margin-top:2px}
.mn{background:var(--gl);color:var(--gn)}
.ms{background:var(--sl);color:var(--st)}
.dot{width:7px;height:7px;border-radius:50%;background:currentColor}
.ms .dot{animation:bl 1.8s ease-in-out infinite}
@keyframes bl{0%,100%{opacity:1}50%{opacity:.25}}
.lb{font-size:11px;font-weight:600;letter-spacing:.8px;text-transform:uppercase;color:var(--t3);margin-bottom:10px}
.ss{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px;margin-bottom:24px}
.sc{background:var(--sf);border:1px solid var(--bd);border-radius:var(--r);padding:14px 16px}
.scl{font-size:11px;color:var(--t3);font-weight:500;margin-bottom:4px}
.scv{font-family:'JetBrains Mono',monospace;font-size:20px;font-weight:700;color:var(--t1);letter-spacing:-.5px}
.scv .u{font-size:12px;color:var(--t3);font-weight:500}
.scs{font-size:11px;color:var(--t3);margin-top:2px}
.wt{background:var(--bg);border-radius:3px;height:6px;overflow:hidden;margin-top:8px}
.wf{height:100%;border-radius:3px;background:var(--gn);transition:width .5s}
.cb{display:inline-flex;align-items:center;gap:5px;font-size:11px;font-weight:600;padding:3px 9px;border-radius:4px;margin-top:6px}
.cp{background:var(--gl);color:var(--gn)}
.cr{background:var(--bg);color:var(--t3)}
.cd{width:5px;height:5px;border-radius:50%;background:currentColor}
.cp .cd{animation:bl 1s ease-in-out infinite}
.tb{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:24px}
.ca{background:var(--sf);border:1px solid var(--bd);border-radius:var(--r);padding:20px}
.ch{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px}
.cn{font-size:14px;font-weight:700}
.ct{font-family:'JetBrains Mono',monospace;font-size:10px;font-weight:600;padding:2px 8px;border-radius:4px;background:var(--bg);color:var(--t3)}
.dh{margin-bottom:14px;padding:12px 14px;background:var(--al);border-radius:6px}
.dl{font-size:11px;color:var(--am);text-transform:uppercase;letter-spacing:.5px;font-weight:600;margin-bottom:2px}
.dv{font-family:'JetBrains Mono',monospace;font-size:32px;font-weight:700;color:var(--am);line-height:1;letter-spacing:-1px}
.ds{font-size:11px;color:var(--t3);margin-top:4px}
.ds b{color:var(--t2);font-weight:600}
.sp{margin-bottom:14px}
.spl{font-size:10px;color:var(--t3);text-transform:uppercase;letter-spacing:.5px;font-weight:500;margin-bottom:4px}
.spb{background:var(--bg);border-radius:6px;padding:8px 10px;height:56px;display:flex;align-items:flex-end}
.spb svg{width:100%;height:100%}
.mr{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:6px}
.ml{font-size:11px;color:var(--t3);font-weight:500}
.mv{font-family:'JetBrains Mono',monospace;font-size:18px;font-weight:700;color:var(--gn)}
.mv .u{font-size:12px;color:var(--t3);font-weight:500}
.mt{background:var(--bg);border-radius:3px;height:4px;overflow:hidden;margin-bottom:14px}
.mf{height:100%;border-radius:3px;background:linear-gradient(90deg,#c89640,#3a8a56,var(--gn))}
.sn{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:10px}
.sb{background:var(--bg);border-radius:6px;padding:8px 10px}
.sn1{font-size:10px;color:var(--t3);font-weight:500;margin-bottom:1px}
.sn2{font-family:'JetBrains Mono',monospace;font-size:13px;font-weight:700}
.sn3{font-family:'JetBrains Mono',monospace;font-size:10px;color:var(--t3)}
.pr{display:flex;justify-content:space-between;align-items:center;padding-top:8px;border-top:1px solid var(--bd)}
.pl{font-size:12px;color:var(--t3);font-weight:500}
.pb{font-family:'JetBrains Mono',monospace;font-size:11px;font-weight:700;padding:3px 10px;border-radius:4px}
.po{background:var(--gl);color:var(--gn)}
.pf{background:var(--bg);color:var(--t3)}
.ctr{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}
.bt{padding:12px 10px;border:1px solid var(--bd);border-radius:var(--r);cursor:pointer;font-family:'Inter',sans-serif;font-size:13px;font-weight:600;background:var(--sf);color:var(--t1);transition:background .15s}
.bt:hover{background:var(--bg)}
.bt:active{transform:scale(.98)}
.bp{background:var(--gn);color:#fff;border-color:var(--gn)}
.bp:hover{background:#164025}
.bw{background:var(--sl);color:var(--st);border-color:transparent}
.bw:hover{background:#fce4da}
.ft{text-align:center;margin-top:28px;padding-top:16px;border-top:1px solid var(--bd);font-size:11px;color:var(--t3)}
@media(max-width:640px){.tb,.ctr,.ss{grid-template-columns:1fr}header{flex-direction:column;gap:12px}}
</style>
</head>
<body>
<div class="w">
  <header>
    <div><div class="ti">HydroSoilSense</div><div class="su">SFU Capstone / Soil Moisture Monitoring</div></div>
    <div id="mode" class="mp mn"><div class="dot"></div><span id="modeText">Normal</span></div>
  </header>

  <div class="lb">Storm Operation</div>
  <div class="ss">
    <div class="sc"><div class="scl">Water dispensed</div><div class="scv"><span id="totalL">0.00</span> <span class="u">L</span></div><div class="scs" id="waterSub">Target: 12.48 L</div><div class="wt"><div class="wf" id="waterBar" style="width:0%"></div></div></div>
    <div class="sc"><div class="scl">Elapsed</div><div class="scv" id="elapsed">--:--</div><div class="scs">Since storm start</div><div id="cycleBadge" class="cb cr"><div class="cd"></div><span id="cyclePhase">Idle</span></div></div>
    <div class="sc"><div class="scl">Cycle</div><div class="scv"><span id="cycleNum">0</span> <span class="u">/ est. <span id="estCycles">0</span></span></div><div class="scs">1 min on / 10 min off</div></div>
  </div>

  <div class="lb">Testbed Comparison</div>
  <div class="tb">
    <div class="ca">
      <div class="ch"><span class="cn">Testbed A</span><span class="ct">S1 + S2</span></div>
      <div class="dh"><div class="dl">Delta ADC</div><div class="dv" id="deltaA">0</div><div class="ds">Baseline <b id="baseA">0</b> &rarr; Current <b id="curA">0</b></div></div>
      <div class="sp"><div class="spl">ADC trend</div><div class="spb"><svg id="sparkA" viewBox="0 0 200 40" preserveAspectRatio="none"><polyline id="lineA" fill="none" stroke="#1a4d2e" stroke-width="1.5" stroke-linejoin="round" points=""/><polyline id="fillA" fill="url(#gA)" stroke="none" points=""/><defs><linearGradient id="gA" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#1a4d2e" stop-opacity="0.15"/><stop offset="100%" stop-color="#1a4d2e" stop-opacity="0"/></linearGradient></defs></svg></div></div>
      <div class="mr"><span class="ml">Avg moisture</span><span class="mv" id="pcA">0<span class="u">%</span></span></div>
      <div class="mt"><div class="mf" id="barA" style="width:0%"></div></div>
      <div class="sn">
        <div class="sb"><div class="sn1">Sensor 1</div><div class="sn2" id="s1pc">0%</div><div class="sn3" id="s1adc">ADC 0</div></div>
        <div class="sb"><div class="sn1">Sensor 2</div><div class="sn2" id="s2pc">0%</div><div class="sn3" id="s2adc">ADC 0</div></div>
      </div>
      <div class="pr"><span class="pl">Pump A</span><span class="pb pf" id="pumpA">OFF</span></div>
    </div>
    <div class="ca">
      <div class="ch"><span class="cn">Testbed B</span><span class="ct">S3 + S4</span></div>
      <div class="dh"><div class="dl">Delta ADC</div><div class="dv" id="deltaB">0</div><div class="ds">Baseline <b id="baseB">0</b> &rarr; Current <b id="curB">0</b></div></div>
      <div class="sp"><div class="spl">ADC trend</div><div class="spb"><svg id="sparkB" viewBox="0 0 200 40" preserveAspectRatio="none"><polyline id="lineB" fill="none" stroke="#1a4d2e" stroke-width="1.5" stroke-linejoin="round" points=""/><polyline id="fillB" fill="url(#gB)" stroke="none" points=""/><defs><linearGradient id="gB" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#1a4d2e" stop-opacity="0.15"/><stop offset="100%" stop-color="#1a4d2e" stop-opacity="0"/></linearGradient></defs></svg></div></div>
      <div class="mr"><span class="ml">Avg moisture</span><span class="mv" id="pcB">0<span class="u">%</span></span></div>
      <div class="mt"><div class="mf" id="barB" style="width:0%"></div></div>
      <div class="sn">
        <div class="sb"><div class="sn1">Sensor 3</div><div class="sn2" id="s3pc">0%</div><div class="sn3" id="s3adc">ADC 0</div></div>
        <div class="sb"><div class="sn1">Sensor 4</div><div class="sn2" id="s4pc">0%</div><div class="sn3" id="s4adc">ADC 0</div></div>
      </div>
      <div class="pr"><span class="pl">Pump B</span><span class="pb pf" id="pumpB">OFF</span></div>
    </div>
  </div>

  <div class="lb">Controls</div>
  <div class="ctr">
    <button class="bt bp" onclick="fetch('/set_base')">Set Baseline</button>
    <button class="bt bw" onclick="fetch('/start_storm')">Start Storm</button>
    <button class="bt" onclick="fetch('/stop_all')">System Reset</button>
  </div>
  <div class="ft">MQTT: broker.hivemq.com / sfu/capstone/storm_surge</div>
</div>
<script>
var hA=[],hB=[],MX=60;
function fm(s){var m=Math.floor(s/60),r=s%60;return(m<10?'0':'')+m+':'+(r<10?'0':'')+r}
function sk(h,li,fi){
  if(h.length<2)return;
  var mn=Math.min.apply(null,h),mx=Math.max.apply(null,h);
  if(mx===mn)mx=mn+1;
  var p=[],n=h.length;
  for(var i=0;i<n;i++){var x=(i/(n-1))*200,y=40-((h[i]-mn)/(mx-mn))*36;p.push(x.toFixed(1)+','+y.toFixed(1))}
  document.getElementById(li).setAttribute('points',p.join(' '));
  document.getElementById(fi).setAttribute('points',p.join(' ')+' 200,40 0,40');
}
function tx(id,v){document.getElementById(id).textContent=v}
function upd(){
  fetch('/data').then(function(r){return r.json()}).then(function(d){
    var me=document.getElementById('mode'),mt=document.getElementById('modeText');
    if(d.storm){me.className='mp ms';mt.textContent='Storm Active'}else{me.className='mp mn';mt.textContent='Normal'}
    tx('totalL',d.totalL.toFixed(2));
    var pct=Math.min((d.totalL/d.targetL)*100,100);
    document.getElementById('waterBar').style.width=pct.toFixed(1)+'%';
    tx('waterSub','Target: '+d.targetL+' L ('+pct.toFixed(1)+'%)');
    tx('elapsed',d.storm?fm(d.elapsedSec):'--:--');
    tx('cycleNum',d.storm?d.cycleNum:'0');
    tx('estCycles',d.estCycles);
    var cb=document.getElementById('cycleBadge'),cp=document.getElementById('cyclePhase');
    if(!d.storm){cb.className='cb cr';cp.textContent='Idle'}
    else if(d.pumping){cb.className='cb cp';cp.textContent='Pumping \u2014 '+fm(d.phaseRemainSec)+' left'}
    else{cb.className='cb cr';cp.textContent='Resting \u2014 '+fm(d.phaseRemainSec)+' left'}
    tx('deltaA',d.deltaA.toFixed(0));tx('deltaB',d.deltaB.toFixed(0));
    tx('baseA',d.baseA.toFixed(0));tx('baseB',d.baseB.toFixed(0));
    tx('curA',d.adcA.toFixed(0));tx('curB',d.adcB.toFixed(0));
    tx('pcA',d.pcA.toFixed(1)+'%');tx('pcB',d.pcB.toFixed(1)+'%');
    document.getElementById('barA').style.width=Math.min(d.pcA,100).toFixed(1)+'%';
    document.getElementById('barB').style.width=Math.min(d.pcB,100).toFixed(1)+'%';
    tx('s1pc',d.s1_pc+'%');tx('s1adc','ADC '+d.s1_adc);
    tx('s2pc',d.s2_pc+'%');tx('s2adc','ADC '+d.s2_adc);
    tx('s3pc',d.s3_pc+'%');tx('s3adc','ADC '+d.s3_adc);
    tx('s4pc',d.s4_pc+'%');tx('s4adc','ADC '+d.s4_adc);
    var pa=document.getElementById('pumpA');pa.textContent=d.pumpA;pa.className='pb '+(d.pumpA==='ON'?'po':'pf');
    var pb=document.getElementById('pumpB');pb.textContent=d.pumpB;pb.className='pb '+(d.pumpB==='ON'?'po':'pf');
    hA.push(d.adcA);if(hA.length>MX)hA.shift();
    hB.push(d.adcB);if(hB.length>MX)hB.shift();
    sk(hA,'lineA','fillA');sk(hB,'lineB','fillB');
  }).catch(function(){});
}
upd();setInterval(upd,3000);
</script>
</body>
</html>
)rawhtml";
  return h;
}

void setup() {
  Serial.begin(115200);
  pinMode(PUMP_A, OUTPUT);
  pinMode(PUMP_B, OUTPUT);
  digitalWrite(PUMP_A, PUMP_OFF);
  digitalWrite(PUMP_B, PUMP_OFF);

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(wifi_ssid, WPA2_AUTH_PEAP, eap_identity, eap_identity, eap_password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());

  esp_now_init();
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  client.setServer(mqtt_server, 1883);

  server.on("/", []() { server.send(200, "text/html", getHTML()); });
  server.on("/data", handleData);

  server.on("/set_base", []() {
    baseAdcA = currentAdcA;
    baseAdcB = currentAdcB;
    server.send(200, "text/plain", "ADC Baseline Set");
  });

  server.on("/start_storm", []() {
    stormMode = true;
    currentTotalL = 0;
    cycleStartTime = millis();
    server.send(200, "text/plain", "Storm Started");
  });

  server.on("/stop_all", []() {
    stormMode = false;
    digitalWrite(PUMP_A, PUMP_OFF);
    digitalWrite(PUMP_B, PUMP_OFF);
    server.send(200, "text/plain", "System Reset");
  });

  server.begin();
}

void loop() {
  reconnectMQTT();
  client.loop();
  server.handleClient();

  if (newDataReady) {
    newDataReady = false;
    sendToMQTT();
  }

  // --- Storm Irrigation (1min ON / 10min OFF) ---
  if (stormMode && currentTotalL < TARGET_TOTAL_L) {
    unsigned long now = millis();
    unsigned long elapsed = (now - cycleStartTime) % CYCLE_TOTAL;

    if (elapsed < PUMP_DURATION) {
      digitalWrite(PUMP_A, PUMP_ON);
      digitalWrite(PUMP_B, PUMP_ON);

      static unsigned long lastUpdate = 0;
      if (now - lastUpdate >= 1000) {
        currentTotalL += FLOW_RATE;
        lastUpdate = now;
      }
    } else {
      digitalWrite(PUMP_A, PUMP_OFF);
      digitalWrite(PUMP_B, PUMP_OFF);
    }
  } else if (currentTotalL >= TARGET_TOTAL_L && stormMode) {
    digitalWrite(PUMP_A, PUMP_OFF);
    digitalWrite(PUMP_B, PUMP_OFF);
  }
}
