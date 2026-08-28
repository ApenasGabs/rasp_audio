/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ DEDICADO: PROJETOR LASER DMX512 (ESP32-C3 SUPER MINI)
 * HARDWARE: Módulo RS-485 (MAX485) conectado ao Projetor Laser Profissional via XLR
 * RECURSOS:
 *   - Controle DMX512 Puro (16 Canais)
 *   - Mesa DMX Virtual (16 Sliders)
 *   - Bancada de Testes / Scanner de Padrões (0 a 255)
 *   - Gatilho Silábico Fonema por Fonema na Voz & Rotação no Bumbo
 *   - Servidor Web HTTP (Porta 80) para calibração em tempo real
 * ==============================================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>

const char* WIFI_SSID = "SEU_WIFI_NOME";        // << Coloque o nome do seu Wi-Fi
const char* WIFI_PASS = "SUA_WIFI_SENHA";       // << Coloque a senha do seu Wi-Fi
const unsigned int UDP_PORT = 5005;

WiFiUDP udp;
WebServer server(80);
Preferences prefs;
char packetBuffer[2048];
bool dmxInicializado = false;

// MÓDULO RS-485 (MAX485)
#define PIN_DMX_TX          21   // DI (Data In) do MAX485
#define PIN_DMX_ENABLE      10   // DE + RE do MAX485
#define PIN_LED_ONBOARD      8   // Status Wi-Fi

uint8_t dmxCanais[16];
unsigned long ultimoEnvioDmx = 0;
bool sistemaLigado = true; // Master Power Switch\nint modoOperacaoWeb = 0; // 0 = Áudio Automático, 1 = Mesa DMX, 2 = Bancada de Testes

#define MAX_PADROES 20
uint8_t listaBumbo[MAX_PADROES] = {70, 90, 110, 130, 40, 55};
int totalListaBumbo = 6;
int indiceBumbo = 0;

uint8_t listaVocal[MAX_PADROES] = {25, 85, 120, 160, 190, 215};
int totalListaVocal = 6;
int indiceVocal = 0;

struct ParametrosCalibracao {
  int zoomMinimo = 190;
  int zoomMaximo = 255;
  int sensibilidadeVocal = 175;
  int velocidadeRotacao = 170;
  int batidasPorTroca = 4;
  float thresholdVocal = 0.32f;
  int duracaoGlitchMs = 95;
} calib;

const uint8_t coresLaser[] = {12, 22, 32, 42, 52, 62, 72};
const int totalCores = sizeof(coresLaser) / sizeof(coresLaser[0]);
int indiceCor = 0;

uint8_t padraoTesteAtual = 70;
int zoomTeste = 220;
int rotacaoTeste = 170;
int corTeste = 12;
bool autoScanAtivo = false;
unsigned long ultimoAutoScan = 0;
unsigned long simulaKickAte = 0;
unsigned long simulaVocalAte = 0;

unsigned long ultimoKickValido = 0;
const unsigned long COOLDOWN_KICK_MS = 260;
int contadorBatidasBumbo = 0;

float zoomAtual = 190.0f;
float ultimoNivelVocal = 0.0f;
unsigned long fimGlitchSilaba = 0;
unsigned long ultimoDisparoSilaba = 0;
unsigned long fimImpactoKick = 0;

void inicializarDMX() {
  pinMode(PIN_DMX_ENABLE, OUTPUT);
  digitalWrite(PIN_DMX_ENABLE, HIGH);
  pinMode(PIN_DMX_TX, OUTPUT);
  digitalWrite(PIN_DMX_TX, HIGH);

  memset(dmxCanais, 0, sizeof(dmxCanais));
  dmxCanais[0] = 50;   // CH1: Modo Manual Console (Luz Aberta)
  dmxCanais[1] = 128;
  dmxCanais[2] = 12;
  dmxCanais[3] = 0;
  dmxCanais[4] = 70;
  dmxCanais[5] = 210;
  dmxCanais[6] = 0;
  dmxCanais[7] = 170;
  dmxCanais[8] = 64;
  dmxCanais[9] = 64;
  dmxCanais[10] = 64;
  dmxCanais[11] = 64;
  dmxCanais[12] = 0;
  dmxCanais[13] = 0;
  dmxCanais[14] = 255;
  dmxCanais[15] = 0;
  dmxInicializado = true;
}

void enviarFrameDMX() {
  if (!dmxInicializado) return;

  Serial1.flush();
  pinMode(PIN_DMX_TX, OUTPUT);
  digitalWrite(PIN_DMX_TX, LOW);
  delayMicroseconds(100);

  digitalWrite(PIN_DMX_TX, HIGH);
  delayMicroseconds(12);

  Serial1.begin(250000, SERIAL_8N2, -1, PIN_DMX_TX);
  Serial1.write((uint8_t)0x00);
  Serial1.write(dmxCanais, 16);
}

void parseListaString(String str, uint8_t* lista, int& total) {
  total = 0;
  int inicio = 0;
  while (inicio < str.length() && total < MAX_PADROES) {
    int virgula = str.indexOf(',', inicio);
    if (virgula == -1) virgula = str.length();
    String pedaco = str.substring(inicio, virgula);
    pedaco.trim();
    if (pedaco.length() > 0) {
      lista[total++] = (uint8_t)constrain(pedaco.toInt(), 0, 255);
    }
    inicio = virgula + 1;
  }
}

String listaParaString(uint8_t* lista, int total) {
  String s = "";
  for (int i = 0; i < total; i++) {
    if (i > 0) s += ",";
    s += String(lista[i]);
  }
  return s;
}

void atualizarLaserDMX_Audio(float nivel_graves, bool pico_grave, float nivel_vocal, float tempo_s, float deltaTempo, unsigned long agora) {
  dmxCanais[0] = 50;
  dmxCanais[1] = 128;
  dmxCanais[13] = 0;
  dmxCanais[14] = 255;
  dmxCanais[15] = 0;
  dmxCanais[6] = 0;

  float deltaVocal = nivel_vocal - ultimoNivelVocal;
  ultimoNivelVocal = nivel_vocal;

  bool novaSilaba = (nivel_vocal >= calib.thresholdVocal && (deltaVocal > 0.08f || agora - ultimoDisparoSilaba > 140));

  if (novaSilaba && (agora - ultimoDisparoSilaba >= 75)) {
    ultimoDisparoSilaba = agora;
    fimGlitchSilaba = agora + calib.duracaoGlitchMs;
    if (totalListaVocal > 0) {
      indiceVocal = (indiceVocal + 1) % totalListaVocal;
    }
  }

  bool silabaAtiva = (agora < fimGlitchSilaba);

  bool kickReal = false;
  if (pico_grave && (agora - ultimoKickValido >= COOLDOWN_KICK_MS)) {
    kickReal = true;
    ultimoKickValido = agora;
    fimImpactoKick = agora + 200;

    indiceCor = (indiceCor + 1) % totalCores;

    if (totalListaBumbo > 0) {
      contadorBatidasBumbo++;
      if (contadorBatidasBumbo >= calib.batidasPorTroca) {
        indiceBumbo = (indiceBumbo + 1) % totalListaBumbo;
        contadorBatidasBumbo = 0;
      }
    }
  }

  dmxCanais[2] = coresLaser[indiceCor];
  dmxCanais[3] = 0;

  if (silabaAtiva && totalListaVocal > 0) {
    dmxCanais[4] = listaVocal[indiceVocal];
    dmxCanais[7] = 230;
    dmxCanais[8] = 64;
    dmxCanais[12] = (uint8_t)(nivel_vocal * (float)calib.sensibilidadeVocal);
  } else {
    if (totalListaBumbo > 0) {
      dmxCanais[4] = listaBumbo[indiceBumbo];
    }
    dmxCanais[7] = (uint8_t)calib.velocidadeRotacao;
    dmxCanais[8] = (kickReal) ? 180 : 64;
    dmxCanais[12] = 0;
  }

  float zoomAlvo = (float)calib.zoomMinimo + (nivel_graves * ((float)calib.zoomMaximo - (float)calib.zoomMinimo));
  if (kickReal || (agora < fimImpactoKick) || silabaAtiva) {
    zoomAlvo = (float)calib.zoomMaximo;
  }

  if (zoomAlvo > zoomAtual) {
    zoomAtual = zoomAlvo;
  } else {
    zoomAtual = max((float)calib.zoomMinimo, zoomAtual - (140.0f * deltaTempo));
  }

  dmxCanais[5] = (uint8_t)constrain(zoomAtual, (float)calib.zoomMinimo, 255.0f);
  dmxCanais[10] = (uint8_t)(64 + sin(tempo_s * 0.4f) * 18);
  dmxCanais[11] = 64;
  dmxCanais[9] = 64;
}

void atualizarModoTeste(unsigned long agora) {
  if (autoScanAtivo && (agora - ultimoAutoScan >= 2000)) {
    ultimoAutoScan = agora;
    padraoTesteAtual = (padraoTesteAtual + 1) % 256;
  }

  dmxCanais[0] = 50;
  dmxCanais[1] = 128;
  dmxCanais[2] = (uint8_t)corTeste;
  dmxCanais[3] = 0;
  dmxCanais[4] = padraoTesteAtual;
  dmxCanais[6] = 0;
  dmxCanais[9] = 64;
  dmxCanais[10] = 64;
  dmxCanais[11] = 64;
  dmxCanais[13] = 0;
  dmxCanais[14] = 255;
  dmxCanais[15] = 0;

  if (agora < simulaKickAte) {
    dmxCanais[5] = 255;
    dmxCanais[7] = 240;
    dmxCanais[8] = 180;
  } else if (agora < simulaVocalAte) {
    dmxCanais[5] = 240;
    dmxCanais[7] = 220;
    dmxCanais[8] = 64;
    dmxCanais[12] = 180;
  } else {
    dmxCanais[5] = (uint8_t)zoomTeste;
    dmxCanais[7] = (uint8_t)rotacaoTeste;
    dmxCanais[8] = 64;
    dmxCanais[12] = 0;
  }
}

// ------------------------------------------------------------------------------
// PÁGINA WEB & ROTAS DO LASER DMX STUDIO PRO
// ------------------------------------------------------------------------------
const char HTML_INDEX_LASER[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Laser DMX Studio Pro</title>
<style>
  :root { --bg: #0b1329; --card: #17233f; --primary: #38bdf8; --accent: #f43f5e; --success: #10b981; --warning: #f59e0b; --text: #f8fafc; }
  body { background: var(--bg); color: var(--text); font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 12px; }
  .container { max-width: 680px; margin: 0 auto; }
  h1 { text-align: center; color: var(--primary); font-size: 1.4rem; margin-bottom: 2px; }
  .subtitle { text-align: center; color: #94a3b8; font-size: 0.8rem; margin-bottom: 14px; }
  .card { background: var(--card); border-radius: 12px; padding: 14px; margin-bottom: 14px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); border: 1px solid #223254; }
  .card-title { font-weight: bold; color: var(--primary); font-size: 1.05rem; margin-bottom: 10px; border-bottom: 1px solid #2a3d66; padding-bottom: 6px; }
  .btn-group { display: flex; gap: 6px; flex-wrap: wrap; margin-bottom: 10px; }
  button { background: #24355a; color: white; border: none; padding: 8px 12px; border-radius: 8px; font-weight: bold; cursor: pointer; flex: 1; min-width: 90px; transition: all 0.2s; font-size: 0.85rem; }
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
  input[type=text] { width: 100%; background: #0b1329; border: 1px solid #2a3d66; border-radius: 6px; color: #a5f3fc; font-family: monospace; font-size: 0.9rem; padding: 8px; box-sizing: border-box; margin-bottom: 8px; }
  .tag-box { display: flex; gap: 5px; flex-wrap: wrap; margin-bottom: 8px; }
  .tag-btn { background: #1e293b; border: 1px solid #334155; color: #94a3b8; font-size: 0.75rem; padding: 4px 8px; border-radius: 6px; cursor: pointer; }
  .tag-btn.on { background: #0284c7; color: white; border-color: #38bdf8; }
  .big-badge { text-align: center; font-size: 2.2rem; font-weight: bold; color: var(--primary); background: #0b1329; border: 2px solid #2a3d66; border-radius: 12px; padding: 8px; margin-bottom: 10px; }
  .toast { display: none; background: var(--success); color: white; text-align: center; padding: 8px; border-radius: 6px; font-weight: bold; margin-top: 8px; font-size: 0.85rem; }
  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
</style>
</head>
<body>
<div class="container">
  <h1>⚡ Laser DMX Studio Pro (Dedicado)</h1>
  <div class="subtitle">Áudio Automático | Mesa DMX 16 Canais | Bancada de Testes</div>

  <!-- MASTER POWER BUTTON -->
  <div class="card" style="text-align: center; padding: 10px;">
    <button id="btnPowerLaser" class="success" style="font-size: 1.1rem; padding: 12px; width: 100%; border-radius: 10px;" onclick="togglePowerLaser()">🟢 LASER LIGADO (Clique para Desligar Tudo)</button>
  </div>

  <div class="card">
    <div class="card-title">🎮 Modo de Operação</div>
    <div class="btn-group">
      <button id="btnAuto" class="active" onclick="setModo(0)">🎵 1. Áudio Automático</button>
      <button id="btnManual" onclick="setModo(1)">🎛️ 2. Mesa DMX (16 Canais)</button>
      <button id="btnTeste" class="warning" onclick="setModo(2)">🔬 3. Bancada de Testes</button>
    </div>
  </div>

  <div class="card" id="cardTeste" style="border: 2px solid var(--warning);">
    <div class="card-title" style="color: var(--warning);">🔬 3. Bancada de Testes / Scanner de Padrões</div>
    
    <div class="big-badge" id="lblPadraoGrande">CH5 (Padrão): 70</div>

    <div class="btn-group">
      <button onclick="navPadrao(-1)">◀ Anterior (-1)</button>
      <button onclick="navPadrao(-5)">⏪ -5</button>
      <button onclick="navPadrao(+5)">+5 ⏩</button>
      <button onclick="navPadrao(+1)">Próximo (+1) ▶</button>
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>CH5: Padrão / Gobo (0-255)</span><span class="slider-val" id="vPadraoSlider">70</span></div>
      <input type="range" min="0" max="255" value="70" id="rngPadrao" oninput="setPadraoDireto(this.value)">
    </div>

    <div class="grid-2">
      <div class="slider-row">
        <div class="slider-header"><span>CH6: Tamanho / Zoom</span><span class="slider-val" id="vZoomTeste">220</span></div>
        <input type="range" min="50" max="255" value="220" oninput="setParamTeste('z', this.value, 'vZoomTeste')">
      </div>
      <div class="slider-row">
        <div class="slider-header"><span>CH8: Rotação</span><span class="slider-val" id="vRotTeste">170</span></div>
        <input type="range" min="0" max="255" value="170" oninput="setParamTeste('r', this.value, 'vRotTeste')">
      </div>
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>CH3: Cor</span><span class="slider-val" id="vCorTeste">12</span></div>
      <input type="range" min="0" max="255" value="12" oninput="setParamTeste('c', this.value, 'vCorTeste')">
    </div>

    <div class="btn-group" style="margin-top: 10px;">
      <button id="btnAutoScan" onclick="toggleAutoScan()">▶️ Iniciar Auto-Scan (2s)</button>
      <button onclick="simulaKick()">🥁 Simular Bumbo</button>
      <button onclick="simulaVocal()">🎤 Simular Voz</button>
    </div>

    <div class="btn-group" style="margin-top: 6px;">
      <button class="selected" onclick="addPadraoPara('bumbo')">➕ Adicionar ao Bumbo</button>
      <button class="selected" onclick="addPadraoPara('vocal')">➕ Adicionar ao Vocal</button>
    </div>
  </div>

  <div class="card" id="cardMesaDMX">
    <div class="card-title">🎛️ 2. Mesa DMX Virtual (16 Canais)</div>
    
    <div class="slider-row">
      <div class="slider-header"><span>CH1: Modo de Operação (50=Manual Console DMX)</span><span class="slider-val" id="vCH1">50</span></div>
      <input type="range" min="0" max="255" value="50" oninput="setDMX(1, this.value, 'vCH1')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH3: Cor do Laser</span><span class="slider-val" id="vCH3">12</span></div>
      <input type="range" min="0" max="255" value="12" oninput="setDMX(3, this.value, 'vCH3')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH5: Seleção de Padrão</span><span class="slider-val" id="vCH5">70</span></div>
      <input type="range" min="0" max="255" value="70" oninput="setDMX(5, this.value, 'vCH5')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH6: Tamanho / Zoom Manual</span><span class="slider-val" id="vCH6">210</span></div>
      <input type="range" min="0" max="255" value="210" oninput="setDMX(6, this.value, 'vCH6')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH8: Rotação no Centro</span><span class="slider-val" id="vCH8">170</span></div>
      <input type="range" min="0" max="255" value="170" oninput="setDMX(8, this.value, 'vCH8')">
    </div>
    <div class="grid-2">
      <div class="slider-row">
        <div class="slider-header"><span>CH11: Posição X</span><span class="slider-val" id="vCH11">64</span></div>
        <input type="range" min="0" max="127" value="64" oninput="setDMX(11, this.value, 'vCH11')">
      </div>
      <div class="slider-row">
        <div class="slider-header"><span>CH12: Posição Y</span><span class="slider-val" id="vCH12">64</span></div>
        <input type="range" min="0" max="127" value="64" oninput="setDMX(12, this.value, 'vCH12')">
      </div>
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH13: Onda no Eixo X</span><span class="slider-val" id="vCH13">0</span></div>
      <input type="range" min="0" max="255" value="0" oninput="setDMX(13, this.value, 'vCH13')">
    </div>
  </div>

  <div class="card">
    <div class="card-title">🥁 1. Lista de Padrões do Bumbo</div>
    <div class="tag-box" id="tagsBumbo"></div>
    <input type="text" id="inBumbo" onchange="salvarListas()">
    
    <div class="slider-row" style="margin-top: 10px;">
      <div class="slider-header"><span>Trocar de Padrão a cada:</span><span class="slider-val" id="vBatidas">4 Batidas</span></div>
      <div class="btn-group">
        <button id="b2" onclick="setBatidas(2)">2 Batidas</button>
        <button id="b4" class="selected" onclick="setBatidas(4)">4 Batidas</button>
        <button id="b8" onclick="setBatidas(8)">8 Batidas</button>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card-title">🎤 2. Lista de Padrões do Glitch Silábico (Na Voz)</div>
    <div class="tag-box" id="tagsVocal"></div>
    <input type="text" id="inVocal" onchange="salvarListas()">

    <div class="slider-row" style="margin-top: 10px;">
      <div class="slider-header"><span>Gatilho de Sensibilidade Vocal</span><span class="slider-val" id="vThresh">0.32</span></div>
      <input type="range" min="10" max="80" value="32" oninput="updateThresh(this.value)">
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>Duração do Flash Silábico</span><span class="slider-val" id="vGlitch">95 ms</span></div>
      <input type="range" min="50" max="180" value="95" oninput="updateGlitch(this.value)">
    </div>
  </div>

  <div class="card">
    <div class="card-title">💾 Salvar na Flash</div>
    <div class="btn-group">
      <button class="success" onclick="salvarFlash()">💾 Salvar Configurações</button>
    </div>
    <div id="toastMsg" class="toast"></div>
  </div>
</div>

<script>
const todosPadroes = [
  {val: 10, nome: "Círculo"},
  {val: 25, nome: "Zigzag"},
  {val: 40, nome: "Caixa 3D"},
  {val: 55, nome: "Estrela"},
  {val: 70, nome: "Túnel"},
  {val: 85, nome: "Túnel Glitch"},
  {val: 90, nome: "Onda"},
  {val: 110, nome: "Plano 3D"},
  {val: 120, nome: "Espiral Fragmentada"},
  {val: 130, nome: "Cruz/Espiral"},
  {val: 145, nome: "Polígono"},
  {val: 160, nome: "Cortina Feixes"},
  {val: 175, nome: "Estrela Multi"},
  {val: 190, nome: "Flip Glitch"},
  {val: 215, nome: "Vórtice Espacial"}
];

let listaBumboArr = [70, 90, 110, 130, 40, 55];
let listaVocalArr = [25, 85, 120, 160, 190, 215];
let padraoAtualTeste = 70;
let autoScan = false;

let powerStateLaser = true;
function togglePowerLaser() {
  powerStateLaser = !powerStateLaser;
  const btn = document.getElementById('btnPowerLaser');
  btn.className = powerStateLaser ? 'success' : 'danger';
  btn.innerText = powerStateLaser ? '🟢 LASER LIGADO (Clique para Desligar Tudo)' : '🔴 LASER DESLIGADO (Clique para Ligar)';
  fetch('/power?val=' + (powerStateLaser ? 1 : 0));
}

function renderTags() {
  document.getElementById('inBumbo').value = listaBumboArr.join(',');
  document.getElementById('inVocal').value = listaVocalArr.join(',');

  const boxB = document.getElementById('tagsBumbo');
  boxB.innerHTML = '';
  todosPadroes.forEach(p => {
    const on = listaBumboArr.includes(p.val) ? 'on' : '';
    boxB.innerHTML += `<div class="tag-btn ${on}" onclick="toggleTag('bumbo', ${p.val})">${p.val}:${p.nome}</div>`;
  });

  const boxV = document.getElementById('tagsVocal');
  boxV.innerHTML = '';
  todosPadroes.forEach(p => {
    const on = listaVocalArr.includes(p.val) ? 'on' : '';
    boxV.innerHTML += `<div class="tag-btn ${on}" onclick="toggleTag('vocal', ${p.val})">${p.val}:${p.nome}</div>`;
  });
}

function setPadraoDireto(val) {
  padraoAtualTeste = parseInt(val);
  document.getElementById('lblPadraoGrande').innerText = 'CH5 (Padrão): ' + padraoAtualTeste;
  document.getElementById('vPadraoSlider').innerText = padraoAtualTeste;
  document.getElementById('rngPadrao').value = padraoAtualTeste;
  setModo(2);
  fetch('/setteste?p=' + padraoAtualTeste);
}

function setParamTeste(param, val, labelId) {
  document.getElementById(labelId).innerText = val;
  setModo(2);
  fetch('/paramteste?' + param + '=' + val);
}

function setDMX(ch, val, labelId) {
  document.getElementById(labelId).innerText = val;
  setModo(1);
  fetch('/dmx?ch=' + ch + '&val=' + val);
}

function navPadrao(delta) {
  let novo = constrain(padraoAtualTeste + delta, 0, 255);
  setPadraoDireto(novo);
}

function toggleAutoScan() {
  autoScan = !autoScan;
  const btn = document.getElementById('btnAutoScan');
  btn.innerText = autoScan ? '⏹️ Parar Auto-Scan' : '▶️ Iniciar Auto-Scan (2s)';
  btn.className = autoScan ? 'danger' : '';
  setModo(2);
  fetch('/autoscan?val=' + (autoScan ? 1 : 0));
}

function simulaKick() { setModo(2); fetch('/simulakick'); }
function simulaVocal() { setModo(2); fetch('/simulavocal'); }

function addPadraoPara(tipo) {
  let arr = (tipo === 'bumbo') ? listaBumboArr : listaVocalArr;
  if (!arr.includes(padraoAtualTeste)) {
    arr.push(padraoAtualTeste);
    if (tipo === 'bumbo') listaBumboArr = arr; else listaVocalArr = arr;
    renderTags();
    salvarListas();
    showToast('➕ Padrão ' + padraoAtualTeste + ' adicionado ao ' + tipo.toUpperCase() + '!');
  }
}

function toggleTag(tipo, val) {
  let arr = (tipo === 'bumbo') ? listaBumboArr : listaVocalArr;
  if (arr.includes(val)) {
    arr = arr.filter(x => x !== val);
  } else {
    arr.push(val);
  }
  if (tipo === 'bumbo') listaBumboArr = arr; else listaVocalArr = arr;
  renderTags();
  salvarListas();
}

function salvarListas() {
  const strB = document.getElementById('inBumbo').value;
  const strV = document.getElementById('inVocal').value;
  fetch('/setlistas?bumbo=' + encodeURIComponent(strB) + '&vocal=' + encodeURIComponent(strV));
}

function setBatidas(n) {
  ['b2','b4','b8'].forEach(id => document.getElementById(id).className = '');
  document.getElementById('b' + n).className = 'selected';
  document.getElementById('vBatidas').innerText = n + ' Batidas';
  fetch('/calib?bat=' + n);
}

function updateThresh(v) {
  const real = (v / 100.0).toFixed(2);
  document.getElementById('vThresh').innerText = real;
  fetch('/calib?thr=' + real);
}

function updateGlitch(v) {
  document.getElementById('vGlitch').innerText = v + ' ms';
  fetch('/calib?glt=' + v);
}

function setModo(m) {
  fetch('/modo?val=' + m);
  document.getElementById('btnAuto').className = (m === 0) ? 'active' : '';
  document.getElementById('btnManual').className = (m === 1) ? 'active' : '';
  document.getElementById('btnTeste').className = (m === 2) ? 'active' : 'warning';
}

function showToast(msg) {
  const toast = document.getElementById('toastMsg');
  toast.style.display = 'block';
  toast.innerText = msg;
  setTimeout(() => { toast.style.display = 'none'; }, 2500);
}

function salvarFlash() {
  salvarListas();
  fetch('/salvar').then(() => showToast('💾 Configurações salvas na Memória Flash!'));
}

function constrain(val, min, max) { return Math.min(Math.max(val, min), max); }
window.onload = renderTags;
</script>
</body>
</html>
)rawliteral";

void handlePowerLaser() {
  if (server.hasArg("val")) {
    sistemaLigado = (server.arg("val").toInt() == 1);
    if (!sistemaLigado) {
      dmxCanais[0] = 0; // Blackout Total
      enviarFrameDMX();
    } else {
      dmxCanais[0] = 50; // Reabre o laser
      enviarFrameDMX();
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleRoot() { server.sendHeader("Connection", "close"); server.send(200, "text/html", HTML_INDEX_LASER); }
void handleModo() {
  if (server.hasArg("val")) {
    modoOperacaoWeb = server.arg("val").toInt();
    if (modoOperacaoWeb == 1 || modoOperacaoWeb == 2) { dmxCanais[0] = 50; dmxCanais[14] = 255; }
  }
  server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK");
}
void handleSetTeste() {
  if (server.hasArg("p")) { padraoTesteAtual = (uint8_t)constrain(server.arg("p").toInt(), 0, 255); modoOperacaoWeb = 2; }
  server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK");
}
void handleParamTeste() {
  if (server.hasArg("z")) zoomTeste = constrain(server.arg("z").toInt(), 0, 255);
  if (server.hasArg("r")) rotacaoTeste = constrain(server.arg("r").toInt(), 0, 255);
  if (server.hasArg("c")) corTeste = constrain(server.arg("c").toInt(), 0, 255);
  modoOperacaoWeb = 2;
  server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK");
}
void handleDMX() {
  if (server.hasArg("ch") && server.hasArg("val")) {
    int ch = server.arg("ch").toInt(); int val = server.arg("val").toInt();
    if (ch >= 1 && ch <= 16) { dmxCanais[ch - 1] = constrain(val, 0, 255); modoOperacaoWeb = 1; }
  }
  server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK");
}
void handleAutoScan() {
  if (server.hasArg("val")) { autoScanAtivo = (server.arg("val").toInt() == 1); modoOperacaoWeb = 2; }
  server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK");
}
void handleSimulaKick() { simulaKickAte = millis() + 250; modoOperacaoWeb = 2; server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK"); }
void handleSimulaVocal() { simulaVocalAte = millis() + 200; modoOperacaoWeb = 2; server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK"); }
void handleSetListas() {
  if (server.hasArg("bumbo")) { parseListaString(server.arg("bumbo"), listaBumbo, totalListaBumbo); indiceBumbo = 0; }
  if (server.hasArg("vocal")) { parseListaString(server.arg("vocal"), listaVocal, totalListaVocal); indiceVocal = 0; }
  server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK");
}
void handleCalib() {
  if (server.hasArg("zmin")) calib.zoomMinimo = server.arg("zmin").toInt();
  if (server.hasArg("zmax")) calib.zoomMaximo = server.arg("zmax").toInt();
  if (server.hasArg("voc"))  calib.sensibilidadeVocal = server.arg("voc").toInt();
  if (server.hasArg("rot"))  calib.velocidadeRotacao = server.arg("rot").toInt();
  if (server.hasArg("bat"))  calib.batidasPorTroca = server.arg("bat").toInt();
  if (server.hasArg("thr"))  calib.thresholdVocal = server.arg("thr").toFloat();
  if (server.hasArg("glt"))  calib.duracaoGlitchMs = server.arg("glt").toInt();
  server.sendHeader("Connection", "close"); server.send(200, "text/plain", "OK");
}
void handleSalvar() {
  prefs.begin("laser_cfg", false);
  prefs.putInt("zmin", calib.zoomMinimo);
  prefs.putInt("zmax", calib.zoomMaximo);
  prefs.putInt("voc", calib.sensibilidadeVocal);
  prefs.putInt("rot", calib.velocidadeRotacao);
  prefs.putInt("bat", calib.batidasPorTroca);
  prefs.putFloat("thr", calib.thresholdVocal);
  prefs.putInt("glt", calib.duracaoGlitchMs);
  prefs.putString("lstB", listaParaString(listaBumbo, totalListaBumbo));
  prefs.putString("lstV", listaParaString(listaVocal, totalListaVocal));
  prefs.end();
  server.sendHeader("Connection", "close"); server.send(200, "text/plain", "SALVO");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  prefs.begin("laser_cfg", true);
  calib.zoomMinimo = prefs.getInt("zmin", 190);
  calib.zoomMaximo = prefs.getInt("zmax", 255);
  calib.sensibilidadeVocal = prefs.getInt("voc", 175);
  calib.velocidadeRotacao = prefs.getInt("rot", 170);
  calib.batidasPorTroca = prefs.getInt("bat", 4);
  calib.thresholdVocal = prefs.getFloat("thr", 0.32f);
  calib.duracaoGlitchMs = prefs.getInt("glt", 95);
  String strB = prefs.getString("lstB", "70,90,110,130,40,55"); parseListaString(strB, listaBumbo, totalListaBumbo);
  String strV = prefs.getString("lstV", "25,85,120,160,190,215"); parseListaString(strV, listaVocal, totalListaVocal);
  prefs.end();

  pinMode(PIN_LED_ONBOARD, OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_ONBOARD, !digitalRead(PIN_LED_ONBOARD));
    delay(150);
  }
  WiFi.setSleep(false);
  digitalWrite(PIN_LED_ONBOARD, LOW);

  inicializarDMX();

  server.on("/", handleRoot);
  server.on("/modo", handleModo);
  server.on("/power", handlePowerLaser);
  server.on("/calib", handleCalib);
  server.on("/setteste", handleSetTeste);
  server.on("/paramteste", handleParamTeste);
  server.on("/dmx", handleDMX);
  server.on("/autoscan", handleAutoScan);
  server.on("/simulakick", handleSimulaKick);
  server.on("/simulavocal", handleSimulaVocal);
  server.on("/setlistas", handleSetListas);
  server.on("/salvar", handleSalvar);
  server.begin();

  udp.begin(UDP_PORT);
}

unsigned long ultimoCicloMs = 0;

void loop() {
  unsigned long agora = millis();
  float tempo_s = agora / 1000.0f;
  float deltaTempo = max(0.001f, (agora - ultimoCicloMs) / 1000.0f);
  ultimoCicloMs = agora;

  server.handleClient();

  if (!sistemaLigado) {
    dmxCanais[0] = 0; // Força Blackout Total
    if (agora - ultimoEnvioDmx >= 33 && dmxInicializado) {
      ultimoEnvioDmx = agora;
      enviarFrameDMX();
    }
    delay(1);
    return;
  }

  if (modoOperacaoWeb == 2) {
    atualizarModoTeste(agora);
  }

  if (agora - ultimoEnvioDmx >= 33 && dmxInicializado) {
    ultimoEnvioDmx = agora;
    enviarFrameDMX();
  }

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
      StaticJsonDocument<1536> doc;
      DeserializationError error = deserializeJson(doc, packetBuffer);

      if (!error) {
        const char* tipo = doc["tipo"] | "audio";
        if (strcmp(tipo, "audio") == 0) {
          JsonObject faixas = doc["faixas"];
          JsonObject graves = faixas["graves"];
          JsonObject medios = faixas["medios"];
          JsonObject medios_graves = faixas["medios_graves"];
          JsonObject agudos = faixas["agudos"];

          bool pico_grave = graves["pico"] | false;
          float nivel_grave = graves["nivel"] | 0.0f;
          float nivel_med = medios["nivel"] | 0.0f;
          float nivel_med_grav = medios_graves["nivel"] | 0.0f;
          float nivel_agud = agudos["nivel"] | 0.0f;

          float nivel_vocal = (nivel_med * 0.70f) + (nivel_med_grav * 0.20f) + (nivel_agud * 0.10f);
          if (nivel_vocal < 0.05f) nivel_vocal = nivel_med;
          nivel_vocal = constrain(nivel_vocal, 0.0f, 1.0f);

          if (modoOperacaoWeb == 0) {
            atualizarLaserDMX_Audio(nivel_grave, pico_grave, nivel_vocal, tempo_s, deltaTempo, agora);
          }
        }
      }
    }
  }

  delay(1);
}
