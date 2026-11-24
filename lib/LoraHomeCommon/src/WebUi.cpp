#include "WebUi.h"
#include "RadioProfileSettings.h"

#include <Preferences.h>
#include <WiFi.h>
#include <Crypto.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

namespace {
constexpr uint8_t SESSION_COUNT = 2;
constexpr uint8_t COMMAND_COUNT = 4;
constexpr uint8_t OPERATION_RESULT_COUNT = 8;
constexpr uint8_t LOG_LINE_COUNT = 24;
constexpr size_t LOG_LINE_LENGTH = 192;
constexpr uint8_t MQTT_EVENT_COUNT = 8;
constexpr size_t MQTT_TOPIC_LENGTH = 96;
constexpr size_t MQTT_PAYLOAD_LENGTH = 2048;
constexpr uint32_t SESSION_LIFETIME_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t LOGIN_WINDOW_MS = 60UL * 1000UL;
constexpr uint8_t LOGIN_LIMIT = 5;
constexpr uint32_t PBKDF2_ROUNDS = 20000;

httpd_handle_t server = nullptr;
SemaphoreHandle_t stateMutex = nullptr;
GatewayWebSnapshot currentSnapshot{};
GatewayWebConfigSnapshot currentConfig{};
GatewayWebCommand commands[COMMAND_COUNT];
uint8_t commandRead = 0;
uint8_t commandWrite = 0;
uint8_t commandUsed = 0;
uint32_t nextOperationId = 1;
GatewayWebOperationResult operationResults[OPERATION_RESULT_COUNT];
uint8_t nextOperationResult = 0;
uint8_t operationResultUsed = 0;
uint8_t preparedPasswordSalt[16] = {};
uint8_t preparedPasswordHash[32] = {};
bool passwordPrepared = false;
char logLines[LOG_LINE_COUNT][LOG_LINE_LENGTH] = {};
uint8_t nextLogLine = 0;
uint8_t logLineUsed = 0;
uint32_t totalLogLines = 0;
uint32_t lastBroadcastLine = 0;
bool websocketWorkPending = false;
struct MqttEvent {
    uint32_t sequence = 0;
    uint32_t uptimeSeconds = 0;
    char direction[4] = {};
    char topic[MQTT_TOPIC_LENGTH] = {};
    char payload[MQTT_PAYLOAD_LENGTH] = {};
};
MqttEvent mqttEvents[MQTT_EVENT_COUNT];
uint8_t nextMqttEvent = 0;
uint8_t mqttEventUsed = 0;
uint32_t totalMqttEvents = 0;
char bootstrapPassword[25] = {};
uint8_t passwordSalt[16] = {};
uint8_t passwordHash[32] = {};
uint32_t loginWindowStarted = 0;
uint8_t loginFailures = 0;
WebUi::ActionLogger actionLogger = nullptr;

bool enqueue(GatewayWebCommand& command);

class WebUiActionLog {
public:
    explicit WebUiActionLog(const char* operation) : operation(operation) {}
    ~WebUiActionLog() {
        if (operation) gatewayWebUi.logAction(operation, outcome);
    }

    void setOutcome(const char* value) { outcome = value; }

private:
    const char* operation;
    const char* outcome = "rejected";
};

bool tracksOperationResult(GatewayWebCommandType type) {
    return type == GatewayWebCommandType::GatewayConfig ||
           type == GatewayWebCommandType::ChangePassword ||
           type == GatewayWebCommandType::RelayConfig;
}

void storeOperationResultLocked(uint32_t operationId, const char* status, const char* message) {
    GatewayWebOperationResult& result = operationResults[nextOperationResult];
    result.operationId = operationId;
    snprintf(result.status, sizeof(result.status), "%s", status);
    snprintf(result.message, sizeof(result.message), "%s", message);
    nextOperationResult = (nextOperationResult + 1) % OPERATION_RESULT_COUNT;
    if (operationResultUsed < OPERATION_RESULT_COUNT) ++operationResultUsed;
}

struct Session {
    char token[49] = {};
    char csrf[33] = {};
    uint32_t createdMs = 0;
    uint32_t lastSeenMs = 0;
};
Session sessions[SESSION_COUNT];

void revokeAllSessions() {
    if (server) {
        size_t clientCount = CONFIG_LWIP_MAX_SOCKETS;
        int clientFds[CONFIG_LWIP_MAX_SOCKETS] = {};
        if (httpd_get_client_list(server, &clientCount, clientFds) == ESP_OK) {
            for (size_t i = 0; i < clientCount; ++i) {
                if (httpd_ws_get_fd_info(server, clientFds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                    httpd_sess_trigger_close(server, clientFds[i]);
                }
            }
        }
    }
    memset(sessions, 0, sizeof(sessions));
}



const char FINAL_PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>LoRaHome Boiler Control</title><style>
:root{color-scheme:dark;--panel:#172034;--panel2:#10182a;--green:#45d19a;--red:#ff6b62;--amber:#f0b43c;--muted:#9aa8bf}*{box-sizing:border-box}body{margin:0;font:14px system-ui;background:#0b1020;color:#e9edf7}header{padding:18px 5%;background:#11182a}main{max-width:1100px;margin:auto;padding:22px 22px 150px}.card{background:var(--panel);padding:18px;border-radius:12px;margin:12px 0}.grid,.readings{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px}.reading{background:var(--panel2);border-radius:8px;padding:12px}.reading b{display:block;font-size:1.15em;margin-top:4px}nav{margin-bottom:12px}nav button,button{background:#25324c;color:white;border:0;padding:10px 14px;border-radius:7px;margin:3px;cursor:pointer;transition:background-color .15s,filter .15s,transform .1s}button:hover:not(:disabled){filter:brightness(1.15)}button:active:not(:disabled){transform:scale(.96);filter:brightness(.9)}button:focus-visible{outline:2px solid var(--green);outline-offset:2px}button:disabled{opacity:.55;cursor:wait;transform:none}nav button.active{background:#3a4d73;font-weight:600;box-shadow:inset 0 -2px 0 var(--green)}.hide{display:none!important}.muted{color:var(--muted)}.ok{color:var(--green)}.warn{color:var(--amber)}.bad{color:var(--red)}.badge{display:inline-block;padding:4px 8px;border-radius:12px;background:#25324c}.badge.ok{background:#174936;color:#6cf0b8}.badge.bad{background:#512524;color:#ff9b95}.detail-head{display:flex;justify-content:space-between;gap:12px;align-items:center}.detail-grid{display:grid;grid-template-columns:minmax(240px,320px) 1fr;gap:22px;align-items:center}.gauge{text-align:center}.gauge svg{display:block;width:100%;max-width:290px;height:auto;margin:0 auto}.gauge-value{font-size:2em;font-weight:700;margin-top:-12px}.gauge-state{font-weight:600}input,select{width:100%;padding:9px;margin:5px 0 12px;background:#0d1425;color:white;border:1px solid #34415c;border-radius:6px}.sensor-status{display:flex;align-items:center;gap:10px;flex-wrap:wrap;width:calc(100% + 36px);background:#f0b43c;color:#0b1020;padding:10px 18px;border-radius:12px 12px 0 0;margin:-18px -18px 12px}.sensor-status button{margin-left:auto;background:#174936;color:#fff}.error-balloon{position:fixed;z-index:30;top:16px;right:16px;max-width:min(90vw,480px);background:#a52b2b;color:#fff;padding:12px;border-radius:8px;display:flex;align-items:start;gap:12px;box-shadow:0 4px 16px #0008}.error-balloon button{margin:0;padding:0 6px;background:transparent;color:#fff;font-size:20px}.relay-row{display:flex;align-items:center;flex-wrap:wrap;gap:12px}.relay-toggle{position:relative;width:76px;height:32px;padding:0;border-radius:17px;background:#512524}.relay-toggle.on{background:#174936}.relay-toggle.pending{outline:2px solid var(--amber);opacity:.65}.relay-knob{position:absolute;top:4px;left:4px;width:24px;height:24px;border-radius:50%;background:#fff;transition:left .15s}.relay-toggle.on .relay-knob{left:48px}.config-table{width:100%;border-collapse:collapse}.config-table th,.config-table td{padding:5px;text-align:left;border-bottom:1px solid #34415c}.config-table input,.config-table select{margin:0}pre{overflow:auto;background:#050811;padding:12px;white-space:pre-wrap}.mqtt-list{display:grid;gap:9px;max-height:300px;overflow:auto}.mqtt-section{margin-top:20px}.mqtt-event{background:var(--panel2);border-left:4px solid #5e74a3;border-radius:7px;padding:10px}.mqtt-event.tx{border-left-color:var(--green)}.mqtt-event.rx{border-left-color:#e2a73a}.mqtt-head{display:flex;gap:10px;align-items:center}.mqtt-topic{font-family:monospace;overflow-wrap:anywhere}.mqtt-payload{margin:8px 0 0;white-space:pre-wrap;overflow-wrap:anywhere;color:#c8d2e5}#console{position:fixed;z-index:20;left:0;right:0;bottom:0;width:100%;height:11.2em;margin:0;padding:2px 5%;border-radius:0;box-shadow:0 -5px 18px #0008}#logResize{height:7px;margin:-2px -5% 0;cursor:ns-resize;background:#25324c}#console .detail-head{padding:0 2px;line-height:1.2}#console h3{margin:2px 0;font-size:1em}#log{height:calc(100% - 29px);margin:0;line-height:1.4;overflow:auto;background:#050811;padding:2px 4px;white-space:pre-wrap}@media(max-width:650px){main{padding-left:12px;padding-right:12px}.detail-grid{grid-template-columns:1fr}#console{padding-left:12px;padding-right:12px}}</style></head><body><header><h2>LoRaHome Boiler Control</h2></header><main>
<section id=login class=card><h3>Administrator sign in</h3><input id=pw type=password maxlength=64 placeholder=Password><button onclick=login()>Sign in</button><span id=loginmsg></span></section>
<div id=errorBalloon class="error-balloon hide" role=alert><span id=errorBalloonText></span><button type=button aria-label="Close error" onclick=closeErrorBalloon()>&times;</button></div><div id=app class=hide><div id=sdFailure class="card bad hide" role=alert>SD: failed ? configuration or storage unavailable</div><nav><button onclick="show('sensors')">Appliance</button><button onclick="show('overview')">Gateway</button><button onclick="show('mqtt')">MQTT</button><button onclick="loadConfig();show('settings')">Settings</button><button onclick=logout()>Sign out</button></nav>
<section id=sensors class="page hide"><div id=sensorDetail><div class=card><div id=sensorStatusBar class="sensor-status hide"><span id=discoveredNotice class=hide>Auto discovered sensor: <span id=discoveredMac></span> <span id=discoveredTrust></span></span><span id=sensorPending></span><button id=pairDiscovered class=hide type=button onclick=pairDiscovered()>Pair</button></div><span id=discoveryMsg></span><div class=detail-head><div><h3 id=sensorTitle>Sensor</h3><span id=sensorState class=badge>Unavailable</span></div></div><div class=detail-grid><div class=gauge><h4>Water Temperature</h4><div id=tempGauge>Boiler temperature unavailable</div></div><div id=sensorReadings class=readings><div class=reading>Battery<b id=batteryValue>Unavailable</b></div><div class=reading>Last seen<b id=lastSeenValue>Unavailable</b></div><div class=reading>Messages<b id=messageCountValue>Unavailable</b></div><div class=reading>Sleep<b id=sleepValue>Unavailable</b></div><div class=reading>Mode<b id=modeValue>Unavailable</b></div><div class=reading>TX power<b id=txPowerValue>Unavailable</b></div><div class=reading>CPU<b id=cpuValue>Unavailable</b></div><div class=reading>Signal<b id=signalValue>Unavailable</b></div><div class=reading>Time sync<b id=timeSyncValue>Unavailable</b></div><div class="reading debug-reading">Debug current<b id=debugCurrentValue>Unavailable</b></div><div class="reading debug-reading">Debug power<b id=debugPowerValue>Unavailable</b></div><div class="reading debug-reading">CPU temperature<b id=debugCpuValue>Unavailable</b></div><div class="reading debug-reading">Debug free RAM<b id=debugRamValue>Unavailable</b></div><div class="reading debug-reading">Config link<b id=debugLinkValue>Unavailable</b></div></div></div></div><div id=relayPanel class=card><h3>Switch</h3><div class=relay-row><button id=relayToggle class=relay-toggle onclick=toggleRelay() aria-label="Relay unavailable" aria-pressed=false aria-busy=false><span class=relay-knob></span></button><label>Timeout (s)<input id=srelaymax type=number min=1 max=7200></label><button id=setRelayMax type=button>SET</button><span id=relayCountdown class=muted>ON timeout inactive</span><span id=relayTimeoutResult class=muted></span></div></div><div class=card><h3>Sensor configuration</h3><table class=config-table><thead><tr><th>#</th><th>Sleep</th><th>Mode</th><th>TX</th><th>CPU (Dev only)</th><th>Source</th></tr></thead><tbody id=configQueue></tbody></table><button onclick=sensorConfig()>Send config</button></div></section>
<section id=overview class="page hide"><div class=detail-head><h3>Gateway status</h3><span id=gatewayRefreshState class=muted>Waiting for data</span></div><div class=grid><div class=card><h3>Network</h3><div class=reading>WiFi<b id=wifiValue>Unavailable</b></div><div class=reading>IP<b id=ipValue>Unavailable</b></div><div class=reading>RSSI<b id=wifiRssiValue>Unavailable</b></div><div class=reading>MQTT<b id=mqttValue>Unavailable</b></div></div><div class=card><h3>Radio</h3><div class=reading>PHY<b id=radioPhyValue>Unavailable</b></div><div class=reading>Link<b id=radioLinkValue>Unavailable</b></div><div class=reading>Activity<b id=radioActivityValue>Unavailable</b></div></div><div class=card><h3>Duty cycle</h3><div class=reading>Gateway burst<b id=burstValue>Unavailable</b></div><div class=reading>Cooldown<b id=cooldownValue>Unavailable</b></div><div class=reading>Sensor minimum sleep<b id=minimumSleepValue>Unavailable</b></div></div><div class=card><h3>System</h3><div class=reading>Uptime<b id=uptimeValue>Unavailable</b></div><div class=reading>Free RAM<b id=heapValue>Unavailable</b></div><div class=reading>SD<b id=sdValue>Unavailable</b></div></div></div></section>
<section id=mqtt class="page hide"><div class=card><div class=detail-head><h3>MQTT topic stream</h3><span id=mqttState class=muted></span></div><p class=muted>Successful publishes and subscribed configuration messages since startup. Sensitive JSON fields are redacted.</p><div class=mqtt-section><h4>Published</h4><div id=mqttPublished class=mqtt-list></div></div><div class=mqtt-section><h4>Received</h4><div id=mqttReceived class=mqtt-list></div></div></div></section>
<section id=settings class="page hide"><div class=card><h3>Gateway configuration</h3><p>Blank secret fields keep their current values.</p><input id=ssid placeholder="WiFi SSID"><input id=wpass type=password maxlength=64 placeholder="New WiFi password"><input id=mhost placeholder="MQTT host"><input id=mport type=number min=1 max=65535 placeholder="MQTT port"><input id=muser placeholder="MQTT user"><input id=mpass type=password maxlength=128 placeholder="New MQTT password"><input id=pairedSensorMac placeholder="Paired remote Sensor MAC (AA:BB:CC:DD:EE:FF)"><button id=saveConfigButton disabled onclick=saveConfig()>Save and apply</button><span id=cmsg></span></div><div class=card><h3>Change administrator password</h3><input id=current type=password maxlength=64 placeholder="Current password"><input id=newpw type=password minlength=12 maxlength=64 placeholder="New password (12+ characters)"><button onclick=changePassword()>Change</button><span id=pmsg></span></div></section><section id=console class=card><div id=logResize title="Drag to resize log"></div><div class=detail-head><h3>Live gateway log</h3><span id=logState class=muted>Disconnected</span></div><pre id=log></pre></section></div></main><script>
let csrf='',ws=null,wsRetryTimer=0,wsHeartbeat=0,wsRetryDelay=1000,sensors=[],waitingCommands=[],selectedMac='',refreshActive=false,refreshTimer=0,sessionEnded=false,relay={requestedClosed:false,feedbackClosed:false,failed:false,autoOff:false,durable:false,maxOnSeconds:7200,remainingSeconds:0},relayPendingTarget=null,relayRevertedState=null,relayPendingPolls=0,relaySnapshotMs=0,discoveredCandidate=null,errorBalloonTimer=0;
const $=id=>document.getElementById(id);const finite=value=>{if(value===null||value===undefined||value==='')return null;const n=Number(value);return Number.isFinite(n)?n:null};const showValue=(id,value)=>{$(id).textContent=value};
let logDragStart=0,logStartHeight=0;function setLogHeight(height){const value=Math.max(156,Math.min(Math.round(innerHeight*.7),height));$('console').style.height=value+'px';document.querySelector('main').style.paddingBottom=(value+20)+'px'}$('logResize').addEventListener('pointerdown',e=>{logDragStart=e.clientY;logStartHeight=$('console').getBoundingClientRect().height;$('logResize').setPointerCapture(e.pointerId)});$('logResize').addEventListener('pointermove',e=>{if($('logResize').hasPointerCapture(e.pointerId))setLogHeight(logStartHeight+logDragStart-e.clientY)});$('logResize').addEventListener('pointerup',e=>{$('logResize').releasePointerCapture(e.pointerId)});setLogHeight($('console').getBoundingClientRect().height);
function closeErrorBalloon(){clearTimeout(errorBalloonTimer);$('errorBalloon').classList.add('hide')}function showErrorBalloon(message){if(!message)return;clearTimeout(errorBalloonTimer);$('errorBalloonText').textContent=message;$('errorBalloon').classList.remove('hide');errorBalloonTimer=setTimeout(closeErrorBalloon,10000)}
['discoveryMsg','cmsg'].forEach(id=>{let timer=0;const target=$(id);new MutationObserver(()=>{clearTimeout(timer);if(target.textContent)timer=setTimeout(()=>{target.textContent=''},5000)}).observe(target,{childList:true,characterData:true,subtree:true})});
function show(id){document.querySelectorAll('.page').forEach(page=>page.classList.add('hide'));$(id).classList.remove('hide');document.querySelectorAll('nav button').forEach(b=>{const c=b.getAttribute('onclick')||'';b.classList.toggle('active',c.includes("'"+id+"'"))});if(id==='sensors')renderSensorDetail();if(id==='mqtt')refreshMqtt()}
async function api(url,opt={}){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),10000);opt={...opt,signal:controller.signal,headers:{...(opt.headers||{}),'Content-Type':'application/json','X-CSRF-Token':csrf}};try{const response=await fetch(url,opt),document=await response.json().catch(()=>({error:response.statusText}));if(!response.ok)throw Error(document.error||'Request failed');if(document.operationId&&url==='/api/config'&&opt.method==='PUT'&&!opt.manualOperationWatch)setTimeout(()=>watchOperation(document.operationId,'cmsg',false),0);if(document.operationId&&url==='/api/config'&&opt.method==='POST'){sessionStorage.setItem('gateway.passwordOperationId',String(document.operationId));setTimeout(()=>watchOperation(document.operationId,'pmsg',true),0)}if(url==='/api/login'||(url==='/api/session'&&document.authenticated))setTimeout(resumePendingPasswordOperation,0);return document}finally{clearTimeout(timer)}}
async function login(){try{const response=await api('/api/login',{method:'POST',body:JSON.stringify({password:$('pw').value})});csrf=response.csrf;sessionEnded=false;$('login').classList.add('hide');$('app').classList.remove('hide');start()}catch(error){showErrorBalloon(error.message||'Request failed'); $('loginmsg').textContent=error.message;$('loginmsg').className='bad'}}async function logout(){await api('/api/logout',{method:'POST'}).catch(()=>{});location.reload()}
function sessionExpired(){sessionEnded=true;if(refreshTimer){clearInterval(refreshTimer);refreshTimer=0}clearTimeout(wsRetryTimer);clearInterval(wsHeartbeat);if(ws){ws.onclose=null;ws.onerror=null;ws.close();ws=null}$('app').classList.add('hide');$('login').classList.remove('hide');$('loginmsg').textContent='Session expired. Sign in again.';$('loginmsg').className='warn'}async function start(){show('sensors');await refresh();if(!refreshTimer)refreshTimer=setInterval(refresh,3000);connectLog()}
function appendLog(line){const match=line.match(/\"([^\"]*(?:Rejected WebUI|\[ERROR\])[^\"]*)\"/);if(match)showErrorBalloon(match[1]);else if(line.includes('[ERROR]'))showErrorBalloon(line);const log=$('log');log.textContent+=line+'\n';if(log.textContent.length>60000)log.textContent=log.textContent.slice(-50000);log.scrollTop=log.scrollHeight}function connectLog(){if(sessionEnded)return;clearTimeout(wsRetryTimer);clearInterval(wsHeartbeat);$('logState').textContent='Connecting';ws=new WebSocket('ws://'+location.host+'/ws?csrf='+encodeURIComponent(csrf));ws.onopen=()=>{$('logState').textContent='Connected';$('logState').className='ok';wsRetryDelay=1000;ws.send('ready');wsHeartbeat=setInterval(()=>{if(ws&&ws.readyState===WebSocket.OPEN)ws.send('ping')},15000)};ws.onmessage=event=>appendLog(event.data);ws.onerror=()=>{if(ws)ws.close()};ws.onclose=()=>{clearInterval(wsHeartbeat);if(sessionEnded)return;$('logState').textContent='Reconnecting';$('logState').className='warn';wsRetryTimer=setTimeout(connectLog,wsRetryDelay);wsRetryDelay=Math.min(wsRetryDelay*2,15000)}}
function ageText(value){const seconds=finite(value);if(seconds===null)return'Unavailable';if(seconds<60)return Math.floor(seconds)+'s ago';if(seconds<3600)return Math.floor(seconds/60)+'m ago';return Math.floor(seconds/3600)+'h ago'}function escapeHtml(value){return String(value).replace(/[&<>"']/g,char=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]))}
async function refreshMqtt(){try{const data=await api('/api/mqtt'),render=events=>events.length?events.map(event=>`<div class="mqtt-event ${event.direction==='TX'?'tx':'rx'}"><div class=mqtt-head><span class=badge>${escapeHtml(event.direction)}</span><span class=mqtt-topic>${escapeHtml(event.topic)}</span><span class=muted>${ageText(Math.max(0,data.uptime-event.uptime))}</span></div><pre class=mqtt-payload>${escapeHtml(event.payload)}</pre></div>`).join(''):'<p class=muted>No MQTT traffic captured yet.</p>',events=(data.events||[]).slice().reverse();$('mqttState').textContent=data.connected?'Connected':'Offline';$('mqttState').className=data.connected?'ok':'bad';$('mqttPublished').innerHTML=render(events.filter(event=>event.direction==='TX'));$('mqttReceived').innerHTML=render(events.filter(event=>event.direction!=='TX'))}catch(error){showErrorBalloon(error.message||'Request failed'); $('mqttState').textContent=error.message;$('mqttState').className='bad'}}
function temperatureState(value){if(value<35)return['Cold','#2d8cff'];if(value<50)return['Warming','#f0b43c'];if(value<=65)return['Ready','#45d16f'];return['Over temperature','#ef554f']}function renderGauge(value){const temperature=value===null||value===undefined||value===''?null:finite(value);if(temperature===null){$('tempGauge').textContent='Boiler temperature unavailable';return}const gaugeValue=Math.max(0,Math.min(100,temperature)),r=Math.PI-(gaugeValue/100*Math.PI),x=75+48*Math.cos(r),y=75-48*Math.sin(r),state=temperatureState(temperature);$('tempGauge').innerHTML=`<svg viewBox="0 0 150 95" width="300" height="190" role="img" aria-label="Water temperature ${temperature.toFixed(1)} degrees Celsius"><path d="M 15 75 A 60 60 0 0 1 47.8 21.5" fill="none" stroke="#2d8cff" stroke-width="13"/><path d="M 47.8 21.5 A 60 60 0 0 1 75 15" fill="none" stroke="#f0b43c" stroke-width="13"/><path d="M 75 15 A 60 60 0 0 1 102.2 21.5" fill="none" stroke="#45d16f" stroke-width="13"/><path d="M 102.2 21.5 A 60 60 0 0 1 135 75" fill="none" stroke="#ef554f" stroke-width="13"/><line x1="75" y1="75" x2="${x.toFixed(1)}" y2="${y.toFixed(1)}" stroke="#fff" stroke-width="4" stroke-linecap="round"/><circle cx="75" cy="75" r="7" fill="#fff" stroke="#0b1020" stroke-width="2"/></svg><div class=gauge-value>${temperature.toFixed(1)}&deg;C</div><div class=gauge-state style="color:${state[1]}">${state[0]}</div>`}
function renderSensorDetail(){const sensor=sensors[0],candidate=discoveredCandidate,discovered=!!(candidate&&candidate.mac),defaultCpu=$('defaultCpuFrequencyMhz'),cpuEligible=!!(sensor&&sensor.online&&sensor.mode&&Number.isFinite(Number(sensor.age))&&Number.isFinite(Number(sensor.sleep))&&Number(sensor.age)<Number(sensor.sleep)+5);if(defaultCpu){defaultCpu.disabled=!cpuEligible;defaultCpu.title=defaultCpu.disabled?'CPU settings require fresh telemetry from an online Sensor in Development Mode':''}$('sensorStatusBar').classList.toggle('hide',!discovered&&!(sensor&&sensor.pending));$('discoveredNotice').classList.toggle('hide',!discovered);$('pairDiscovered').classList.toggle('hide',!discovered);if(discovered){$('discoveredMac').textContent=candidate.mac;$('discoveredTrust').textContent=candidate.authenticated?'(authenticated)':'(unverified)'}if(!sensor){selectedMac='';$('sensorTitle').textContent='Sensor';$('configQueue').innerHTML='';$('sensorState').textContent='Unpaired / no telemetry';$('sensorState').className='badge bad';$('sensorPending').textContent='';renderGauge(null);['batteryValue','lastSeenValue','messageCountValue','sleepValue','modeValue','txPowerValue','cpuValue','signalValue','timeSyncValue'].forEach(id=>showValue(id,'Unavailable'));document.querySelectorAll('.debug-reading').forEach(card=>card.classList.remove('hide'));['debugCurrentValue','debugPowerValue','debugCpuValue','debugRamValue','debugLinkValue'].forEach(id=>showValue(id,'Unavailable'));return}selectedMac=sensor.mac||'';$('sensorTitle').textContent=sensor.mac?'Sensor '+sensor.mac:'Sensor';$('sensorState').textContent=sensor.online?'Online':'Offline / stale';$('sensorState').className='badge '+(sensor.online?'ok':'bad');$('sensorPending').textContent=sensor.pending?(sensor.configurationOrigin==='MQTT'?'MQTT configuration pending':'Configuration pending'):'';renderGauge(sensor.temperature);const voltage=finite(sensor.batteryVoltage),percent=finite(sensor.batteryPercent),rssi=finite(sensor.rssi),snr=finite(sensor.snr),cpu=finite(sensor.cpuFrequency);showValue('batteryValue',voltage===null||percent===null?'Unavailable':voltage.toFixed(2)+' V / '+percent.toFixed(0)+'%');showValue('lastSeenValue',ageText(sensor.age));showValue('messageCountValue',finite(sensor.messageCount)===null?'Unavailable':String(sensor.messageCount));showValue('sleepValue',finite(sensor.sleep)===null?'Unavailable':sensor.sleep+' seconds');showValue('modeValue',sensor.mode===undefined?'Unavailable':sensor.mode?'Debug':'Operation');showValue('txPowerValue',finite(sensor.txPower)===null?'Unavailable':sensor.txPower+' dBm');showValue('cpuValue',cpu===null?'Unavailable':cpu+' MHz');showValue('signalValue',rssi===null||snr===null?'Unavailable':rssi+' dBm / '+snr+' dB');showValue('timeSyncValue',sensor.needsTimeSync===undefined?'Unavailable':sensor.needsTimeSync?'Required':'Synchronized');const debug=!!sensor.debugAvailable;document.querySelectorAll('.debug-reading').forEach(card=>card.classList.remove('hide'));showValue('debugCurrentValue',debug&&finite(sensor.debugCurrentMa)!==null?sensor.debugCurrentMa+' mA':'Unavailable for Operation telemetry');showValue('debugPowerValue',debug&&finite(sensor.debugPowerMw)!==null?sensor.debugPowerMw+' mW':'Unavailable for Operation telemetry');showValue('debugCpuValue',debug&&finite(sensor.debugCpuTemp)!==null?sensor.debugCpuTemp+' °C':'Unavailable for Operation telemetry');showValue('debugRamValue',debug&&finite(sensor.debugFreeRam)!==null?sensor.debugFreeRam+' bytes':'Unavailable for Operation telemetry');showValue('debugLinkValue',debug&&finite(sensor.debugLastRssi)!==null&&finite(sensor.debugLastSnr)!==null?sensor.debugLastRssi+' dBm / '+sensor.debugLastSnr+' dB':'Unavailable for Operation telemetry');renderQueuedConfig(sensor);const cpuControl=$('scpu');if(cpuControl){cpuControl.disabled=!cpuEligible;cpuControl.title=cpuControl.disabled?'CPU settings require fresh telemetry from an online Sensor in Development Mode':''}}
function configCells(config){return'<td>'+escapeHtml(config.sleepSeconds===undefined?'':config.sleepSeconds)+'</td><td>'+escapeHtml(config.developmentMode===undefined?'':config.developmentMode?'Debug':'Operation')+'</td><td>'+escapeHtml(config.txPower===undefined?'':config.txPower)+'</td><td>'+escapeHtml(config.cpuFrequencyMhz===undefined?'':config.cpuFrequencyMhz)+'</td>'}function renderQueuedConfig(sensor){const queued=waitingCommands.filter(command=>command.mac===sensor.mac),current={sleepSeconds:sensor.sleep,developmentMode:sensor.mode,txPower:sensor.txPower,cpuFrequencyMhz:sensor.cpuFrequency},desired={sleepSeconds:sensor.desired?.sleep,developmentMode:sensor.desired?.mode,txPower:sensor.desired?.txPower,cpuFrequencyMhz:sensor.desired?.cpuFrequency},source=sensor.configurationOrigin==='WebUI'?'UI':(sensor.configurationOrigin||'Defaults'),input='<tr><td>New</td><td><input id=ssleep type=number min=10 max=255 placeholder="'+escapeHtml(sensor.sleep)+'"></td><td><select id=sdev><option value=""></option><option value=false>Operation</option><option value=true>Debug</option></select></td><td><input id=stx type=number min=2 max=15 placeholder="'+escapeHtml(sensor.txPower)+'"></td><td><select id=scpu><option value=""></option><option>80</option><option>160</option><option>240</option></select></td><td>UI</td></tr>';let rows='<tr><td>Current</td>'+configCells(current)+'<td>'+escapeHtml(source)+'</td></tr>';if(queued.length)rows+=queued.map((command,index)=>'<tr><td>'+String(index+1)+'</td>'+configCells(command)+'<td>UI</td></tr>').join('');if(sensor.pending)rows+='<tr><td>Pending</td>'+configCells(desired)+'<td>'+escapeHtml(source)+'</td></tr>';const table=$('configQueue'),existing=$('ssleep');if(!existing)table.innerHTML=input+rows;else{while(table.rows.length>1)table.deleteRow(1);table.insertAdjacentHTML('beforeend',rows)}}
async function reconcileRelay(){if(relayPendingTarget===null)return;try{relay={...relay,...await api('/api/relay')};relaySnapshotMs=Date.now();if(relay.failed&&relay.requestedClosed===relayPendingTarget){relayRevertedState=!relayPendingTarget;relayPendingTarget=null;showErrorBalloon('Relay did not reach the requested state.')}else if(relay.requestedClosed===relayPendingTarget){relayPendingTarget=null}else if(++relayPendingPolls<12){setTimeout(reconcileRelay,250)}else{relayRevertedState=!relayPendingTarget;relayPendingTarget=null;showErrorBalloon('Relay command was not confirmed; switch restored.')}renderGateway()}catch(error){showErrorBalloon(error.message||'Request failed'); if(++relayPendingPolls<12)setTimeout(reconcileRelay,250);else{relayRevertedState=!relayPendingTarget;relayPendingTarget=null;showErrorBalloon('Relay status could not be confirmed; switch restored.');renderGateway()}}}
async function toggleRelay(){if(relayPendingTarget!==null)return;const target=!(relayRevertedState===null?relay.requestedClosed:relayRevertedState);try{const result=await api('/api/relay',{method:'PUT',body:JSON.stringify({relayClosed:target})});if(result.status!=='queued')throw Error('Relay command was not queued');relayRevertedState=null;relayPendingTarget=target;relayPendingPolls=0;renderGateway();reconcileRelay();refresh()}catch(error){showErrorBalloon(error.message||'Request failed'); relayPendingTarget=null;renderGateway()}}
$('setRelayMax').addEventListener('click',async()=>{const value=Number($('srelaymax').value),button=$('setRelayMax');if(!Number.isInteger(value)||value<1||value>7200){showErrorBalloon('Failsafe timeout must be 1-7200 seconds.');return}button.disabled=true;try{const result=await api('/api/relay',{method:'PUT',body:JSON.stringify({relayMaxOnSeconds:value})});if(result.status!=='queued')throw Error('Failsafe timeout rejected.');await watchOperation(result.operationId,'relayTimeoutResult',false);await refresh();updateRadioProfilePanel()}catch(error){showErrorBalloon(error.message||'Request failed')}finally{button.disabled=false}});setInterval(()=>renderGateway(),1000);
function renderGateway(status){const pending=relayPendingTarget!==null,requested=pending?relayPendingTarget:(relayRevertedState===null?!!relay.requestedClosed:relayRevertedState),toggle=$('relayToggle');toggle.classList.toggle('on',requested);toggle.classList.toggle('pending',pending);toggle.disabled=pending;toggle.setAttribute('aria-pressed',requested?'true':'false');toggle.setAttribute('aria-busy',pending?'true':'false');let state=pending?'Pending confirmation':relay.failed?'Failed':relay.autoOff?'Automatic safety-off':requested?(relay.feedbackClosed?'Requested closed; feedback closed':'Requested closed; feedback open'):(relay.feedbackClosed?'Requested open; feedback closed':'Open');if(!relay.durable)state+='; persistence unavailable';toggle.setAttribute('aria-label',state);if(document.activeElement!==$('srelaymax'))$('srelaymax').value=finite(relay.maxOnSeconds)===null?'':relay.maxOnSeconds;const elapsed=relaySnapshotMs?Math.floor((Date.now()-relaySnapshotMs)/1000):0,remaining=Math.max(0,(finite(relay.remainingSeconds)||0)-elapsed);$('relayCountdown').textContent=requested&&!relay.failed&&!relay.autoOff&&remaining>0?'ON: '+remaining+' s remaining':'inactive';if(!status)return;$('sdFailure').classList.toggle('hide',status.sd!==false);const wifi=status.wifi||{},radio=status.radio||{},duty=status.duty||{};showValue('wifiValue',wifi.status||'Unavailable');showValue('ipValue',wifi.ip||'Unavailable');showValue('wifiRssiValue',finite(wifi.rssi)===null?'Unavailable':wifi.rssi+' dBm');showValue('mqttValue',status.mqtt===undefined?'Unavailable':status.mqtt?'Connected':'Offline');showValue('radioPhyValue',radio.available?radio.frequency+' MHz / BW '+radio.bandwidth+' kHz / SF'+radio.sf:'Unavailable');showValue('radioLinkValue',finite(radio.rssi)===null||finite(radio.snr)===null?'Unavailable':radio.rssi+' dBm / '+radio.snr+' dB');showValue('radioActivityValue',radio.activity||'Unavailable');showValue('burstValue',finite(duty.countedUs)===null?'Unavailable':(duty.countedUs/1000).toFixed(1)+' ms');showValue('cooldownValue',finite(duty.cooldownMs)===null?'Unavailable':duty.cooldownMs+' ms');showValue('minimumSleepValue',finite(duty.minimumSleep)===null?'Unavailable':duty.minimumSleep+' s');showValue('uptimeValue',finite(status.uptime)===null||status.uptime<=0?'Unavailable':status.uptime+' s');showValue('heapValue',finite(status.heap)===null?'Unavailable':status.heap+' bytes');showValue('sdValue',status.sd===undefined?'Unavailable':status.sd?'Ready':'Offline');$('gatewayRefreshState').textContent='Live';$('gatewayRefreshState').className='ok'}
async function refresh(){if(refreshActive||sessionEnded)return;refreshActive=true;try{const status=await api('/api/status');relay={...relay,...(status.relay||{})};relaySnapshotMs=Date.now();discoveredCandidate=status.discoveredSensor||null;renderGateway(status);const data=await api('/api/sensors');sensors=data.sensors||[];waitingCommands=data.waitingCommands||[];renderSensorDetail();updateRadioProfilePanel();renderGateway();if(!$('mqtt').classList.contains('hide'))await refreshMqtt()}catch(error){showErrorBalloon(error.message||'Request failed'); if(error.message==='unauthorized')sessionExpired();else if(error.name!=='AbortError'){$('gatewayRefreshState').textContent='Refresh delayed';$('gatewayRefreshState').className='warn'}}finally{refreshActive=false}}
async function sensorConfig(){const sensor=sensors[0];if(!sensor)return;try{const body={mac:sensor.mac};if($('ssleep').value)body.sleepSeconds=Number($('ssleep').value);if($('sdev').value)body.developmentMode=$('sdev').value==='true';if($('stx').value)body.txPower=Number($('stx').value);if($('scpu').value)body.cpuFrequencyMhz=Number($('scpu').value);await api('/api/sensors',{method:'PUT',body:JSON.stringify(body)});['ssleep','sdev','stx','scpu'].forEach(id=>$(id).value='');await refresh();updateRadioProfilePanel()}catch(error){showErrorBalloon(error.message||'Request failed')}}
async function loadConfig(){const button=$('saveConfigButton');button.disabled=true;$('cmsg').textContent='Loading settings...';$('cmsg').className='muted';try{const config=await api('/api/config');$('ssid').value=config.wifi.ssid;$('mhost').value=config.mqtt.host;$('mport').value=config.mqtt.port;$('muser').value=config.mqtt.user;$('pairedSensorMac').value=config.pairedSensorMac||'';button.disabled=false;$('cmsg').textContent='Settings loaded.';$('cmsg').className='ok'}catch(error){showErrorBalloon(error.message||'Request failed');$('cmsg').textContent='Settings could not be loaded; saving is disabled.';$('cmsg').className='bad'}}async function saveConfig(){if($('saveConfigButton').disabled)return;try{const body={wifi:{ssid:$('ssid').value},mqtt:{host:$('mhost').value,port:Number($('mport').value),user:$('muser').value},pairedSensorMac:$('pairedSensorMac').value};if($('wpass').value)body.wifi.password=$('wpass').value;if($('mpass').value)body.mqtt.password=$('mpass').value;const result=await api('/api/config',{method:'PUT',body:JSON.stringify(body)});$('cmsg').textContent='Queued operation '+result.operationId}catch(error){showErrorBalloon(error.message||'Request failed'); $('cmsg').textContent=error.message;$('cmsg').className='bad'}}async function changePassword(){try{await api('/api/config',{method:'POST',body:JSON.stringify({currentPassword:$('current').value,newPassword:$('newpw').value})});$('pmsg').textContent='Password changed; sign in again';setTimeout(()=>location.reload(),1200)}catch(error){showErrorBalloon(error.message||'Request failed'); $('pmsg').textContent=error.message}}
async function watchOperation(operationId,targetId,passwordOperation){const target=$(targetId);for(let attempt=0;attempt<40;attempt++){if(sessionEnded)return;try{const result=await api('/api/operation?id='+encodeURIComponent(operationId));if(result.status==='pending'){target.textContent='Operation '+operationId+' pending...';target.className='warn';await new Promise(resolve=>setTimeout(resolve,500));continue}target.textContent=result.message||'Operation '+result.status+'.';target.className=result.status==='succeeded'?'ok':result.status==='rejected'?'bad':'warn';if(result.status==='rejected')showErrorBalloon(target.textContent);if(passwordOperation)sessionStorage.removeItem('gateway.passwordOperationId');return result}catch(error){if(error.message==='unauthorized'){sessionExpired();return}if(attempt===39){target.textContent='Operation '+operationId+' result is still pending; retry after reconnecting.';target.className='warn';return}await new Promise(resolve=>setTimeout(resolve,500))}}}
async function pairDiscovered(){const button=$('pairDiscovered'),mac=$('discoveredMac').textContent;if(!mac||button.disabled)return;button.disabled=true;try{const result=await originalApi('/api/config',{method:'PUT',manualOperationWatch:true,body:JSON.stringify({pairedSensorMac:mac})});$('discoveryMsg').textContent='Pairing initiated, awaiting confirmation.';$('discoveryMsg').className='warn';await watchOperation(result.operationId,'discoveryMsg',false);await refresh()}catch(error){showErrorBalloon(error.message||'Request failed'); $('discoveryMsg').textContent=error.message;$('discoveryMsg').className='bad'}finally{button.disabled=false}}
function resumePendingPasswordOperation(){if(sessionEnded)return;const operationId=sessionStorage.getItem('gateway.passwordOperationId');if(operationId)watchOperation(operationId,'pmsg',true)}
document.querySelector('#settings .card h3').textContent='Gateway settings';document.querySelector('#settings .card').insertAdjacentHTML('beforeend','<label>Gateway radio TX power (dBm)<input id=gatewayTxPower type=number min=2 max=15></label><button id=setGatewayTxPower type=button>SET</button><span id=gatewayTxPowerResult class=muted></span>');document.querySelector('#settings .card').insertAdjacentHTML('afterend','<div id=sensorDefaultsSettings class=card><h3>Sensor defaults</h3><label>Default sleep interval (seconds)<input id=defaultSleepSeconds type=number min=10 max=255></label><label>Default mode<select id=defaultDevMode><option value=false>Operation</option><option value=true>Debug</option></select></label><label>Default TX power (dBm)<input id=defaultTxPower type=number min=2 max=15></label><label>Default CPU frequency (online Dev Mode only)<select id=defaultCpuFrequencyMhz><option value=80>80 MHz</option><option value=160>160 MHz</option><option value=240>240 MHz</option></select></label></div>');document.querySelector('#overview .card:nth-child(2)').insertAdjacentHTML('beforeend','<div class=reading>TX power<b id=radioTxPowerValue>Unavailable</b></div>');document.querySelector('#overview .card:nth-child(3)').insertAdjacentHTML('beforeend','<div class=reading>Rule / observed<b id=dutyRuleValue>Unavailable</b></div><div class=reading>Drift / non-compliant bursts<b id=dutyDriftValue>Unavailable</b></div>');
document.querySelector('#sensorDetail button[onclick="sensorConfig()"]').insertAdjacentHTML('afterend','<button id=useGatewayDefaultsButton onclick=useGatewayDefaults()>Use gateway defaults</button>');
setTimeout(()=>{const base=renderGateway;renderGateway=status=>{base(status);if(!status)return;const radio=status.radio||{},duty=status.duty||{},rate=finite(duty.nonCompliantBurstRatePercent),rule=finite(duty.rulePercent),drift=finite(duty.driftFromOnePercent),total=finite(duty.totalBurstCount),nonCompliant=finite(duty.nonCompliantBurstCount);showValue('radioTxPowerValue',radio.available&&finite(radio.txPowerDbm)!==null?radio.txPowerDbm+' dBm':'Unavailable');showValue('dutyRuleValue',rate===null||rule===null?'Unavailable':rule.toFixed(1)+'% / '+rate.toFixed(1)+'%'+(total===0?' (no bursts)':''));showValue('dutyDriftValue',drift===null||nonCompliant===null?'Unavailable':(drift>=0?'+':'')+drift.toFixed(1)+' pp / '+nonCompliant+(total===null?'':' of '+total))}},0);
document.querySelector('#sensorDetail .config-table').insertAdjacentHTML('afterend','<div class=card id=radioProfileBox><h3>Radio configuration</h3><p id=radioProfileModeMessage class=muted>Radio config available only in dev mode</p><div id=radioProfileControls class=hide><p id=currentRadioProfile class=muted></p><table class=config-table style="table-layout:fixed"><thead><tr><th>Preset</th><th>Spreading factor</th><th>Bandwidth (kHz)</th><th>Coding rate</th></tr></thead><tbody><tr><td><select id=radioProfilePreset disabled><option value="">Custom</option><option value=fast>Fast (SF7 / BW125 / CR4/5)</option><option value=default>Default (SF9 / BW125 / CR4/5)</option><option value=range>Range (SF10 / BW125 / CR4/5)</option></select></td><td><input id=radioProfileSf type=number min=7 max=12 disabled></td><td><select id=radioProfileBw disabled><option value="" selected>Current</option><option>7.8</option><option>10.4</option><option>15.6</option><option>20.8</option><option>31.25</option><option>41.7</option><option>62.5</option><option>125</option><option>250</option><option>500</option></select></td><td><select id=radioProfileCr disabled><option value="" selected>Current</option><option value=5>4/5</option><option value=6>4/6</option><option value=7>4/7</option><option value=8>4/8</option></select></td></tr></tbody></table><p id=radioProfilePreview class=muted></p><button id=applyRadioProfile type=button disabled>Apply radio profile</button></div></div>');
function profileAirtimeMs(sf,bw,cr,bytes){const de=(Math.pow(2,sf)/(bw*1000))>=0.016?1:0,tsym=Math.pow(2,sf)/(bw*1000),n=8*bytes-4*sf+28+16,payload=8+Math.max(Math.ceil(n/(4*(sf-2*de)))*cr,0);return(8+payload+4.25)*tsym*1000}function profileFitsEu868(bw){const half=bw/2000;return 868.300-half>=868.0&&868.300+half<=868.6}
function updateRadioProfilePanel(){const sensor=sensors[0],controls=$('radioProfileControls'),modeMessage=$('radioProfileModeMessage'),current=$('currentRadioProfile'),preview=$('radioProfilePreview'),button=$('applyRadioProfile');if(!controls||!modeMessage||!preview)return;if(!sensor){controls.classList.add('hide');modeMessage.classList.remove('hide');return}const devMode=!!sensor.mode;controls.classList.toggle('hide',!devMode);modeMessage.classList.toggle('hide',devMode);const enabled=!!(sensor.online&&devMode&&sensor.radioProfileAvailable&&!sensor.radioProfilePending);$('radioProfileSf').disabled=!enabled;$('radioProfileBw').disabled=!enabled;$('radioProfileCr').disabled=!enabled;$('radioProfilePreset').disabled=!enabled;button.disabled=!enabled;current.textContent=sensor.radioProfileAvailable?'Current radio: 868.300 MHz / SF'+sensor.radioProfileSf+', BW'+sensor.radioProfileBandwidthKhz+' kHz, CR4/'+sensor.radioProfileCodingRate:'Current radio: waiting for authenticated telemetry';if(!devMode)return;if(!sensor.radioProfileAvailable){preview.textContent='Waiting for authenticated radio profile telemetry.';return}if($('radioProfileSf').validity.badInput||($('radioProfileSf').value&&!$('radioProfileSf').validity.valid)){preview.textContent='Enter a spreading factor from 7 to 12.';button.disabled=true;return}const sf=Number($('radioProfileSf').value||sensor.radioProfileSf),bw=Number($('radioProfileBw').value||sensor.radioProfileBandwidthKhz),cr=Number($('radioProfileCr').value||sensor.radioProfileCodingRate);if(!profileFitsEu868(bw)){preview.textContent='Unsupported: occupied bandwidth falls outside 868.0-868.6 MHz.';button.disabled=true;return}const operationAir=profileAirtimeMs(sf,bw,cr,34),developmentAir=profileAirtimeMs(sf,bw,cr,45),ack=profileAirtimeMs(sf,bw,cr,10),operationSleep=Math.ceil(Math.max(operationAir,ack)*99/1000),developmentSleep=Math.ceil(Math.max(developmentAir,ack)*99/1000),currentDevelopmentAir=profileAirtimeMs(sensor.radioProfileSf,sensor.radioProfileBandwidthKhz,sensor.radioProfileCodingRate,45),delta=(developmentAir/currentDevelopmentAir-1)*100;if(operationSleep>255||developmentSleep>255){preview.innerHTML='Unsupported: calculated minimum sleep (op/dev) <strong>'+operationSleep+'/'+developmentSleep+' s</strong> exceeds the 255 s configuration limit.';button.disabled=true;return}const change=(delta>=0?'+':'')+delta.toFixed(1)+'%';const consumption=delta>0?'Estimated consumption increase: ':delta<0?'Estimated consumption decrease: ':'Estimated consumption change: ';preview.innerHTML='Estimated minimum sleep (op/dev): <strong>'+operationSleep+'/'+developmentSleep+' s</strong>. Payload Airtime increase: <strong>'+change+'</strong>. '+consumption+'<strong>'+change+'</strong> (TX).'}
['radioProfileSf','radioProfileBw','radioProfileCr'].forEach(id=>$(id).addEventListener('input',()=>{$('radioProfilePreset').value='';updateRadioProfilePanel()}));$('radioProfilePreset').addEventListener('change',()=>{const values={fast:[7,125,5],default:[9,125,5],range:[10,125,5]}[$('radioProfilePreset').value];if(values){$('radioProfileSf').value=values[0];$('radioProfileBw').value=values[1];$('radioProfileCr').value=values[2]}updateRadioProfilePanel()});$('applyRadioProfile').addEventListener('click',async()=>{const sensor=sensors[0];if(!sensor||!sensor.mode||!sensor.online)return;const button=$('applyRadioProfile');button.disabled=true;try{const preset=$('radioProfilePreset').value,body={mac:sensor.mac};if(preset)body.radioProfilePreset=preset;else{body.spreadingFactor=Number($('radioProfileSf').value||sensor.radioProfileSf);body.bandwidthKhz=Number($('radioProfileBw').value||sensor.radioProfileBandwidthKhz);body.codingRate=Number($('radioProfileCr').value||sensor.radioProfileCodingRate)}await api('/api/sensors',{method:'PUT',body:JSON.stringify(body)});showErrorBalloon('Radio profile queued; waiting for Sensor ACK and new-PHY confirmation.');await refresh()}catch(error){showErrorBalloon(error.message||'Radio profile request failed')}finally{updateRadioProfilePanel()}});
$('pairedSensorMac').placeholder='Paired remote Sensor MAC (AA:BB:CC:DD:EE:FF; blank to unpair)';
$('setGatewayTxPower').addEventListener('click',async()=>{const button=$('setGatewayTxPower'),result=$('gatewayTxPowerResult'),value=Number($('gatewayTxPower').value);if(!Number.isInteger(value)||value<2||value>15){showErrorBalloon('Gateway TX power must be 2-15 dBm.');return}button.disabled=true;try{const queued=await originalApi('/api/config',{method:'PUT',body:JSON.stringify({gateway:{radioTxPower:value}})});result.textContent='Queued operation '+queued.operationId;result.className='warn';const outcome=await watchOperation(queued.operationId,'gatewayTxPowerResult',false);if(outcome&&outcome.status==='succeeded')await loadConfig()}catch(error){showErrorBalloon(error.message||'Gateway TX power request failed');result.textContent=error.message;result.className='bad'}finally{button.disabled=false}});
async function useGatewayDefaults(){const button=$('useGatewayDefaultsButton');if(!selectedMac)return;button.disabled=true;try{const result=await api('/api/sensors',{method:'PUT',body:JSON.stringify({mac:selectedMac,inheritDefaults:true})});await refresh();return result}catch(error){showErrorBalloon(error.message||'Request failed')}finally{button.disabled=false}}
const originalApi=api;let mqttRefreshRequest=null,loadedDefaultCpu=null,loadedDefaultMode=null;api=async function(url,opt={}){if(url==='/api/mqtt'){if(!mqttRefreshRequest)mqttRefreshRequest=originalApi(url,opt).finally(()=>mqttRefreshRequest=null);return mqttRefreshRequest}if(url==='/api/config'&&(!opt.method||opt.method==='GET')){const result=await originalApi(url,opt),sensor=result.sensor||{},gateway=result.gateway||{};$('defaultSleepSeconds').value=sensor.defaultSleepSeconds;$('defaultDevMode').value=sensor.defaultDevMode?'true':'false';$('defaultTxPower').value=sensor.defaultTxPower;$('defaultCpuFrequencyMhz').value=sensor.defaultCpuFrequencyMhz;$('gatewayTxPower').value=gateway.radioTxPower;loadedDefaultCpu=sensor.defaultCpuFrequencyMhz;loadedDefaultMode=!!sensor.defaultDevMode;return result}if(url==='/api/config'&&opt.method==='PUT'){let body;try{body=JSON.parse(opt.body||'{}')}catch(error){showErrorBalloon(error.message||'Request failed'); throw Error('Invalid configuration request')}const nextCpu=Number($('defaultCpuFrequencyMhz').value),nextMode=$('defaultDevMode').value==='true',cpuChanged=nextCpu!==loadedDefaultCpu,modeChanged=nextMode!==loadedDefaultMode;if(cpuChanged&&modeChanged)throw Error('Change mode separately before CPU frequency');body.gateway={...(body.gateway||{}),radioTxPower:Number($('gatewayTxPower').value)};body.sensor={...(body.sensor||{}),defaultSleepSeconds:Number($('defaultSleepSeconds').value),defaultTxPower:Number($('defaultTxPower').value)};if(cpuChanged)body.sensor.defaultCpuFrequencyMhz=nextCpu;else body.sensor.defaultDevMode=nextMode;opt={...opt,body:JSON.stringify(body)}}return originalApi(url,opt)};
changePassword=async function(){try{await api('/api/config',{method:'POST',body:JSON.stringify({currentPassword:$('current').value,newPassword:$('newpw').value})});$('current').value='';$('newpw').value='';$('pmsg').textContent='Password change queued; awaiting Gateway confirmation.';$('pmsg').className='warn'}catch(error){showErrorBalloon(error.message||'Request failed'); $('pmsg').textContent=error.message;$('pmsg').className='bad'}};
(async()=>{try{const response=await api('/api/session');if(!response.authenticated)return;csrf=response.csrf;$('login').classList.add('hide');$('app').classList.remove('hide');start()}catch(error){showErrorBalloon(error.message||'Request failed'); }})();
</script></body></html>)HTML";

void randomHex(char* output, size_t bytes) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t value = static_cast<uint8_t>(esp_random());
        output[i * 2] = hex[value >> 4];
        output[i * 2 + 1] = hex[value & 0x0f];
    }
    output[bytes * 2] = '\0';
}

bool derivePassword(const char* password, const uint8_t* salt, uint8_t* output) {
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || mbedtls_md_setup(&context, info, 1) != 0) return false;
    int result = mbedtls_pkcs5_pbkdf2_hmac(&context,
        reinterpret_cast<const unsigned char*>(password), strlen(password), salt, 16,
        PBKDF2_ROUNDS, 32, output);
    mbedtls_md_free(&context);
    return result == 0;
}

bool persistPasswordVerifier(const uint8_t* salt, const uint8_t* hash, bool retainBootstrap) {
    Preferences prefs;
    if (!prefs.begin("gw-web", false)) return false;
    bool ok = prefs.putBytes("salt", salt, 16) == 16 &&
              prefs.putBytes("hash", hash, 32) == 32;
    if (!retainBootstrap) prefs.remove("bootstrap");
    prefs.end();
    return ok;
}

bool savePassword(const char* password, bool retainBootstrap) {
    uint8_t salt[sizeof(passwordSalt)] = {};
    uint8_t hash[sizeof(passwordHash)] = {};
    esp_fill_random(salt, sizeof(salt));
    if (!derivePassword(password, salt, hash) || !persistPasswordVerifier(salt, hash, retainBootstrap)) {
        memset(salt, 0, sizeof(salt));
        memset(hash, 0, sizeof(hash));
        return false;
    }
    memcpy(passwordSalt, salt, sizeof(passwordSalt));
    memcpy(passwordHash, hash, sizeof(passwordHash));
    memset(salt, 0, sizeof(salt));
    memset(hash, 0, sizeof(hash));
    return true;
}

bool loadOrCreatePassword(const char* configuredPassword) {
    if (configuredPassword && strlen(configuredPassword) >= 12) {
        memset(bootstrapPassword, 0, sizeof(bootstrapPassword));
        uint8_t salt[sizeof(passwordSalt)] = {};
        uint8_t hash[sizeof(passwordHash)] = {};
        esp_fill_random(salt, sizeof(salt));
        if (!derivePassword(configuredPassword, salt, hash)) return false;
        const bool persisted = persistPasswordVerifier(salt, hash, false);
        memcpy(passwordSalt, salt, sizeof(passwordSalt));
        memcpy(passwordHash, hash, sizeof(passwordHash));
        memset(salt, 0, sizeof(salt));
        memset(hash, 0, sizeof(hash));
        if (!persisted) Serial.println("[WEBUI] Warning: password verifier cache could not be saved; SD configuration remains authoritative.");
        return true;
    }
    Preferences prefs;
    if (!prefs.begin("gw-web", false)) return false;
    size_t saltLength = prefs.isKey("salt") ? prefs.getBytesLength("salt") : 0;
    size_t hashLength = prefs.isKey("hash") ? prefs.getBytesLength("hash") : 0;
    if (saltLength == sizeof(passwordSalt) && hashLength == sizeof(passwordHash)) {
        prefs.getBytes("salt", passwordSalt, sizeof(passwordSalt));
        prefs.getBytes("hash", passwordHash, sizeof(passwordHash));
        String saved = prefs.isKey("bootstrap") ? prefs.getString("bootstrap", "") : String();
        snprintf(bootstrapPassword, sizeof(bootstrapPassword), "%s", saved.c_str());
        prefs.end();
        return true;
    }
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    for (size_t i = 0; i < 20; ++i) bootstrapPassword[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    bootstrapPassword[20] = '\0';
    prefs.putString("bootstrap", bootstrapPassword);
    prefs.end();
    return savePassword(bootstrapPassword, true);
}

String headerValue(httpd_req_t* request, const char* name) {
    size_t length = httpd_req_get_hdr_value_len(request, name);
    if (!length || length > 256) return String();
    char buffer[257];
    if (httpd_req_get_hdr_value_str(request, name, buffer, sizeof(buffer)) != ESP_OK) return String();
    return String(buffer);
}

Session* authenticatedSession(httpd_req_t* request, bool requireCsrf) {
    String cookie = headerValue(request, "Cookie");
    int position = cookie.indexOf("GWHTTPSESSION=");
    if (position < 0) return nullptr;
    String token = cookie.substring(position + 14, position + 62);
    String csrf = requireCsrf ? headerValue(request, "X-CSRF-Token") : String();
    uint32_t now = millis();
    for (Session& item : sessions) {
        if (!item.token[0] || strcmp(item.token, token.c_str()) != 0) continue;
        if (now - item.lastSeenMs > SESSION_LIFETIME_MS || now - item.createdMs > 8UL * 60UL * 60UL * 1000UL) {
            revokeAllSessions();
            return nullptr;
        }
        if (requireCsrf && strcmp(item.csrf, csrf.c_str()) != 0) return nullptr;
        item.lastSeenMs = now;
        return &item;
    }
    return nullptr;
}

bool validOrigin(httpd_req_t* request) {
    String host = headerValue(request, "Host");
    String origin = headerValue(request, "Origin");
    return host.length() > 0 && origin == String("http://") + host;
}

esp_err_t jsonError(httpd_req_t* request, const char* status, const char* message) {
    httpd_resp_set_status(request, status); httpd_resp_set_type(request, "application/json");
    char response[128]; snprintf(response, sizeof(response), "{\"error\":\"%s\"}", message);
    return httpd_resp_sendstr(request, response);
}

bool readJson(httpd_req_t* request, StaticJsonDocument<768>& document) {
    if (request->content_len <= 0 || request->content_len > 767) return false;
    char body[768]; int received = httpd_req_recv(request, body, request->content_len);
    if (received != request->content_len) return false;
    body[received] = '\0'; return !deserializeJson(document, body);
}

esp_err_t sendDocument(httpd_req_t* request, JsonDocument& document) {
    String output; serializeJson(document, output); httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store"); return httpd_resp_send(request, output.c_str(), output.length());
}

esp_err_t rootHandler(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self' ws:");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(request, FINAL_PAGE, HTTPD_RESP_USE_STRLEN);
}

esp_err_t loginHandler(httpd_req_t* request) {
    WebUiActionLog action("login");
    if (!validOrigin(request)) return jsonError(request, "403 Forbidden", "invalid origin");
    uint32_t now = millis();
    if (now - loginWindowStarted > LOGIN_WINDOW_MS) { loginWindowStarted = now; loginFailures = 0; }
    if (loginFailures >= LOGIN_LIMIT) {
        action.setOutcome("rate-limited");
        return jsonError(request, "429 Too Many Requests", "login rate limited");
    }
    StaticJsonDocument<768> document;
    if (!readJson(request, document) || !document["password"].is<const char*>()) return jsonError(request, "400 Bad Request", "invalid request");
    uint8_t candidate[32] = {};
    bool valid = derivePassword(document["password"], passwordSalt, candidate) &&
                 secure_compare(candidate, passwordHash, sizeof(candidate));
    memset(candidate, 0, sizeof(candidate));
    if (!valid) {
        ++loginFailures;
        action.setOutcome("failed");
        return jsonError(request, "401 Unauthorized", "invalid credentials");
    }
    action.setOutcome("succeeded");
    loginFailures = 0;
    revokeAllSessions();
    Session* target = &sessions[0];
    for (Session& item : sessions) if (!item.token[0] || now - item.lastSeenMs > SESSION_LIFETIME_MS) { target = &item; break; }
    randomHex(target->token, 24); randomHex(target->csrf, 16); target->createdMs = now; target->lastSeenMs = now;
    char cookie[128]; snprintf(cookie, sizeof(cookie), "GWHTTPSESSION=%s; Path=/; HttpOnly; SameSite=Strict", target->token);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    StaticJsonDocument<96> response; response["csrf"] = target->csrf; return sendDocument(request, response);
}

esp_err_t sessionHandler(httpd_req_t* request) {
    Session* session = authenticatedSession(request, false);
    StaticJsonDocument<128> response;
    response["authenticated"] = session != nullptr;
    if (session) response["csrf"] = session->csrf;
    return sendDocument(request, response);
}

esp_err_t logoutHandler(httpd_req_t* request) {
    WebUiActionLog action("logout");
    if (!validOrigin(request)) return jsonError(request, "403 Forbidden", "invalid origin");
    Session* session = authenticatedSession(request, true);
    if (!session) return jsonError(request, "401 Unauthorized", "unauthorized");
    revokeAllSessions(); httpd_resp_set_hdr(request, "Set-Cookie", "GWHTTPSESSION=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    action.setOutcome("succeeded");
    return httpd_resp_sendstr(request, "{}");
}

esp_err_t statusHandler(httpd_req_t* request) {
    if (!authenticatedSession(request, false)) return jsonError(request, "401 Unauthorized", "unauthorized");
    GatewayWebSnapshot snapshot; xSemaphoreTake(stateMutex, portMAX_DELAY); snapshot = currentSnapshot; xSemaphoreGive(stateMutex);
    StaticJsonDocument<1536> document; document["uptime"] = snapshot.uptimeSeconds; document["heap"] = snapshot.freeHeap; document["cpuFrequencyMhz"] = snapshot.cpuFrequencyMhz; document["sd"] = snapshot.sdReady;
    JsonObject wifi = document.createNestedObject("wifi"); wifi["status"] = snapshot.wifiStatus; wifi["ip"] = snapshot.ip; wifi["rssi"] = snapshot.wifiRssi;
    document["mqtt"] = snapshot.mqttConnected; JsonObject radio = document.createNestedObject("radio"); radio["available"] = snapshot.radioAvailable; radio["profilePending"] = snapshot.radioProfilePending; radio["profileStatus"] = snapshot.radioProfileStatus;
    if (snapshot.radioAvailable) { radio["frequency"] = snapshot.radioFrequencyMhz; radio["bandwidth"] = snapshot.radioBandwidthKhz; radio["sf"] = snapshot.spreadingFactor; radio["cr"] = snapshot.codingRate; radio["txPowerDbm"] = snapshot.gatewayTxPower; radio["rssi"] = snapshot.lastRadioRssi; radio["snr"] = snapshot.lastRadioSnr; radio["activity"] = snapshot.gatewayCooldownRemainingMs > 0 ? "cooldown" : "listening"; }
    else { radio["frequency"] = nullptr; radio["bandwidth"] = nullptr; radio["sf"] = nullptr; radio["cr"] = nullptr; radio["txPowerDbm"] = nullptr; radio["rssi"] = nullptr; radio["snr"] = nullptr; radio["activity"] = nullptr; }
    JsonObject duty = document.createNestedObject("duty"); duty["countedUs"] = snapshot.countedBurstUs; duty["cooldownMs"] = snapshot.gatewayCooldownRemainingMs; duty["minimumSleep"] = snapshot.operationMinimumSleepSeconds; duty["rulePercent"] = 1.0f; duty["totalBurstCount"] = snapshot.totalBurstCount; duty["nonCompliantBurstCount"] = snapshot.nonCompliantBurstCount; duty["nonCompliantBurstRatePercent"] = snapshot.nonCompliantBurstRatePercent; duty["driftFromOnePercent"] = snapshot.driftFromOnePercent;
    JsonObject relay = document.createNestedObject("relay"); relay["requestedClosed"] = snapshot.relayRequestedClosed; relay["feedbackClosed"] = snapshot.relayFeedbackClosed; relay["failed"] = snapshot.relayFailed; relay["autoOff"] = snapshot.relayAutoOff; relay["durable"] = snapshot.relayDurable; relay["maxOnSeconds"] = snapshot.relayMaxOnSeconds; relay["remainingSeconds"] = snapshot.relayRemainingSeconds;
    if (snapshot.discoveredSensorMac[0]) { JsonObject discovered = document.createNestedObject("discoveredSensor"); discovered["mac"] = snapshot.discoveredSensorMac; discovered["authenticated"] = snapshot.discoveredSensorAuthenticated; }
    JsonObject remoteSensor = document.createNestedObject("remoteSensor");
    remoteSensor["paired"] = snapshot.sensorCount > 0;
    if (snapshot.sensorCount > 0) {
        const GatewayWebSensorSnapshot& sensor = snapshot.sensors[0];
        remoteSensor["mac"] = sensor.mac;
        remoteSensor["online"] = sensor.online;
        remoteSensor["lastSeenSeconds"] = sensor.lastSeenSeconds;
        remoteSensor["temperature"] = sensor.temperatureC;
        remoteSensor["batteryVoltage"] = sensor.batteryVoltage;
        remoteSensor["batteryPercent"] = sensor.batteryPercent;
        remoteSensor["radioProfileAvailable"] = sensor.radioProfileAvailable;
        if (sensor.radioProfileAvailable) {
            remoteSensor["radioProfileSf"] = sensor.radioProfileSf;
            remoteSensor["radioProfileBandwidthKhz"] = sensor.radioProfileBandwidthKhz;
            remoteSensor["radioProfileCodingRate"] = sensor.radioProfileCodingRate;
        }
    }
    return sendDocument(request, document);
}

esp_err_t relayHandler(httpd_req_t* request) {
    WebUiActionLog action(request->method == HTTP_GET ? nullptr : "relay configuration");
    if (!authenticatedSession(request, request->method != HTTP_GET)) return jsonError(request, "401 Unauthorized", "unauthorized");
    if (request->method == HTTP_GET) {
        GatewayWebSnapshot snapshot; xSemaphoreTake(stateMutex, portMAX_DELAY); snapshot = currentSnapshot; xSemaphoreGive(stateMutex);
        StaticJsonDocument<256> document; document["requestedClosed"] = snapshot.relayRequestedClosed; document["feedbackClosed"] = snapshot.relayFeedbackClosed; document["failed"] = snapshot.relayFailed; document["autoOff"] = snapshot.relayAutoOff; document["durable"] = snapshot.relayDurable; document["maxOnSeconds"] = snapshot.relayMaxOnSeconds; document["remainingSeconds"] = snapshot.relayRemainingSeconds;
        return sendDocument(request, document);
    }
    if (!validOrigin(request)) return jsonError(request, "403 Forbidden", "invalid origin");
    StaticJsonDocument<768> document; if (!readJson(request, document)) return jsonError(request, "400 Bad Request", "invalid JSON");
    GatewayWebCommand command{}; command.type = GatewayWebCommandType::RelayConfig;
    if (document.containsKey("relayClosed")) { if (!document["relayClosed"].is<bool>()) return jsonError(request, "400 Bad Request", "invalid relay state"); command.hasRelayClosed = true; command.relayClosed = document["relayClosed"]; }
    if (document.containsKey("relayMaxOnSeconds")) { if (!document["relayMaxOnSeconds"].is<int>()) return jsonError(request, "400 Bad Request", "invalid relay timeout"); int value = document["relayMaxOnSeconds"] | 0; if (value < 1 || value > 7200) return jsonError(request, "400 Bad Request", "invalid relay timeout"); command.hasRelayMaxOn = true; command.relayMaxOnSeconds = value; }
    if (!command.hasRelayClosed && !command.hasRelayMaxOn) return jsonError(request, "400 Bad Request", "no supported changes");
    if (!enqueue(command)) return jsonError(request, "503 Service Unavailable", "command queue full");
    action.setOutcome("queued");
    StaticJsonDocument<96> response; response["operationId"] = command.operationId; response["status"] = "queued"; return sendDocument(request, response);
}

esp_err_t mqttHandler(httpd_req_t* request) {
    if (!authenticatedSession(request, false)) return jsonError(request, "401 Unauthorized", "unauthorized");
    DynamicJsonDocument document(4096);
    if (document.capacity() == 0) return jsonError(request, "503 Service Unavailable", "insufficient memory for MQTT history");
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    do {
        document.clear();
        document["connected"] = currentSnapshot.mqttConnected;
        document["uptime"] = currentSnapshot.uptimeSeconds;
        JsonArray list = document.createNestedArray("events");
        const uint8_t oldest = (nextMqttEvent + MQTT_EVENT_COUNT - mqttEventUsed) % MQTT_EVENT_COUNT;
        for (uint8_t i = 0; i < mqttEventUsed; ++i) {
            const MqttEvent& event = mqttEvents[(oldest + i) % MQTT_EVENT_COUNT];
            JsonObject item = list.createNestedObject();
            item["sequence"] = event.sequence;
            item["uptime"] = event.uptimeSeconds;
            item["direction"] = String(event.direction);
            item["topic"] = String(event.topic);
            item["payload"] = String(event.payload);
        }
        if (!document.overflowed() || mqttEventUsed == 0) break;
        --mqttEventUsed;
    } while (true);
    xSemaphoreGive(stateMutex);
    if (document.overflowed()) return jsonError(request, "503 Service Unavailable", "insufficient memory for MQTT history");
    return sendDocument(request, document);
}

esp_err_t operationHandler(httpd_req_t* request) {
    if (!authenticatedSession(request, false)) return jsonError(request, "401 Unauthorized", "unauthorized");
    char query[32] = {};
    char value[16] = {};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", value, sizeof(value)) != ESP_OK) {
        return jsonError(request, "400 Bad Request", "operation id is required");
    }
    char* end = nullptr;
    const unsigned long parsedId = strtoul(value, &end, 10);
    if (value[0] == '\0' || !end || *end != '\0' || parsedId == 0 ||
        parsedId > UINT32_MAX) {
        return jsonError(request, "400 Bad Request", "invalid operation id");
    }

    GatewayWebOperationResult result{};
    StaticJsonDocument<192> document;
    document["operationId"] = static_cast<uint32_t>(parsedId);
    if (gatewayWebUi.getOperationResult(static_cast<uint32_t>(parsedId), result)) {
        document["status"] = result.status;
        document["message"] = result.message;
    } else {
        document["status"] = "unavailable";
        document["message"] = "Operation result is no longer available.";
    }
    return sendDocument(request, document);
}

bool enqueue(GatewayWebCommand& command) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (command.type == GatewayWebCommandType::SensorConfig) {
        for (uint8_t i = 0; i < commandUsed; ++i) {
            GatewayWebCommand& waiting = commands[(commandRead + i) % COMMAND_COUNT];
            if (waiting.type != GatewayWebCommandType::SensorConfig || strcmp(waiting.sensorMac, command.sensorMac) != 0) continue;
            command.operationId = nextOperationId++;
            waiting = command;
            xSemaphoreGive(stateMutex);
            return true;
        }
    }
    if (commandUsed == COMMAND_COUNT) { xSemaphoreGive(stateMutex); return false; }
    command.operationId = nextOperationId++;
    commands[commandWrite] = command; commandWrite = (commandWrite + 1) % COMMAND_COUNT; ++commandUsed;
    if (tracksOperationResult(command.type)) {
        storeOperationResultLocked(command.operationId, "pending", "Waiting for Gateway processing.");
    }
    xSemaphoreGive(stateMutex); return true;
}
esp_err_t sensorsHandler(httpd_req_t* request) {
    bool mutation = request->method != HTTP_GET;
    WebUiActionLog action(mutation ? (request->method == HTTP_DELETE ? "Sensor queue clear" : "Sensor configuration") : nullptr);
    if (mutation && !validOrigin(request)) return jsonError(request, "403 Forbidden", "invalid origin");
    if (!authenticatedSession(request, mutation)) return jsonError(request, "401 Unauthorized", "unauthorized");
    if (request->method == HTTP_DELETE) {
        StaticJsonDocument<768> body;
        if (!readJson(request, body)) return jsonError(request, "400 Bad Request", "invalid JSON");
        const char* mac = body["mac"] | "";
        if (!isValidSensorMac(mac)) return jsonError(request, "400 Bad Request", "invalid sensor MAC");
        uint8_t cleared = 0;
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        uint8_t kept = 0;
        for (uint8_t i = 0; i < commandUsed; ++i) {
            const uint8_t source = (commandRead + i) % COMMAND_COUNT;
            if (commands[source].type == GatewayWebCommandType::SensorConfig &&
                strcmp(commands[source].sensorMac, mac) == 0) {
                ++cleared;
            } else {
                commands[(commandRead + kept++) % COMMAND_COUNT] = commands[source];
            }
        }
        commandUsed = kept;
        commandWrite = (commandRead + kept) % COMMAND_COUNT;
        xSemaphoreGive(stateMutex);
        action.setOutcome("completed");
        StaticJsonDocument<96> response;
        response["cleared"] = cleared;
        response["scope"] = "waiting-only";
        return sendDocument(request, response);
    }
    if (mutation) {
        StaticJsonDocument<768> document; if (!readJson(request, document)) return jsonError(request, "400 Bad Request", "invalid JSON");
        for (JsonPairConst field : document.as<JsonObjectConst>()) {
            const char* key = field.key().c_str();
            if (strcmp(key, "mac") != 0 && strcmp(key, "inheritDefaults") != 0 &&
                strcmp(key, "sleepSeconds") != 0 && strcmp(key, "developmentMode") != 0 &&
                strcmp(key, "txPower") != 0 && strcmp(key, "cpuFrequencyMhz") != 0 &&
                strcmp(key, "spreadingFactor") != 0 && strcmp(key, "bandwidthKhz") != 0 &&
                strcmp(key, "codingRate") != 0 && strcmp(key, "radioProfilePreset") != 0) {
                return jsonError(request, "400 Bad Request", "unknown sensor configuration field");
            }
        }
        const char* mac = document["mac"] | "";
        if (!isValidSensorMac(mac)) return jsonError(request, "400 Bad Request", "invalid sensor MAC");
        GatewayWebCommand command{}; command.type = GatewayWebCommandType::SensorConfig; snprintf(command.sensorMac, sizeof(command.sensorMac), "%s", mac);
        if (document.containsKey("inheritDefaults")) {
            if (!document["inheritDefaults"].is<bool>() || !document["inheritDefaults"].as<bool>()) return jsonError(request, "400 Bad Request", "invalid inheritDefaults");
            command.inheritDefaults = true;
        }
        if (document.containsKey("sleepSeconds")) { if (!document["sleepSeconds"].is<int>()) return jsonError(request, "400 Bad Request", "invalid sleep interval"); int sleep = document["sleepSeconds"]; if (sleep < 10 || sleep > 255) return jsonError(request, "400 Bad Request", "invalid sleep interval"); command.hasSleep = true; command.sleepSeconds = sleep; }
        if (document.containsKey("developmentMode")) { if (!document["developmentMode"].is<bool>()) return jsonError(request, "400 Bad Request", "invalid development mode"); command.hasDevelopmentMode = true; command.developmentMode = document["developmentMode"]; }
        if (document.containsKey("txPower")) { if (!document["txPower"].is<int>()) return jsonError(request, "400 Bad Request", "invalid TX power"); int value = document["txPower"]; if (value < GATEWAY_SENSOR_TX_POWER_MIN_DBM || value > GATEWAY_SENSOR_TX_POWER_MAX_DBM || !isSupportedSensorTxPower(value)) return jsonError(request, "400 Bad Request", "invalid TX power"); command.hasTxPower = true; command.txPower = value; }
        if (document.containsKey("cpuFrequencyMhz")) { if (!document["cpuFrequencyMhz"].is<int>()) return jsonError(request, "400 Bad Request", "invalid CPU frequency"); int value = document["cpuFrequencyMhz"]; if (value != 80 && value != 160 && value != 240) return jsonError(request, "400 Bad Request", "invalid CPU frequency"); command.hasCpuFrequency = true; command.cpuFrequencyMhz = value; }
        const bool hasSf = document.containsKey("spreadingFactor");
        const bool hasBw = document.containsKey("bandwidthKhz");
        const bool hasCr = document.containsKey("codingRate");
        const bool hasPreset = document.containsKey("radioProfilePreset");
        if (hasPreset && (hasSf || hasBw || hasCr)) return jsonError(request, "400 Bad Request", "choose a preset or custom radio values");
        if ((hasSf || hasBw || hasCr) && !(hasSf && hasBw && hasCr)) return jsonError(request, "400 Bad Request", "radio profile requires SF, bandwidth, and coding rate");
        if (hasPreset) {
            if (!document["radioProfilePreset"].is<const char*>()) return jsonError(request, "400 Bad Request", "invalid radio profile preset");
            const char* preset = document["radioProfilePreset"];
            if (strcmp(preset, "fast") == 0) command.radioProfile = RADIO_PROFILE_FAST;
            else if (strcmp(preset, "default") == 0) command.radioProfile = RADIO_PROFILE_DEFAULT;
            else if (strcmp(preset, "range") == 0) command.radioProfile = RADIO_PROFILE_RANGE;
            else return jsonError(request, "400 Bad Request", "unknown radio profile preset");
            command.hasRadioProfile = true;
        } else if (hasSf) {
            if (!document["spreadingFactor"].is<int>() ||
                (!document["bandwidthKhz"].is<float>() && !document["bandwidthKhz"].is<int>()) ||
                !document["codingRate"].is<int>()) return jsonError(request, "400 Bad Request", "invalid radio profile");
            const int sf = document["spreadingFactor"].as<int>();
            const int cr = document["codingRate"].as<int>();
            const float bw = document["bandwidthKhz"].as<float>();
            if (sf < 7 || sf > 12 || cr < 5 || cr > 8) return jsonError(request, "400 Bad Request", "unsupported SF or coding rate");
            command.radioProfile.spreadingFactor = sf;
            command.radioProfile.codingRate = cr;
            command.radioProfile.bandwidthIndex = 0xFF;
            constexpr float bandwidths[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};
            for (uint8_t i = 0; i < sizeof(bandwidths) / sizeof(bandwidths[0]); ++i) {
                if (fabsf(bw - bandwidths[i]) < 0.01f) { command.radioProfile.bandwidthIndex = i; break; }
            }
            if (command.radioProfile.bandwidthIndex == 0xFF) return jsonError(request, "400 Bad Request", "unsupported bandwidth");
            command.hasRadioProfile = true;
        }
        if (command.inheritDefaults && (command.hasSleep || command.hasDevelopmentMode || command.hasTxPower || command.hasCpuFrequency || command.hasRadioProfile)) return jsonError(request, "400 Bad Request", "inheritDefaults must be used alone");
        if (command.hasDevelopmentMode && (command.hasCpuFrequency || command.hasRadioProfile)) return jsonError(request, "400 Bad Request", "change mode separately before Development-only settings");
        if (command.hasCpuFrequency || command.hasRadioProfile) {
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            const bool eligible = currentSnapshot.sensorCount && currentSnapshot.sensors[0].online &&
                currentSnapshot.sensors[0].developmentMode && currentSnapshot.sensors[0].mac[0] &&
                strcasecmp(currentSnapshot.sensors[0].mac, command.sensorMac) == 0;
            xSemaphoreGive(stateMutex);
            if (!eligible) return jsonError(request, "400 Bad Request", "Sensor must be online in Development Mode");
        }
        if (!command.inheritDefaults && !command.hasSleep && !command.hasDevelopmentMode && !command.hasTxPower && !command.hasCpuFrequency && !command.hasRadioProfile) return jsonError(request, "400 Bad Request", "no supported changes");
        if (!enqueue(command)) return jsonError(request, "503 Service Unavailable", "command queue full");
        action.setOutcome("queued");
        StaticJsonDocument<96> response; response["operationId"] = command.operationId; response["status"] = "queued"; return sendDocument(request, response);
    }
    GatewayWebSnapshot snapshot; xSemaphoreTake(stateMutex, portMAX_DELAY); snapshot = currentSnapshot; xSemaphoreGive(stateMutex);
    DynamicJsonDocument document(4096);
    if (document.capacity() == 0) return jsonError(request, "503 Service Unavailable", "insufficient memory for sensor snapshot");
    JsonArray list = document.createNestedArray("sensors");
    for (uint8_t i = 0; i < snapshot.sensorCount; ++i) {
        const auto& sensor = snapshot.sensors[i];
        JsonObject item = list.createNestedObject();
        item["mac"] = sensor.mac;
        item["online"] = sensor.online;
        item["age"] = sensor.lastSeenSeconds;
        item["onlineSeconds"] = sensor.onlineSeconds;
        item["temperature"] = sensor.temperatureC;
        item["batteryVoltage"] = sensor.batteryVoltage;
        item["batteryPercent"] = sensor.batteryPercent;
        item["messageCount"] = sensor.messageCount;
        item["sleep"] = sensor.sleepSeconds;
        item["mode"] = sensor.developmentMode;
        item["needsTimeSync"] = sensor.needsTimeSync;
        item["txPower"] = sensor.txPower;
        if (sensor.developmentMode) item["cpuFrequency"] = sensor.cpuFrequencyMhz;
        else item["cpuFrequency"] = nullptr;
        item["radioProfileAvailable"] = sensor.radioProfileAvailable;
        item["radioProfilePending"] = sensor.radioProfilePending;
        item["radioProfileStatus"] = sensor.radioProfileStatus;
        if (sensor.radioProfileAvailable) {
            item["radioProfileSf"] = sensor.radioProfileSf;
            item["radioProfileBandwidthKhz"] = sensor.radioProfileBandwidthKhz;
            item["radioProfileCodingRate"] = sensor.radioProfileCodingRate;
        }
        item["rssi"] = sensor.lastRssi;
        item["snr"] = sensor.lastSnr;
        item["debugAvailable"] = sensor.debugTelemetryAvailable;
        if (sensor.debugTelemetryAvailable) {
            item["debugCurrentMa"] = sensor.debugCurrentMa;
            item["debugPowerMw"] = sensor.debugPowerMw;
            item["debugCpuTemp"] = sensor.debugCpuTemp;
            item["debugFreeRam"] = sensor.debugFreeRam;
            item["debugLastSnr"] = sensor.debugLastSnr;
            item["debugLastRssi"] = sensor.debugLastRssi;
        }
        item["pending"] = sensor.configurationPending;
        item["confirmed"] = sensor.configurationConfirmed;
        item["mqttResultPending"] = sensor.mqttResultPending;
        item["configurationOrigin"] = sensor.configurationOrigin;
        JsonObject desired = item.createNestedObject("desired");
        desired["sleep"] = sensor.desiredSleepSeconds;
        desired["mode"] = sensor.desiredDevelopmentMode;
        desired["txPower"] = sensor.desiredTxPower;
        desired["cpuFrequency"] = sensor.desiredCpuFrequencyMhz;
    }
    JsonArray waiting = document.createNestedArray("waitingCommands");
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    for (uint8_t i = 0; i < commandUsed; ++i) {
        const GatewayWebCommand& command = commands[(commandRead + i) % COMMAND_COUNT];
        if (command.type != GatewayWebCommandType::SensorConfig) continue;
        JsonObject item = waiting.createNestedObject();
        item["operationId"] = command.operationId;
        item["mac"] = command.sensorMac;
        if (command.inheritDefaults) item["inheritDefaults"] = true;
        if (command.hasSleep) item["sleepSeconds"] = command.sleepSeconds;
        if (command.hasDevelopmentMode) item["developmentMode"] = command.developmentMode;
        if (command.hasTxPower) item["txPower"] = command.txPower;
        if (command.hasCpuFrequency) item["cpuFrequencyMhz"] = command.cpuFrequencyMhz;
    }
    xSemaphoreGive(stateMutex);
    if (document.overflowed()) return jsonError(request, "503 Service Unavailable", "sensor snapshot too large");
    return sendDocument(request, document);
}

esp_err_t configHandler(httpd_req_t* request) {
    bool mutation = request->method != HTTP_GET;
    char actionDescription[128] = "Gateway configuration";
    WebUiActionLog action(mutation ? (request->method == HTTP_POST ? "administrator password change" : actionDescription) : nullptr);
    if (mutation && !validOrigin(request)) return jsonError(request, "403 Forbidden", "invalid origin");
    if (!authenticatedSession(request, mutation)) return jsonError(request, "401 Unauthorized", "unauthorized");
    if (request->method == HTTP_GET) {
        GatewayWebConfigSnapshot config; xSemaphoreTake(stateMutex, portMAX_DELAY); config = currentConfig; xSemaphoreGive(stateMutex);
        StaticJsonDocument<512> document; JsonObject wifi = document.createNestedObject("wifi"); wifi["ssid"] = config.wifiSsid; wifi["passwordConfigured"] = config.wifiPasswordConfigured; JsonObject mqtt = document.createNestedObject("mqtt"); mqtt["host"] = config.mqttHost; mqtt["port"] = config.mqttPort; mqtt["user"] = config.mqttUser; mqtt["passwordConfigured"] = config.mqttPasswordConfigured; JsonObject gateway = document.createNestedObject("gateway"); gateway["radioTxPower"] = config.gatewayTxPower; JsonObject sensor = document.createNestedObject("sensor"); sensor["defaultSleepSeconds"] = config.defaultSleepSeconds; sensor["defaultDevMode"] = config.defaultDevMode; sensor["defaultTxPower"] = config.defaultTxPower; sensor["defaultCpuFrequencyMhz"] = config.defaultCpuFrequencyMhz; document["pairedSensorMac"] = config.pairedSensorMac; return sendDocument(request, document);
    }
    StaticJsonDocument<768> document; if (!readJson(request, document)) return jsonError(request, "400 Bad Request", "invalid JSON");
    GatewayWebCommand command{};
    if (request->method == HTTP_POST) {
        if (!document.containsKey("currentPassword") || !document.containsKey("newPassword") ||
            !document["currentPassword"].is<const char*>() || !document["newPassword"].is<const char*>()) {
            return jsonError(request, "400 Bad Request", "password must be text");
        }
        const char* current = document["currentPassword"] | ""; const char* replacement = document["newPassword"] | "";
        if (strlen(current) > 64 || strlen(replacement) < 12 || strlen(replacement) > 64) {
            return jsonError(request, "400 Bad Request", "password length is invalid");
        }
        uint8_t candidate[32] = {}; bool valid = strlen(replacement) >= 12 && derivePassword(current, passwordSalt, candidate) &&
            secure_compare(candidate, passwordHash, sizeof(candidate)); memset(candidate, 0, sizeof(candidate));
        if (!valid) return jsonError(request, "400 Bad Request", "password rejected");
        command.type = GatewayWebCommandType::ChangePassword;
        snprintf(command.currentPassword, sizeof(command.currentPassword), "%s", current);
        snprintf(command.newPassword, sizeof(command.newPassword), "%s", replacement);
        if (!enqueue(command)) return jsonError(request, "503 Service Unavailable", "command queue full");
        action.setOutcome("queued");
        StaticJsonDocument<96> response; response["operationId"] = command.operationId; response["status"] = "queued"; return sendDocument(request, response);
    }
    command.type = GatewayWebCommandType::GatewayConfig; JsonObject wifi = document["wifi"]; JsonObject mqtt = document["mqtt"]; JsonObject gateway = document["gateway"]; JsonObject sensor = document["sensor"];
    if (document.containsKey("gateway") && !document["gateway"].is<JsonObject>()) return jsonError(request, "400 Bad Request", "invalid Gateway configuration");
    if (document.containsKey("sensor") && !document["sensor"].is<JsonObject>()) return jsonError(request, "400 Bad Request", "invalid Sensor configuration");
    if ((!wifi.isNull() && wifi.containsKey("ssid") && !wifi["ssid"].is<const char*>()) ||
        (!mqtt.isNull() && mqtt.containsKey("host") && !mqtt["host"].is<const char*>()) ||
        (!mqtt.isNull() && mqtt.containsKey("user") && !mqtt["user"].is<const char*>())) {
        return jsonError(request, "400 Bad Request", "configuration value must be text");
    }
    if ((!wifi.isNull() && wifi.containsKey("password") && !wifi["password"].is<const char*>()) ||
        (!mqtt.isNull() && mqtt.containsKey("password") && !mqtt["password"].is<const char*>())) {
        return jsonError(request, "400 Bad Request", "password must be text");
    }
    const char* wifiSsid = wifi["ssid"] | ""; const char* wifiPassword = wifi["password"] | "";
    const char* mqttHost = mqtt["host"] | ""; const char* mqttUser = mqtt["user"] | ""; const char* mqttPassword = mqtt["password"] | "";
    if (strlen(wifiSsid) > 32 || strlen(wifiPassword) > 64 || strlen(mqttHost) > 128 || strlen(mqttUser) > 64 || strlen(mqttPassword) > 128) return jsonError(request, "400 Bad Request", "configuration value too long");
    if (!wifi.isNull() && wifi.containsKey("ssid")) { command.hasWifiSsid = true; snprintf(command.wifiSsid, sizeof(command.wifiSsid), "%s", wifiSsid); }
    if (!wifi.isNull() && wifi.containsKey("password")) { command.hasWifiPassword = true; snprintf(command.wifiPassword, sizeof(command.wifiPassword), "%s", wifiPassword); }
    if (!mqtt.isNull() && mqtt.containsKey("host")) { command.hasMqttHost = true; snprintf(command.mqttHost, sizeof(command.mqttHost), "%s", mqttHost); }
    if (!mqtt.isNull() && mqtt.containsKey("port")) { if (!mqtt["port"].is<int>()) return jsonError(request, "400 Bad Request", "invalid MQTT port"); int port = mqtt["port"] | 0; if (port < 1 || port > 65535) return jsonError(request, "400 Bad Request", "invalid MQTT port"); command.hasMqttPort = true; command.mqttPort = port; }
    if (!mqtt.isNull() && mqtt.containsKey("user")) { command.hasMqttUser = true; snprintf(command.mqttUser, sizeof(command.mqttUser), "%s", mqttUser); }
    if (!mqtt.isNull() && mqtt.containsKey("password")) { command.hasMqttPassword = true; snprintf(command.mqttPassword, sizeof(command.mqttPassword), "%s", mqttPassword); }
    if (gateway.containsKey("radioTxPower")) { if (!gateway["radioTxPower"].is<int>()) return jsonError(request, "400 Bad Request", "invalid Gateway TX power"); int value = gateway["radioTxPower"]; if (value < GATEWAY_SENSOR_TX_POWER_MIN_DBM || value > GATEWAY_SENSOR_TX_POWER_MAX_DBM || !isSupportedSensorTxPower(value)) return jsonError(request, "400 Bad Request", "invalid Gateway TX power"); command.hasGatewayTxPower = true; command.gatewayTxPower = value; }
    if (sensor.containsKey("defaultSleepSeconds")) { if (!sensor["defaultSleepSeconds"].is<int>()) return jsonError(request, "400 Bad Request", "invalid default sleep interval"); int value = sensor["defaultSleepSeconds"]; if (value < 10 || value > UINT8_MAX) return jsonError(request, "400 Bad Request", "invalid default sleep interval"); command.hasDefaultSleep = true; command.defaultSleepSeconds = value; }
    if (sensor.containsKey("defaultDevMode")) { if (!sensor["defaultDevMode"].is<bool>()) return jsonError(request, "400 Bad Request", "invalid default mode"); command.hasDefaultDevelopmentMode = true; command.defaultDevelopmentMode = sensor["defaultDevMode"]; }
    if (sensor.containsKey("defaultTxPower")) { if (!sensor["defaultTxPower"].is<int>()) return jsonError(request, "400 Bad Request", "invalid default TX power"); int value = sensor["defaultTxPower"]; if (value < GATEWAY_SENSOR_TX_POWER_MIN_DBM || value > GATEWAY_SENSOR_TX_POWER_MAX_DBM || !isSupportedSensorTxPower(value)) return jsonError(request, "400 Bad Request", "invalid default TX power"); command.hasDefaultTxPower = true; command.defaultTxPower = value; }
    if (sensor.containsKey("defaultCpuFrequencyMhz")) { if (!sensor["defaultCpuFrequencyMhz"].is<int>()) return jsonError(request, "400 Bad Request", "invalid default CPU frequency"); int value = sensor["defaultCpuFrequencyMhz"]; if (value != 80 && value != 160 && value != 240) return jsonError(request, "400 Bad Request", "invalid default CPU frequency"); command.hasDefaultCpuFrequency = true; command.defaultCpuFrequencyMhz = value; }
    const char* pairedMac = document["pairedSensorMac"] | (sensor["pairedMac"] | "");
    if (document.containsKey("pairedSensorMac") || (!sensor.isNull() && sensor.containsKey("pairedMac"))) {
        if ((document.containsKey("pairedSensorMac") && !document["pairedSensorMac"].is<const char*>()) ||
            (!document.containsKey("pairedSensorMac") && !sensor["pairedMac"].is<const char*>()))
            return jsonError(request, "400 Bad Request", "invalid paired Sensor MAC");
        if (pairedMac[0] && !isValidSensorMac(pairedMac)) return jsonError(request, "400 Bad Request", "invalid paired Sensor MAC");
        command.hasPairedSensorMac = true; snprintf(command.pairedSensorMac, sizeof(command.pairedSensorMac), "%s", pairedMac);
    }
    if (command.hasGatewayTxPower &&
        (command.hasWifiSsid || command.hasWifiPassword || command.hasMqttHost || command.hasMqttPort ||
         command.hasMqttUser || command.hasMqttPassword || command.hasDefaultSleep ||
         command.hasDefaultDevelopmentMode || command.hasDefaultTxPower || command.hasDefaultCpuFrequency ||
         command.hasPairedSensorMac)) {
        command.hasGatewayTxPower = false;
    }
    if (!command.hasWifiSsid && !command.hasWifiPassword && !command.hasMqttHost && !command.hasMqttPort && !command.hasMqttUser && !command.hasMqttPassword && !command.hasGatewayTxPower && !command.hasDefaultSleep && !command.hasDefaultDevelopmentMode && !command.hasDefaultTxPower && !command.hasDefaultCpuFrequency && !command.hasPairedSensorMac) return jsonError(request, "400 Bad Request", "no supported changes");
    snprintf(actionDescription, sizeof(actionDescription), "Gateway settings:");
    size_t actionDescriptionLength = strlen(actionDescription);
    bool hasActionDescription = false;
    if (command.hasWifiSsid || command.hasWifiPassword) {
        actionDescriptionLength += snprintf(actionDescription + actionDescriptionLength,
                                            sizeof(actionDescription) - actionDescriptionLength,
                                            "%s Wi-Fi settings", hasActionDescription ? "," : "");
        hasActionDescription = true;
    }
    if (command.hasMqttHost || command.hasMqttPort || command.hasMqttUser || command.hasMqttPassword) {
        actionDescriptionLength += snprintf(actionDescription + actionDescriptionLength,
                                            sizeof(actionDescription) - actionDescriptionLength,
                                            "%s MQTT settings", hasActionDescription ? "," : "");
        hasActionDescription = true;
    }
    if (command.hasGatewayTxPower) {
        actionDescriptionLength += snprintf(actionDescription + actionDescriptionLength,
                                            sizeof(actionDescription) - actionDescriptionLength,
                                            "%s Gateway radio TX power", hasActionDescription ? "," : "");
        hasActionDescription = true;
    }
    if (command.hasDefaultSleep || command.hasDefaultDevelopmentMode ||
        command.hasDefaultTxPower || command.hasDefaultCpuFrequency) {
        actionDescriptionLength += snprintf(actionDescription + actionDescriptionLength,
                                            sizeof(actionDescription) - actionDescriptionLength,
                                            "%s Sensor defaults", hasActionDescription ? "," : "");
        hasActionDescription = true;
    }
    if (command.hasPairedSensorMac) {
        snprintf(actionDescription + actionDescriptionLength,
                 sizeof(actionDescription) - actionDescriptionLength,
                 "%s Sensor pairing", hasActionDescription ? "," : "");
    }
    if (!enqueue(command)) return jsonError(request, "503 Service Unavailable", "command queue full");
    action.setOutcome("queued");
    StaticJsonDocument<96> response; response["operationId"] = command.operationId; response["status"] = "queued"; return sendDocument(request, response);
}

esp_err_t wsHandler(httpd_req_t* request) {
    if (request->method == HTTP_GET) {
        if (!validOrigin(request)) return ESP_FAIL;
        char query[96] = {}; char csrf[40] = {};
        if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK || httpd_query_key_value(query, "csrf", csrf, sizeof(csrf)) != ESP_OK) return ESP_FAIL;
        Session* session = authenticatedSession(request, false); if (!session || strcmp(session->csrf, csrf) != 0) return ESP_FAIL;
        // The HTTP upgrade is completed only after this GET handler returns.
        // The browser sends "ready" once open, and the established-frame path
        // below then sends the bounded backlog.
        return ESP_OK;
    }

    httpd_ws_frame_t received{};
    esp_err_t result = httpd_ws_recv_frame(request, &received, 0);
    if (result != ESP_OK) return result;
    if (received.len > 16) return ESP_ERR_INVALID_SIZE;
    uint8_t payload[17] = {};
    received.payload = payload;
    result = httpd_ws_recv_frame(request, &received, sizeof(payload) - 1);
    if (result != ESP_OK) return result;
    payload[received.len] = '\0';
    if (received.type != HTTPD_WS_TYPE_TEXT || strcmp(reinterpret_cast<char*>(payload), "ready") != 0) return ESP_OK;

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    uint8_t start = (nextLogLine + LOG_LINE_COUNT - logLineUsed) % LOG_LINE_COUNT;
    uint8_t count = logLineUsed;
    xSemaphoreGive(stateMutex);
    for (uint8_t i = 0; i < count; ++i) {
        char line[LOG_LINE_LENGTH];
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        snprintf(line, sizeof(line), "%s", logLines[(start + i) % LOG_LINE_COUNT]);
        xSemaphoreGive(stateMutex);
        httpd_ws_frame_t outgoing{};
        outgoing.type = HTTPD_WS_TYPE_TEXT;
        outgoing.payload = reinterpret_cast<uint8_t*>(line);
        outgoing.len = strlen(line);
        if (httpd_ws_send_frame(request, &outgoing) != ESP_OK) break;
    }
    return ESP_OK;
}

void broadcastPendingLogs(void*) {
    uint32_t target;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    target = totalLogLines;
    if (target - lastBroadcastLine > LOG_LINE_COUNT) lastBroadcastLine = target - LOG_LINE_COUNT;
    xSemaphoreGive(stateMutex);

    while (lastBroadcastLine < target) {
        char outbound[LOG_LINE_LENGTH];
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        snprintf(outbound, sizeof(outbound), "%s", logLines[lastBroadcastLine % LOG_LINE_COUNT]);
        ++lastBroadcastLine;
        xSemaphoreGive(stateMutex);

        size_t clients = CONFIG_LWIP_MAX_SOCKETS;
        int sockets[CONFIG_LWIP_MAX_SOCKETS];
        if (server && httpd_get_client_list(server, &clients, sockets) == ESP_OK) {
            for (size_t i = 0; i < clients; ++i) {
                if (httpd_ws_get_fd_info(server, sockets[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
                httpd_ws_frame_t frame{}; frame.type = HTTPD_WS_TYPE_TEXT;
                frame.payload = reinterpret_cast<uint8_t*>(outbound); frame.len = strlen(outbound);
                httpd_ws_send_frame_async(server, sockets[i], &frame);
            }
        }
    }

    bool queueAgain = false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    websocketWorkPending = false;
    if (server && lastBroadcastLine < totalLogLines) { websocketWorkPending = true; queueAgain = true; }
    xSemaphoreGive(stateMutex);
    if (queueAgain && httpd_queue_work(server, broadcastPendingLogs, nullptr) != ESP_OK) {
        xSemaphoreTake(stateMutex, portMAX_DELAY); websocketWorkPending = false; xSemaphoreGive(stateMutex);
    }
}
}

WebUi gatewayWebUi;

void WebUi::setActionLogger(ActionLogger logger) { actionLogger = logger; }
void WebUi::setCommandHandler(CommandHandler handler) { commandHandler = handler; }
void WebUi::setSnapshotProvider(SnapshotProvider provider) { snapshotProvider = provider; }
void WebUi::logAction(const char* operation, const char* outcome) const {
    if (actionLogger && operation && outcome) actionLogger(operation, outcome);
}

bool WebUi::begin(const char* adminPassword) {
    if (server) return true;
    if (!stateMutex) stateMutex = xSemaphoreCreateMutex();
    if (!stateMutex || !loadOrCreatePassword(adminPassword)) return false;
    return startServer();
}

bool WebUi::startServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.max_open_sockets = 2; config.max_uri_handlers = 16; config.stack_size = 10240;
    if (httpd_start(&server, &config) != ESP_OK) { server = nullptr; return false; }
    httpd_uri_t routes[] = {
        {"/", HTTP_GET, rootHandler, nullptr, false}, {"/api/login", HTTP_POST, loginHandler, nullptr, false},
        {"/api/session", HTTP_GET, sessionHandler, nullptr, false}, {"/api/logout", HTTP_POST, logoutHandler, nullptr, false},
        {"/api/status", HTTP_GET, statusHandler, nullptr, false}, {"/api/relay", HTTP_GET, relayHandler, nullptr, false}, {"/api/relay", HTTP_PUT, relayHandler, nullptr, false}, {"/api/mqtt", HTTP_GET, mqttHandler, nullptr, false},
        {"/api/operation", HTTP_GET, operationHandler, nullptr, false},
        {"/api/sensors", HTTP_GET, sensorsHandler, nullptr, false},
        {"/api/sensors", HTTP_PUT, sensorsHandler, nullptr, false}, {"/api/sensors", HTTP_DELETE, sensorsHandler, nullptr, false},
        {"/api/config", HTTP_GET, configHandler, nullptr, false},
        {"/api/config", HTTP_PUT, configHandler, nullptr, false}, {"/api/config", HTTP_POST, configHandler, nullptr, false}
    };
    for (auto& route : routes) if (httpd_register_uri_handler(server, &route) != ESP_OK) { stop(); return false; }
    httpd_uri_t websocket{"/ws", HTTP_GET, wsHandler, nullptr, true};
    if (httpd_register_uri_handler(server, &websocket) != ESP_OK) { stop(); return false; }
    return true;
}

void WebUi::stop() { if (server) { httpd_stop(server); server = nullptr; } }
bool WebUi::running() const { return server != nullptr; }
void WebUi::service() {
    if (!running()) return;

    GatewayWebCommand command;
    if (pollCommand(command) && commandHandler) commandHandler(command);

    static uint32_t lastSnapshotMs = 0;
    if (!snapshotProvider || millis() - lastSnapshotMs < 2000) return;
    lastSnapshotMs = millis();

    GatewayWebSnapshot snapshot{};
    GatewayWebConfigSnapshot config{};
    snapshotProvider(snapshot, config);
    updateSnapshot(snapshot);
    updateConfigSnapshot(config);
}
void WebUi::updateSnapshot(const GatewayWebSnapshot& snapshot) { if (!stateMutex) return; xSemaphoreTake(stateMutex, portMAX_DELAY); currentSnapshot = snapshot; xSemaphoreGive(stateMutex); }
void WebUi::updateConfigSnapshot(const GatewayWebConfigSnapshot& snapshot) { if (!stateMutex) return; xSemaphoreTake(stateMutex, portMAX_DELAY); currentConfig = snapshot; xSemaphoreGive(stateMutex); }
bool WebUi::pollCommand(GatewayWebCommand& command) { if (!stateMutex) return false; xSemaphoreTake(stateMutex, portMAX_DELAY); if (!commandUsed) { xSemaphoreGive(stateMutex); return false; } command = commands[commandRead]; commandRead = (commandRead + 1) % COMMAND_COUNT; --commandUsed; xSemaphoreGive(stateMutex); return true; }
void WebUi::updateOperationResult(uint32_t operationId, const char* status, const char* message) {
    if (!stateMutex || operationId == 0 || !status || !message) return;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    for (uint8_t i = 0; i < operationResultUsed; ++i) {
        GatewayWebOperationResult& result = operationResults[i];
        if (result.operationId != operationId) continue;
        snprintf(result.status, sizeof(result.status), "%s", status);
        snprintf(result.message, sizeof(result.message), "%s", message);
        break;
    }
    xSemaphoreGive(stateMutex);
}
bool WebUi::getOperationResult(uint32_t operationId, GatewayWebOperationResult& result) const {
    if (!stateMutex || operationId == 0) return false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    for (uint8_t i = 0; i < operationResultUsed; ++i) {
        const GatewayWebOperationResult& candidate = operationResults[i];
        if (candidate.operationId != operationId) continue;
        result = candidate;
        xSemaphoreGive(stateMutex);
        return true;
    }
    xSemaphoreGive(stateMutex);
    return false;
}
uint32_t WebUi::queuedCommandBoundary() const { if (!stateMutex) return 0; xSemaphoreTake(stateMutex, portMAX_DELAY); const uint32_t boundary = nextOperationId; xSemaphoreGive(stateMutex); return boundary; }
uint8_t WebUi::discardOlderSensorConfigs(const char* mac, uint32_t boundary) {
    if (!stateMutex || !mac) return 0;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    uint8_t discarded = 0;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < commandUsed; ++i) {
        const uint8_t source = (commandRead + i) % COMMAND_COUNT;
        if (commands[source].type == GatewayWebCommandType::SensorConfig &&
            strcmp(commands[source].sensorMac, mac) == 0 && commands[source].operationId < boundary) {
            ++discarded;
        } else {
            commands[(commandRead + kept++) % COMMAND_COUNT] = commands[source];
        }
    }
    commandUsed = kept;
    commandWrite = (commandRead + kept) % COMMAND_COUNT;
    xSemaphoreGive(stateMutex);
    return discarded;
}
void WebUi::appendLogLine(const char* line) {
    if (!stateMutex || !line) return;
    bool schedule = false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    snprintf(logLines[nextLogLine], LOG_LINE_LENGTH, "%s", line);
    nextLogLine = (nextLogLine + 1) % LOG_LINE_COUNT;
    if (logLineUsed < LOG_LINE_COUNT) ++logLineUsed;
    ++totalLogLines;
    if (server && !websocketWorkPending) { websocketWorkPending = true; schedule = true; }
    xSemaphoreGive(stateMutex);
    if (schedule && httpd_queue_work(server, broadcastPendingLogs, nullptr) != ESP_OK) {
        xSemaphoreTake(stateMutex, portMAX_DELAY); websocketWorkPending = false; xSemaphoreGive(stateMutex);
    }
}

bool isSensitiveMqttField(const char* name) {
    if (!name) return false;
    return strcasecmp(name, "signature") == 0 || strcasecmp(name, "password") == 0 ||
           strcasecmp(name, "adminPassword") == 0 || strcasecmp(name, "admin_password") == 0 ||
           strcasecmp(name, "secret") == 0 || strcasecmp(name, "commandSecret") == 0 ||
           strcasecmp(name, "client_secret") == 0 || strcasecmp(name, "wifiPassword") == 0 ||
           strcasecmp(name, "mqttPassword") == 0 || strcasecmp(name, "token") == 0 ||
           strcasecmp(name, "accessToken") == 0 || strcasecmp(name, "access_token") == 0 ||
           strcasecmp(name, "refresh_token") == 0 || strcasecmp(name, "id_token") == 0 ||
           strcasecmp(name, "apiKey") == 0 || strcasecmp(name, "api_key") == 0 ||
           strcasecmp(name, "authorization") == 0 || strcasecmp(name, "credential") == 0 ||
           strcasecmp(name, "credentials") == 0 || strcasecmp(name, "privateKey") == 0 ||
           strcasecmp(name, "private_key") == 0;
}

void redactMqttJson(JsonVariant value) {
    if (value.is<JsonObject>()) {
        JsonObject object = value.as<JsonObject>();
        for (JsonPair field : object) {
            if (isSensitiveMqttField(field.key().c_str())) {
                field.value().set("[redacted]");
            } else if (field.value().is<JsonArray>() || field.value().is<JsonObject>()) {
                redactMqttJson(field.value());
            }
        }
        return;
    }
    if (value.is<JsonArray>()) {
        JsonArray array = value.as<JsonArray>();
        for (JsonVariant item : array) {
            if (item.is<JsonObject>() || item.is<JsonArray>()) {
                redactMqttJson(item);
            } else {
                item.set("[redacted]");
            }
        }
    }
}

void WebUi::appendMqttEvent(const char* direction, const char* topic, const char* payload, size_t payloadLength) {
    if (!stateMutex || !direction || !topic || !payload) return;
    const size_t inputLength = payloadLength ? payloadLength : strlen(payload);
    String sanitizedPayload;
    sanitizedPayload.reserve(MQTT_PAYLOAD_LENGTH);
    DynamicJsonDocument document(4096);
    DeserializationError jsonError = deserializeJson(document, payload, inputLength);
    if (!jsonError && document.is<JsonObject>()) {
        redactMqttJson(document.as<JsonVariant>());
        if (!document.overflowed()) serializeJson(document, sanitizedPayload);
        else sanitizedPayload = "[unavailable: diagnostic redaction exceeded capacity]";
    } else {
        sanitizedPayload = "[unavailable: non-JSON payload, ";
        sanitizedPayload += static_cast<unsigned int>(inputLength);
        sanitizedPayload += " bytes]";
    }
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    MqttEvent& event = mqttEvents[nextMqttEvent];
    event.sequence = ++totalMqttEvents;
    event.uptimeSeconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    snprintf(event.direction, sizeof(event.direction), "%s", direction);
    snprintf(event.topic, sizeof(event.topic), "%s", topic);
    snprintf(event.payload, sizeof(event.payload), "%s", sanitizedPayload.c_str());
    if (sanitizedPayload.length() >= sizeof(event.payload)) {
        constexpr char marker[] = "...[truncated]";
        memcpy(event.payload + sizeof(event.payload) - sizeof(marker), marker, sizeof(marker));
    }
    nextMqttEvent = (nextMqttEvent + 1) % MQTT_EVENT_COUNT;
    if (mqttEventUsed < MQTT_EVENT_COUNT) ++mqttEventUsed;
    xSemaphoreGive(stateMutex);
}
const char* WebUi::bootstrapCredential() const { return bootstrapPassword; }
bool WebUi::prepareAdminPassword(const char* password) {
    discardPreparedAdminPassword();
    if (!password || strlen(password) < 12 || strlen(password) > 64) return false;
    esp_fill_random(preparedPasswordSalt, sizeof(preparedPasswordSalt));
    if (!derivePassword(password, preparedPasswordSalt, preparedPasswordHash)) {
        discardPreparedAdminPassword();
        return false;
    }
    passwordPrepared = true;
    return true;
}

bool WebUi::commitPreparedAdminPassword(bool& verifierPersisted) {
    if (!passwordPrepared) return false;
    verifierPersisted = persistPasswordVerifier(preparedPasswordSalt, preparedPasswordHash, false);
    memcpy(passwordSalt, preparedPasswordSalt, sizeof(passwordSalt));
    memcpy(passwordHash, preparedPasswordHash, sizeof(passwordHash));
    discardPreparedAdminPassword();
    memset(bootstrapPassword, 0, sizeof(bootstrapPassword));
    revokeAllSessions();
    return true;
}

void WebUi::discardPreparedAdminPassword() {
    memset(preparedPasswordSalt, 0, sizeof(preparedPasswordSalt));
    memset(preparedPasswordHash, 0, sizeof(preparedPasswordHash));
    passwordPrepared = false;
}

bool WebUi::applyAdminPassword(const char* password, bool& verifierPersisted) {
    verifierPersisted = false;
    if (!prepareAdminPassword(password)) return false;
    if (commitPreparedAdminPassword(verifierPersisted)) return true;
    discardPreparedAdminPassword();
    return false;
}
