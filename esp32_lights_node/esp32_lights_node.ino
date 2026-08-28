/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ DE ILUMINAÇÃO & MOVIMENTO (ESP32-C3 SUPER MINI)
 * VERSÃO: 2.1 (Pino Padrão GPIO 1 + Strobe Rítmico Ultra-Responsivo)
 * ATUADORES:
 *   - Strobe Branco 12V (Padrão GPIO 1 via ULN2003) com rajadas nítidas
 *   - Globo RGB (GPIO 4, 5, 6 via ULN2003)
 *   - Servo Motor SG90 (GPIO 7)
 *   - Servidor Web HTTP (Porta 80)
 * ==============================================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>

// ------------------------------------------------------------------------------
// 1. CONFIGURAÇÃO WI-FI, UDP & WEB SERVER
// ------------------------------------------------------------------------------
const char* WIFI_SSID = "SEU_WIFI_NOME";        // << Coloque o nome do seu Wi-Fi
const char* WIFI_PASS = "SUA_WIFI_SENHA";       // << Coloque a senha do seu Wi-Fi
const unsigned int UDP_PORT = 5005;

WiFiUDP udp;
WebServer server(80);
Preferences prefs;
char packetBuffer[2048];

// ------------------------------------------------------------------------------
// 2. PINAGEM GPIO (ESP32-C3 Super Mini)
// ------------------------------------------------------------------------------
// GPIO 1 é o pino padrão definitivo (livre de strapping/cristal)
int pinoStrobeAtual = 1;

#define PIN_GLOBO_R          4   // ULN2003 - Globo Vermelho
#define PIN_GLOBO_G          5   // ULN2003 - Globo Verde
#define PIN_GLOBO_B          6   // ULN2003 - Globo Azul
#define PIN_SERVO_GLOBO      7   // Sinal do Servo Motor SG90 (50Hz)
#define PIN_LED_ONBOARD      8   // Status Wi-Fi

// ------------------------------------------------------------------------------
// 3. PARÂMETROS DE CALIBRAÇÃO & CONTROLE DOS ATUADORES
// ------------------------------------------------------------------------------
bool sistemaLigado = true; // Master Power Switch
int modoOperacao = 0;      // 0 = Áudio Automático, 1 = Manual / Teste

struct ConfigLuzes {
  // STROBE
  float threshGrave = 0.35f;    // Limiar nos graves (0.15 a 0.85)
  int duracaoTotalMs = 300;     // Janela da rajada (100 a 800ms)
  int quantidadeFlashes = 2;    // Piscadas por batida (1 a 8 flashes)

  // GLOBO RGB
  int brilhoGloboPct = 100;     // Brilho máximo do globo (0 a 100%)
  int sensibilidadeVocal = 150; // Modulação vocal

  // SERVO SG90
  int anguloMin = 20;           // Ângulo mínimo (0 a 80 graus)
  int anguloMax = 160;          // Ângulo máximo (100 a 180 graus)
  float freqVarredura = 0.4f;   // Velocidade suave (0.1 a 2.0 Hz)
  bool jumpNoKick = true;       // Salto no bumbo
} cfg;

// Estados do Strobe
bool strobeFixoOn = false;
bool strobeRajadaAtiva = false;
unsigned long inicioRajadaStrobe = 0;
int flashesRestantes = 0;
unsigned long proximoChaveamentoStrobe = 0;
bool estadoFisicoStrobe = false;

// Variáveis do Servo SG90
unsigned long ultimoPulsoServo = 0;
int pulsoServoUs = 1500; // 90 graus
float anguloAtualServo = 90.0f;
float ladoSaltoServo = 30.0f;

// Controle Rítmico
unsigned long ultimoKickValido = 0;
const unsigned long COOLDOWN_KICK_MS = 160; // Cooldown ágil
unsigned long ultimoPacoteAudio = 0;
unsigned long totalPacotesRecebidos = 0;

// Contexto Spotify
struct ContextoSpotify {
  bool ativo = false;
  bool tocando = false;
  float energia = 0.6;
  float danceabilidade = 0.6;
  String modo_sugerido = "fallback";
  unsigned long ultimo_timestamp = 0;
} spotify;

// ------------------------------------------------------------------------------
// 4. FUNÇÕES DE CONTROLE DE HARDWARE
// ------------------------------------------------------------------------------

void aplicarPinoStrobe(int novoPino) {
  digitalWrite(pinoStrobeAtual, LOW);
  pinoStrobeAtual = novoPino;
  pinMode(pinoStrobeAtual, OUTPUT);
  digitalWrite(pinoStrobeAtual, LOW);
}

void setStrobeHardware(bool ligado) {
  estadoFisicoStrobe = ligado;
  digitalWrite(pinoStrobeAtual, ligado ? HIGH : LOW);
}

void setGloboRGB(float r_pct, float g_pct, float b_pct) {
  float fator = (float)cfg.brilhoGloboPct / 100.0f;
  int valR = (int)(constrain(r_pct * fator, 0.0f, 100.0f) * 2.55f);
  int valG = (int)(constrain(g_pct * fator, 0.0f, 100.0f) * 2.55f);
  int valB = (int)(constrain(b_pct * fator, 0.0f, 100.0f) * 2.55f);

  analogWrite(PIN_GLOBO_R, valR);
  analogWrite(PIN_GLOBO_G, valG);
  analogWrite(PIN_GLOBO_B, valB);
}

void setServoAngulo(float graus) {
  graus = constrain(graus, (float)cfg.anguloMin, (float)cfg.anguloMax);
  anguloAtualServo = graus;
  pulsoServoUs = 550 + (int)((graus / 180.0f) * (2400 - 550));
}

void setServoVarredura(float freq_hz, float ang_min, float ang_max, float tempo_s) {
  float seno = (sin(2.0f * PI * freq_hz * tempo_s) + 1.0f) / 2.0f;
  float angulo = ang_min + (seno * (ang_max - ang_min));
  setServoAngulo(angulo);
}

void atualizarServo(unsigned long agoraUs) {
  if (agoraUs - ultimoPulsoServo >= 20000) {
    ultimoPulsoServo = agoraUs;
    digitalWrite(PIN_SERVO_GLOBO, HIGH);
    delayMicroseconds(pulsoServoUs);
    digitalWrite(PIN_SERVO_GLOBO, LOW);
  }
}

// Inicia uma rajada nítida de flashes
void dispararStrobeRajada(int numFlashes, int duracaoTotalMs) {
  strobeFixoOn = false;
  strobeRajadaAtiva = true;
  inicioRajadaStrobe = millis();
  flashesRestantes = max(1, numFlashes);

  // Liga o primeiro flash imediatamente
  setStrobeHardware(true);
  
  // Duração de cada piscada acesa: entre 40ms e 70ms
  int tempoFlashOn = constrain((duracaoTotalMs / (flashesRestantes * 2)), 35, 80);
  proximoChaveamentoStrobe = millis() + tempoFlashOn;
}

// Motor Não-Bloqueante de Flashes do Strobe
void processarStrobe(unsigned long agora) {
  if (strobeFixoOn) {
    setStrobeHardware(true);
    return;
  }

  if (!strobeRajadaAtiva) {
    setStrobeHardware(false);
    return;
  }

  if (agora >= proximoChaveamentoStrobe) {
    if (estadoFisicoStrobe) {
      // Estava aceso: agora apaga
      setStrobeHardware(false);
      flashesRestantes--;

      if (flashesRestantes <= 0 || (agora - inicioRajadaStrobe >= (unsigned long)cfg.duracaoTotalMs)) {
        strobeRajadaAtiva = false;
      } else {
        // Intervalo apagado entre piscadas (40ms a 60ms)
        proximoChaveamentoStrobe = agora + 45;
      }
    } else {
      // Estava apagado e ainda restam flashes: acende o próximo!
      setStrobeHardware(true);
      int tempoFlashOn = constrain((cfg.duracaoTotalMs / (max(1, cfg.quantidadeFlashes) * 2)), 35, 80);
      proximoChaveamentoStrobe = agora + tempoFlashOn;
    }
  }
}

void desligarTudo() {
  strobeRajadaAtiva = false;
  strobeFixoOn = false;
  setStrobeHardware(false);
  setGloboRGB(0, 0, 0);
  setServoAngulo(90.0f);
}

void atualizarPaletaGlobo(String modo, float nivel_vocal, float tempo_s) {
  float brilho_base = max(30.0f, nivel_vocal * 100.0f);

  if (modo == "suave") {
    float onda = (sin(tempo_s * 0.8f) + 1.0f) / 2.0f;
    float r = 0.0f;
    float g = onda * 50.0f * (brilho_base / 100.0f);
    float b = (1.0f - onda * 0.4f) * 90.0f * (brilho_base / 100.0f);
    setGloboRGB(r, g, b);
  } 
  else if (modo == "alta_energia") {
    float onda_r = (sin(tempo_s * 3.0f) + 1.0f) / 2.0f;
    float onda_g = (sin(tempo_s * 3.0f + 2.09f) + 1.0f) / 2.0f;
    float onda_b = (sin(tempo_s * 3.0f + 4.18f) + 1.0f) / 2.0f;
    setGloboRGB(
      onda_r * 100.0f * max(0.6f, nivel_vocal),
      onda_g * 80.0f * max(0.4f, nivel_vocal),
      onda_b * 100.0f * max(0.6f, nivel_vocal)
    );
  } 
  else if (modo == "standby") {
    setGloboRGB(0, 0, 0);
  } 
  else {
    float onda = (sin(tempo_s * 1.5f) + 1.0f) / 2.0f;
    float r = onda * 80.0f * (brilho_base / 100.0f);
    float g = (1.0f - onda) * 50.0f * (brilho_base / 100.0f);
    float b = 90.0f * (brilho_base / 100.0f);
    setGloboRGB(r, g, b);
  }
}

// ------------------------------------------------------------------------------
// 5. PÁGINA WEB HTML / CSS / JS EMBUTIDA
// ------------------------------------------------------------------------------

const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Controle de Luzes & Strobe</title>
<style>
  :root { --bg: #0b1329; --card: #17233f; --primary: #38bdf8; --accent: #f43f5e; --success: #10b981; --warning: #f59e0b; --text: #f8fafc; }
  body { background: var(--bg); color: var(--text); font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 12px; }
  .container { max-width: 650px; margin: 0 auto; }
  h1 { text-align: center; color: var(--primary); font-size: 1.4rem; margin-bottom: 2px; }
  .subtitle { text-align: center; color: #94a3b8; font-size: 0.8rem; margin-bottom: 14px; }
  .card { background: var(--card); border-radius: 12px; padding: 14px; margin-bottom: 14px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); border: 1px solid #223254; }
  .card-title { font-weight: bold; color: var(--primary); font-size: 1.05rem; margin-bottom: 10px; border-bottom: 1px solid #2a3d66; padding-bottom: 6px; }
  .btn-group { display: flex; gap: 6px; flex-wrap: wrap; margin-bottom: 8px; }
  button { background: #24355a; color: white; border: none; padding: 9px 12px; border-radius: 8px; font-weight: bold; cursor: pointer; flex: 1; min-width: 90px; transition: all 0.2s; font-size: 0.85rem; }
  button:hover { background: #324775; }
  button.active { background: var(--primary); color: #0b1329; }
  button.selected { background: #0284c7; color: white; border: 1px solid #38bdf8; }
  button.success { background: var(--success); color: white; }
  button.warning { background: var(--warning); color: #0b1329; }
  button.danger { background: var(--accent); }
  .slider-row { margin-bottom: 10px; }
  .slider-header { display: flex; justify-content: space-between; font-size: 0.85rem; margin-bottom: 4px; }
  .slider-val { font-weight: bold; color: var(--primary); }
  input[type=range] { width: 100%; height: 8px; border-radius: 4px; background: #24355a; outline: none; -webkit-appearance: none; }
  input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: var(--primary); cursor: pointer; }
  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .toast { display: none; background: var(--success); color: white; text-align: center; padding: 8px; border-radius: 6px; font-weight: bold; margin-top: 8px; font-size: 0.85rem; }
</style>
</head>
<body>
<div class="container">
  <h1>⚡ Controle de Luzes & Servo</h1>
  <div class="subtitle">Strobe Branco | Globo RGB | Servo SG90</div>

  <!-- MASTER POWER -->
  <div class="card" style="text-align: center; padding: 10px;">
    <button id="btnPower" class="success" style="font-size: 1.1rem; padding: 12px; width: 100%; border-radius: 10px;" onclick="togglePower()">🟢 SISTEMA LIGADO (Clique para Desligar Tudo)</button>
  </div>

  <div class="card">
    <div class="card-title">🎮 Modo de Operação</div>
    <div class="btn-group">
      <button id="btnAuto" class="active" onclick="setModo(0)">🎵 Modo Áudio Automático (UDP)</button>
      <button id="btnManual" onclick="setModo(1)">🎛️ Modo Manual / Testes</button>
    </div>
  </div>

  <!-- SEÇÃO 1: STROBE BRANCO -->
  <div class="card" style="border-left: 4px solid #f8fafc;">
    <div class="card-title">⚪ 1. Strobe Branco (Graves / Kicks)</div>
    
    <div style="font-size: 0.85rem; color: #94a3b8; margin-bottom: 6px;">Pino GPIO do Strobe:</div>
    <div class="btn-group" style="margin-bottom: 12px;">
      <button id="pin1" class="selected" onclick="setPinoStrobe(1)">GPIO 1 (Padrão)</button>
      <button id="pin3" onclick="setPinoStrobe(3)">GPIO 3</button>
      <button id="pin10" onclick="setPinoStrobe(10)">GPIO 10</button>
      <button id="pin0" onclick="setPinoStrobe(0)">GPIO 0</button>
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>Quantidade de Piscadas no Bumbo</span><span class="slider-val" id="vFlashes">2 Piscadas</span></div>
      <input type="range" min="1" max="6" step="1" value="2" oninput="updateCfg('fls', this.value, 'vFlashes', ' Piscadas')">
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>Duração da Rajada (Delay Total)</span><span class="slider-val" id="vDelay">300 ms</span></div>
      <input type="range" min="80" max="600" step="20" value="300" oninput="updateCfg('del', this.value, 'vDelay', ' ms')">
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>Sensibilidade aos Graves (Threshold)</span><span class="slider-val" id="vThresh">0.35</span></div>
      <input type="range" min="15" max="80" value="35" oninput="updateCfgFloat('thr', this.value, 'vThresh')">
    </div>

    <div class="btn-group" style="margin-top: 10px;">
      <button class="selected" onclick="testarStrobe()">⚡ Testar Rajada Agora</button>
      <button class="warning" onclick="forcarStrobe(1)">🔦 Forçar 100% LIGADO</button>
      <button class="danger" onclick="forcarStrobe(0)">⬛ Apagar</button>
    </div>
  </div>

  <!-- SEÇÃO 2: GLOBO RGB -->
  <div class="card" style="border-left: 4px solid #38bdf8;">
    <div class="card-title">🌈 2. Globo RGB (Cores & Modulação)</div>
    
    <div class="slider-row">
      <div class="slider-header"><span>Brilho Máximo do Globo</span><span class="slider-val" id="vBrilhoGlobo">100%</span></div>
      <input type="range" min="10" max="100" value="100" oninput="updateCfg('bglo', this.value, 'vBrilhoGlobo', '%')">
    </div>

    <div class="btn-group">
      <button onclick="testarCor(100, 0, 0)">🔴 Vermelho</button>
      <button onclick="testarCor(0, 100, 0)">🟢 Verde</button>
      <button onclick="testarCor(0, 0, 100)">🔵 Azul</button>
      <button onclick="testarCor(100, 100, 100)">⚪ Branco</button>
      <button class="danger" onclick="testarCor(0, 0, 0)">⬛ Apagar</button>
    </div>
  </div>

  <!-- SEÇÃO 3: SERVO MOTOR SG90 -->
  <div class="card" style="border-left: 4px solid #f59e0b;">
    <div class="card-title">🤖 3. Servo Motor SG90 (Movimento do Globo)</div>
    
    <div class="grid-2">
      <div class="slider-row">
        <div class="slider-header"><span>Ângulo Mínimo</span><span class="slider-val" id="vAngMin">20°</span></div>
        <input type="range" min="0" max="80" value="20" oninput="updateCfg('amin', this.value, 'vAngMin', '°')">
      </div>
      <div class="slider-row">
        <div class="slider-header"><span>Ângulo Máximo</span><span class="slider-val" id="vAngMax">160°</span></div>
        <input type="range" min="100" max="180" value="160" oninput="updateCfg('amax', this.value, 'vAngMax', '°')">
      </div>
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>Velocidade de Varredura Suave</span><span class="slider-val" id="vFreqServo">0.4 Hz</span></div>
      <input type="range" min="1" max="15" value="4" oninput="updateCfgFloat('fser', this.value, 'vFreqServo')">
    </div>

    <div class="btn-group">
      <button onclick="posicionarServo(20)">◀ 20°</button>
      <button onclick="posicionarServo(90)">⏹ Centro (90°)</button>
      <button onclick="posicionarServo(160)">160° ▶</button>
      <button onclick="testarVarredura()">🔄 Testar Varredura</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">💾 Salvar Configurações</div>
    <div class="btn-group">
      <button class="success" onclick="salvarFlash()">💾 Salvar na Memória Flash</button>
    </div>
    <div id="toastMsg" class="toast"></div>
  </div>
</div>

<script>
let powerState = true;
function togglePower() {
  powerState = !powerState;
  const btn = document.getElementById('btnPower');
  btn.className = powerState ? 'success' : 'danger';
  btn.innerText = powerState ? '🟢 SISTEMA LIGADO (Clique para Desligar Tudo)' : '🔴 SISTEMA DESLIGADO (Clique para Ligar)';
  fetch('/power?val=' + (powerState ? 1 : 0));
}

function setPinoStrobe(pin) {
  ['pin1', 'pin3', 'pin10', 'pin0'].forEach(id => {
    const el = document.getElementById(id);
    if (el) el.className = '';
  });
  document.getElementById('pin' + pin).className = 'selected';
  fetch('/setpin?pin=' + pin).then(() => showToast('🔌 Pino alterado para GPIO ' + pin));
}

function updateCfg(param, val, labelId, sufixo) {
  document.getElementById(labelId).innerText = val + sufixo;
  fetch('/cfg?' + param + '=' + val);
}

function updateCfgFloat(param, val, labelId) {
  const real = (val / 100.0).toFixed(2);
  document.getElementById(labelId).innerText = real;
  fetch('/cfg?' + param + '=' + real);
}

function setModo(m) {
  fetch('/modo?val=' + m);
  document.getElementById('btnAuto').className = (m === 0) ? 'active' : '';
  document.getElementById('btnManual').className = (m === 1) ? 'active' : '';
}

function testarStrobe() {
  fetch('/teststrobe');
}

function forcarStrobe(val) {
  fetch('/forcarstrobe?val=' + val);
}

function testarCor(r, g, b) {
  setModo(1);
  fetch('/testcor?r=' + r + '&g=' + g + '&b=' + b);
}

function posicionarServo(ang) {
  setModo(1);
  fetch('/testservo?ang=' + ang);
}

function testarVarredura() {
  setModo(1);
  fetch('/testservo?sweep=1');
}

function showToast(msg) {
  const toast = document.getElementById('toastMsg');
  toast.style.display = 'block';
  toast.innerText = msg;
  setTimeout(() => { toast.style.display = 'none'; }, 2500);
}

function salvarFlash() {
  fetch('/salvar').then(() => showToast('💾 Configurações salvas na Memória Flash!'));
}
</script>
</body>
</html>
)rawliteral";

// ------------------------------------------------------------------------------
// 6. ROTAS DO WEB SERVER
// ------------------------------------------------------------------------------

void handlePower() {
  if (server.hasArg("val")) {
    sistemaLigado = (server.arg("val").toInt() == 1);
    if (!sistemaLigado) {
      desligarTudo();
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleRoot() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", HTML_INDEX);
}

void handleModo() {
  if (server.hasArg("val")) {
    modoOperacao = server.arg("val").toInt();
    if (modoOperacao == 0) {
      strobeFixoOn = false;
      setStrobeHardware(false);
      setGloboRGB(0, 0, 0);
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleSetPin() {
  if (server.hasArg("pin")) {
    int p = server.arg("pin").toInt();
    aplicarPinoStrobe(p);
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleForcarStrobe() {
  if (server.hasArg("val")) {
    int val = server.arg("val").toInt();
    if (val == 1) {
      modoOperacao = 1;
      strobeFixoOn = true;
      setStrobeHardware(true);
    } else {
      strobeFixoOn = false;
      setStrobeHardware(false);
      modoOperacao = 0; // Ao apagar o teste fixo, volta automaticamente para o modo de áudio!
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleCfg() {
  if (server.hasArg("del"))  cfg.duracaoTotalMs = server.arg("del").toInt();
  if (server.hasArg("fls"))  cfg.quantidadeFlashes = server.arg("fls").toInt();
  if (server.hasArg("thr"))  cfg.threshGrave = server.arg("thr").toFloat();
  if (server.hasArg("bglo")) cfg.brilhoGloboPct = server.arg("bglo").toInt();
  if (server.hasArg("amin")) cfg.anguloMin = server.arg("amin").toInt();
  if (server.hasArg("amax")) cfg.anguloMax = server.arg("amax").toInt();
  if (server.hasArg("fser")) cfg.freqVarredura = server.arg("fser").toFloat();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleTestStrobe() {
  dispararStrobeRajada(cfg.quantidadeFlashes, cfg.duracaoTotalMs);
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleTestCor() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    modoOperacao = 1;
    setGloboRGB(server.arg("r").toFloat(), server.arg("g").toFloat(), server.arg("b").toFloat());
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleTestServo() {
  modoOperacao = 1;
  if (server.hasArg("ang")) {
    setServoAngulo(server.arg("ang").toFloat());
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleSalvar() {
  prefs.begin("lights_cfg", false);
  prefs.putInt("pstr", pinoStrobeAtual);
  prefs.putInt("del", cfg.duracaoTotalMs);
  prefs.putInt("fls", cfg.quantidadeFlashes);
  prefs.putFloat("thr", cfg.threshGrave);
  prefs.putInt("bglo", cfg.brilhoGloboPct);
  prefs.putInt("amin", cfg.anguloMin);
  prefs.putInt("amax", cfg.anguloMax);
  prefs.putFloat("fser", cfg.freqVarredura);
  prefs.end();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "SALVO");
}

// ------------------------------------------------------------------------------
// 7. SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==================================================");
  Serial.println(" ESP32-C3: NÓ DE LUZES & STROBE v2.1 (Padrão GPIO 1)");
  Serial.println("==================================================");

  prefs.begin("lights_cfg", true);
  pinoStrobeAtual = prefs.getInt("pstr", 1); // Padrão GPIO 1
  cfg.duracaoTotalMs = prefs.getInt("del", 300);
  cfg.quantidadeFlashes = prefs.getInt("fls", 2);
  cfg.threshGrave = prefs.getFloat("thr", 0.35f);
  cfg.brilhoGloboPct = prefs.getInt("bglo", 100);
  cfg.anguloMin = prefs.getInt("amin", 20);
  cfg.anguloMax = prefs.getInt("amax", 160);
  cfg.freqVarredura = prefs.getFloat("fser", 0.4f);
  prefs.end();

  // Inicializa o pino do Strobe (GPIO 1)
  aplicarPinoStrobe(pinoStrobeAtual);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  // AUTO-TESTE INICIAL DE BOOT (Dois flashes no Strobe)
  setStrobeHardware(true); delay(100); setStrobeHardware(false); delay(80);
  setStrobeHardware(true); delay(100); setStrobeHardware(false);

  setGloboRGB(100, 0, 0); delay(100);
  setGloboRGB(0, 100, 0); delay(100);
  setGloboRGB(0, 0, 100); delay(100);
  setGloboRGB(0, 0, 0);

  // Wi-Fi
  Serial.printf("\n[Wi-Fi] Conectando a: %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_ONBOARD, !digitalRead(PIN_LED_ONBOARD));
    delay(150);
    Serial.print(".");
  }

  WiFi.setSleep(false);
  digitalWrite(PIN_LED_ONBOARD, LOW);
  Serial.printf("\n[Wi-Fi] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());

  // Rotas Web
  server.on("/", handleRoot);
  server.on("/power", handlePower);
  server.on("/modo", handleModo);
  server.on("/setpin", handleSetPin);
  server.on("/forcarstrobe", handleForcarStrobe);
  server.on("/cfg", handleCfg);
  server.on("/teststrobe", handleTestStrobe);
  server.on("/testcor", handleTestCor);
  server.on("/testservo", handleTestServo);
  server.on("/salvar", handleSalvar);
  server.begin();
  Serial.printf("[WEB] Painel de Luzes ativo em: http://%s\n", WiFi.localIP().toString().c_str());

  udp.begin(UDP_PORT);
  Serial.printf("[UDP] Escutando porta %d...\n", UDP_PORT);
}

// ------------------------------------------------------------------------------
// 8. LOOP PRINCIPAL
// ------------------------------------------------------------------------------
void loop() {
  unsigned long agora = millis();
  unsigned long agoraUs = micros();
  float tempo_s = agora / 1000.0f;

  server.handleClient();

  if (!sistemaLigado) {
    desligarTudo();
    delay(1);
    return;
  }

  atualizarServo(agoraUs);
  processarStrobe(agora);

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(100);
    return;
  }

  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = '\0';
      totalPacotesRecebidos++;

      digitalWrite(PIN_LED_ONBOARD, (totalPacotesRecebidos % 2 == 0) ? LOW : HIGH);

      StaticJsonDocument<1536> doc;
      DeserializationError error = deserializeJson(doc, packetBuffer);

      if (!error) {
        const char* tipo = doc["tipo"] | "audio";

        if (strcmp(tipo, "spotify") == 0) {
          spotify.ativo = true;
          spotify.tocando = doc["tocando"] | false;
          spotify.energia = doc["energia"] | 0.6f;
          spotify.danceabilidade = doc["danceabilidade"] | 0.6f;
          spotify.modo_sugerido = doc["modo_sugerido"] | "media_energia";
          spotify.ultimo_timestamp = agora;
        }
        else {
          ultimoPacoteAudio = agora;

          bool spotify_online = spotify.ativo && (agora - spotify.ultimo_timestamp < 4000);
          String modo_atual = "fallback";
          if (spotify_online) {
            modo_atual = spotify.tocando ? spotify.modo_sugerido : "standby";
          }

          JsonObject faixas = doc["faixas"];
          JsonObject graves = faixas["graves"];
          JsonObject medios = faixas["medios"];
          JsonObject medios_graves = faixas["medios_graves"];
          JsonObject agudos = faixas["agudos"];

          bool ativo_grave = graves["ativo"] | false;
          bool pico_grave = graves["pico"] | false;
          float nivel_grave = graves["nivel"] | 0.0f;

          float nivel_med = medios["nivel"] | 0.0f;
          float nivel_med_grav = medios_graves["nivel"] | 0.0f;
          float nivel_agud = agudos["nivel"] | 0.0f;

          float nivel_vocal = (nivel_med * 0.70f) + (nivel_med_grav * 0.20f) + (nivel_agud * 0.10f);
          if (nivel_vocal < 0.05f) nivel_vocal = nivel_med;
          nivel_vocal = constrain(nivel_vocal, 0.0f, 1.0f);

          // A. DISPARO DO STROBE NO ÁUDIO
          if (modoOperacao == 0) {
            // Reage ao pico_grave, ativo_grave ou nível acima do threshold
            bool bateuGrave = (pico_grave || ativo_grave || nivel_grave >= cfg.threshGrave);

            if (bateuGrave && (agora - ultimoKickValido >= COOLDOWN_KICK_MS)) {
              ultimoKickValido = agora;
              dispararStrobeRajada(cfg.quantidadeFlashes, cfg.duracaoTotalMs);
            }

            // B. GLOBO RGB
            atualizarPaletaGlobo(modo_atual, nivel_vocal, tempo_s);

            // C. SERVO SG90
            if (modo_atual == "alta_energia") {
              if (bateuGrave && cfg.jumpNoKick) {
                ladoSaltoServo = (ladoSaltoServo <= 90.0f) ? (float)cfg.anguloMax : (float)cfg.anguloMin;
                setServoAngulo(ladoSaltoServo);
              } else {
                setServoVarredura(cfg.freqVarredura * 1.5f, (float)cfg.anguloMin, (float)cfg.anguloMax, tempo_s);
              }
            } 
            else if (modo_atual == "suave") {
              setServoVarredura(cfg.freqVarredura * 0.5f, (float)cfg.anguloMin + 20, (float)cfg.anguloMax - 20, tempo_s);
            } 
            else if (modo_atual == "standby") {
              if (ativo_grave || pico_grave) {
                setServoVarredura(0.4f, (float)cfg.anguloMin + 15, (float)cfg.anguloMax - 15, tempo_s);
              } else {
                setServoAngulo(90.0f);
              }
            } 
            else {
              setServoVarredura(cfg.freqVarredura, (float)cfg.anguloMin, (float)cfg.anguloMax, tempo_s);
            }
          }
        }
      }
    }
  }

  if (agora - ultimoPacoteAudio > 4000 && ultimoPacoteAudio > 0 && modoOperacao == 0) {
    desligarTudo();
  }

  delay(1);
}
