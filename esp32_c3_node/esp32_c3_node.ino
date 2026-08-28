/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ RECEPTOR ESP32-C3 SUPER MINI (DMX512 STUDIO PRO)
 * VERSÃO: 5.3 (Mesa DMX Virtual de 16 Canais + Controle Total de Tamanho e Posição)
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
bool dmxInicializado = false;

// ------------------------------------------------------------------------------
// 2. MAPEAMENTO DE PINOS GPIO (ESP32-C3 Super Mini)
// ------------------------------------------------------------------------------
#define PIN_STROBE_BRANCO    0   // ULN2003 - Strobe nos Graves
#define PIN_GLOBO_R          4   // ULN2003 - Globo Vermelho
#define PIN_GLOBO_G          5   // ULN2003 - Globo Verde
#define PIN_GLOBO_B          6   // ULN2003 - Globo Azul
#define PIN_SERVO_GLOBO      7   // Sinal do Servo Motor SG90 (50Hz)

// MÓDULO RS-485 (MAX485) PARA DMX512
#define PIN_DMX_TX          21   // Conectado no DI (Data In) do MAX485
#define PIN_DMX_ENABLE      10   // Conectado no DE + RE do MAX485

#define PIN_LED_ONBOARD      8   // LED de status onboard

// ------------------------------------------------------------------------------
// 3. BUFFER DMX512 (16 Canais) & ESTADOS
// ------------------------------------------------------------------------------
uint8_t dmxCanais[16];
unsigned long ultimoEnvioDmx = 0;

// Modo de Operação: 0 = Áudio Automático, 1 = Mesa DMX Manual (16 Sliders), 2 = Bancada de Testes
int modoOperacaoWeb = 0;

#define MAX_PADROES 20

uint8_t listaBumbo[MAX_PADROES] = {70, 90, 110, 130, 40, 55};
int totalListaBumbo = 6;
int indiceBumbo = 0;

uint8_t listaVocal[MAX_PADROES] = {25, 85, 120, 160, 190, 215};
int totalListaVocal = 6;
int indiceVocal = 0;

struct ParametrosCalibracao {
  int zoomMinimo = 190;        // Tamanho de repouso (0-255)
  int zoomMaximo = 255;        // Tamanho no kick (0-255)
  int sensibilidadeVocal = 175;
  int velocidadeRotacao = 170;
  int batidasPorTroca = 4;
  float thresholdVocal = 0.32f;
  int duracaoGlitchMs = 95;
} calib;

// Cores Sólidas de Alto Contraste
const uint8_t coresLaser[] = {12, 22, 32, 42, 52, 62, 72};
const int totalCores = sizeof(coresLaser) / sizeof(coresLaser[0]);
int indiceCor = 0;

// Variáveis do Modo Bancada de Testes
uint8_t padraoTesteAtual = 70;
int zoomTeste = 220;
int rotacaoTeste = 170;
int corTeste = 12;
bool autoScanAtivo = false;
unsigned long ultimoAutoScan = 0;
unsigned long simulaKickAte = 0;
unsigned long simulaVocalAte = 0;

// Controle Rítmico Musical
unsigned long ultimoKickValido = 0;
const unsigned long COOLDOWN_KICK_MS = 260;
int contadorBatidasBumbo = 0;

float zoomAtual = 190.0f;
float ultimoNivelVocal = 0.0f;
unsigned long fimGlitchSilaba = 0;
unsigned long ultimoDisparoSilaba = 0;
unsigned long fimImpactoKick = 0;

// ------------------------------------------------------------------------------
// 4. VARIÁVEIS DE ESTADO E TEMPORIZAÇÃO
// ------------------------------------------------------------------------------
struct ContextoSpotify {
  bool ativo = false;
  bool tocando = false;
  float energia = 0.6;
  float danceabilidade = 0.6;
  String modo_sugerido = "fallback";
  unsigned long ultimo_timestamp = 0;
} spotify;

unsigned long fimPulsoStrobe = 0;
unsigned long ultimoPacoteAudio = 0;
unsigned long totalPacotesRecebidos = 0;

// Servo Motor SG90
unsigned long ultimoPulsoServo = 0;
int pulsoServoUs = 1500;
float ladoSaltoServo = 30.0f;

// ------------------------------------------------------------------------------
// 5. DRIVER DE TRANSMISSÃO DMX512 (UART a 250 kbps com Break/MAB)
// ------------------------------------------------------------------------------

void inicializarDMX() {
  pinMode(PIN_DMX_ENABLE, OUTPUT);
  digitalWrite(PIN_DMX_ENABLE, HIGH);
  pinMode(PIN_DMX_TX, OUTPUT);
  digitalWrite(PIN_DMX_TX, HIGH);

  memset(dmxCanais, 0, sizeof(dmxCanais));
  dmxCanais[0] = 50;   // CH1: Modo Manual Console (Luz Aberta)
  dmxCanais[1] = 128;  // CH2: Velocidade padrão
  dmxCanais[2] = 12;   // CH3: Cor Vermelha
  dmxCanais[3] = 0;    // CH4: Sem fluxo
  dmxCanais[4] = 70;   // CH5: Túnel
  dmxCanais[5] = 210;  // CH6: Tamanho Grande
  dmxCanais[6] = 0;    // CH7: Zoom manual (CH6 ativo)
  dmxCanais[7] = 170;  // CH8: Rotação suave
  dmxCanais[8] = 64;   // CH9: Flip H Centro
  dmxCanais[9] = 64;   // CH10: Flip V Centro
  dmxCanais[10] = 64;  // CH11: Posição X Centro
  dmxCanais[11] = 64;  // CH12: Posição Y Centro
  dmxCanais[12] = 0;   // CH13: Sem onda
  dmxCanais[13] = 0;   // CH14: Sem desenho gradual
  dmxCanais[14] = 255; // CH15: Velocidade máxima dos espelhos
  dmxCanais[15] = 0;   // CH16: Exibição padrão contínua

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

void atualizarLaserDMX_Audio(String modo, float nivel_graves, bool pico_grave, float nivel_vocal, float tempo_s, float deltaTempo, unsigned long agora) {
  dmxCanais[0] = 50;  // Luz sempre aberta
  dmxCanais[1] = 128;
  dmxCanais[13] = 0;
  dmxCanais[14] = 255;
  dmxCanais[15] = 0;
  dmxCanais[6] = 0;

  // Gatilho Silábico
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

  // Processamento do Bumbo
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

// ------------------------------------------------------------------------------
// 6. MODO BANCADA DE TESTES & SCANNER
// ------------------------------------------------------------------------------
void atualizarModoTeste(unsigned long agora) {
  if (autoScanAtivo && (agora - ultimoAutoScan >= 2000)) {
    ultimoAutoScan = agora;
    padraoTesteAtual = (padraoTesteAtual + 1) % 256;
  }

  dmxCanais[0] = 50;  // Luz ativa
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
// 7. FUNÇÕES DE STROBE, GLOBO RGB E SERVO SG90
// ------------------------------------------------------------------------------

void setStrobe(int intensidade_0_a_255) {
  intensidade_0_a_255 = constrain(intensidade_0_a_255, 0, 255);
  analogWrite(PIN_STROBE_BRANCO, intensidade_0_a_255);
}

void setGloboRGB(float r_pct, float g_pct, float b_pct) {
  int valR = (int)(constrain(r_pct, 0.0f, 100.0f) * 2.55f);
  int valG = (int)(constrain(g_pct, 0.0f, 100.0f) * 2.55f);
  int valB = (int)(constrain(b_pct, 0.0f, 100.0f) * 2.55f);

  analogWrite(PIN_GLOBO_R, valR);
  analogWrite(PIN_GLOBO_G, valG);
  analogWrite(PIN_GLOBO_B, valB);
}

void setServoAngulo(float graus) {
  graus = constrain(graus, 0.0f, 180.0f);
  pulsoServoUs = 550 + (int)((graus / 180.0f) * (2400 - 550));
}

void setServoVarredura(float freq_hz, float ang_min, float ang_max, float tempo_s) {
  float seno = (sin(2.0f * PI * freq_hz * tempo_s) + 1.0f) / 2.0f;
  float angulo = ang_min + (seno * (ang_max - ang_min));
  setServoAngulo(angulo);
}

void desligarTudo() {
  setStrobe(0);
  setGloboRGB(0, 0, 0);
  setServoAngulo(90.0f);
}

void atualizarServo(unsigned long agoraUs) {
  if (agoraUs - ultimoPulsoServo >= 20000) {
    ultimoPulsoServo = agoraUs;
    digitalWrite(PIN_SERVO_GLOBO, HIGH);
    delayMicroseconds(pulsoServoUs);
    digitalWrite(PIN_SERVO_GLOBO, LOW);
  }
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
// 8. PÁGINA WEB HTML / CSS / JS EMBUTIDA
// ------------------------------------------------------------------------------

const char HTML_INDEX[] PROGMEM = R"rawliteral(
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
  <h1>⚡ Laser DMX Studio Pro</h1>
  <div class="subtitle">Áudio Automático | Mesa DMX 16 Canais | Bancada de Testes</div>

  <div class="card">
    <div class="card-title">🎮 Modo de Operação</div>
    <div class="btn-group">
      <button id="btnAuto" class="active" onclick="setModo(0)">🎵 1. Áudio Automático</button>
      <button id="btnManual" onclick="setModo(1)">🎛️ 2. Mesa DMX (16 Canais)</button>
      <button id="btnTeste" class="warning" onclick="setModo(2)">🔬 3. Bancada de Testes</button>
    </div>
  </div>

  <!-- MODO 3: BANCADA DE TESTES & SCANNER -->
  <div class="card" id="cardTeste" style="border: 2px solid var(--warning);">
    <div class="card-title" style="color: var(--warning);">🔬 3. Bancada de Testes / Scanner (Sem Música)</div>
    
    <div class="big-badge" id="lblPadraoGrande">CH5 (Padrão): 70</div>

    <div class="btn-group">
      <button onclick="navPadrao(-1)">◀ Anterior (-1)</button>
      <button onclick="navPadrao(-5)">⏪ -5</button>
      <button onclick="navPadrao(+5)">+5 ⏩</button>
      <button onclick="navPadrao(+1)">Próximo (+1) ▶</button>
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>CH5: Padrão / Gobo</span><span class="slider-val" id="vPadraoSlider">70</span></div>
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
      <div class="slider-header"><span>CH3: Cor (12=Vm, 22=Vd, 32=Az, 42=Am, 52=Ciano, 72=Branco)</span><span class="slider-val" id="vCorTeste">12</span></div>
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

  <!-- MODO 2: MESA DMX VIRTUAL (16 CANAIS INDIVIDUAIS) -->
  <div class="card" id="cardMesaDMX">
    <div class="card-title">🎛️ 2. Mesa DMX Virtual (16 Canais Individuais do Laser)</div>
    
    <div class="slider-row">
      <div class="slider-header"><span>CH1: Modo de Operação (50=Manual Console DMX)</span><span class="slider-val" id="vCH1">50</span></div>
      <input type="range" min="0" max="255" value="50" oninput="setDMX(1, this.value, 'vCH1')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH3: Cor do Laser (10-79=Fixas, 80-255=Efeitos)</span><span class="slider-val" id="vCH3">12</span></div>
      <input type="range" min="0" max="255" value="12" oninput="setDMX(3, this.value, 'vCH3')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH5: Seleção de Padrão / Desenho</span><span class="slider-val" id="vCH5">70</span></div>
      <input type="range" min="0" max="255" value="70" oninput="setDMX(5, this.value, 'vCH5')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH6: Tamanho / Zoom Manual do Desenho</span><span class="slider-val" id="vCH6">210</span></div>
      <input type="range" min="0" max="255" value="210" oninput="setDMX(6, this.value, 'vCH6')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH8: Rotação no Centro (0=Fixo, 130-255=Giro)</span><span class="slider-val" id="vCH8">170</span></div>
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
      <div class="slider-header"><span>CH13: Onda no Eixo X (Osciloscópio)</span><span class="slider-val" id="vCH13">0</span></div>
      <input type="range" min="0" max="255" value="0" oninput="setDMX(13, this.value, 'vCH13')">
    </div>
  </div>

  <!-- MODO 1: ÁUDIO AUTOMÁTICO E LISTAS -->
  <div class="card">
    <div class="card-title">🥁 1. Lista de Padrões do Bumbo (Fundo Musical)</div>
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
    <div class="card-title">⚙️ Calibração de Tamanho do Modo Musical</div>
    <div class="slider-row">
      <div class="slider-header"><span>Tamanho Mínimo (Base Ampla)</span><span class="slider-val" id="vZoomMin">190</span></div>
      <input type="range" min="100" max="240" value="190" oninput="updateCalib('zmin', this.value, 'vZoomMin')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>Tamanho Máximo (Pico do Bumbo)</span><span class="slider-val" id="vZoomMax">255</span></div>
      <input type="range" min="180" max="255" value="255" oninput="updateCalib('zmax', this.value, 'vZoomMax')">
    </div>
  </div>

  <div class="card">
    <div class="card-title">💾 Salvar & Exportar</div>
    <div class="btn-group">
      <button class="success" onclick="salvarFlash()">💾 Salvar na Memória do ESP32</button>
      <button onclick="copiarConfigs()">📋 Copiar Configurações</button>
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

function simulaKick() {
  setModo(2);
  fetch('/simulakick');
}

function simulaVocal() {
  setModo(2);
  fetch('/simulavocal');
}

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

function updateCalib(param, val, labelId) {
  document.getElementById(labelId).innerText = val;
  fetch('/calib?' + param + '=' + val);
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

function copiarConfigs() {
  const obj = { bumbo: listaBumboArr, vocal: listaVocalArr };
  navigator.clipboard.writeText(JSON.stringify(obj, null, 2));
  showToast('📋 Listas copiadas para a Área de Transferência!');
}

function constrain(val, min, max) {
  return Math.min(Math.max(val, min), max);
}

window.onload = renderTags;
</script>
</body>
</html>
)rawliteral";

// ------------------------------------------------------------------------------
// 9. ROTAS DO WEB SERVER
// ------------------------------------------------------------------------------

void handleRoot() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", HTML_INDEX);
}

void handleModo() {
  if (server.hasArg("val")) {
    modoOperacaoWeb = server.arg("val").toInt();
    if (modoOperacaoWeb == 1 || modoOperacaoWeb == 2) {
      dmxCanais[0] = 50;
      dmxCanais[14] = 255;
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleSetTeste() {
  if (server.hasArg("p")) {
    padraoTesteAtual = (uint8_t)constrain(server.arg("p").toInt(), 0, 255);
    modoOperacaoWeb = 2;
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleParamTeste() {
  if (server.hasArg("z")) zoomTeste = constrain(server.arg("z").toInt(), 0, 255);
  if (server.hasArg("r")) rotacaoTeste = constrain(server.arg("r").toInt(), 0, 255);
  if (server.hasArg("c")) corTeste = constrain(server.arg("c").toInt(), 0, 255);
  modoOperacaoWeb = 2;
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleDMX() {
  if (server.hasArg("ch") && server.hasArg("val")) {
    int ch = server.arg("ch").toInt();
    int val = server.arg("val").toInt();
    if (ch >= 1 && ch <= 16) {
      dmxCanais[ch - 1] = constrain(val, 0, 255);
      modoOperacaoWeb = 1; // Força modo mesa DMX manual
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleAutoScan() {
  if (server.hasArg("val")) {
    autoScanAtivo = (server.arg("val").toInt() == 1);
    modoOperacaoWeb = 2;
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleSimulaKick() {
  simulaKickAte = millis() + 250;
  modoOperacaoWeb = 2;
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleSimulaVocal() {
  simulaVocalAte = millis() + 200;
  modoOperacaoWeb = 2;
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleSetListas() {
  if (server.hasArg("bumbo")) {
    parseListaString(server.arg("bumbo"), listaBumbo, totalListaBumbo);
    indiceBumbo = 0;
  }
  if (server.hasArg("vocal")) {
    parseListaString(server.arg("vocal"), listaVocal, totalListaVocal);
    indiceVocal = 0;
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleCalib() {
  if (server.hasArg("zmin")) calib.zoomMinimo = server.arg("zmin").toInt();
  if (server.hasArg("zmax")) calib.zoomMaximo = server.arg("zmax").toInt();
  if (server.hasArg("voc"))  calib.sensibilidadeVocal = server.arg("voc").toInt();
  if (server.hasArg("rot"))  calib.velocidadeRotacao = server.arg("rot").toInt();
  if (server.hasArg("bat"))  calib.batidasPorTroca = server.arg("bat").toInt();
  if (server.hasArg("thr"))  calib.thresholdVocal = server.arg("thr").toFloat();
  if (server.hasArg("glt"))  calib.duracaoGlitchMs = server.arg("glt").toInt();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
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
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "SALVO");
}

// ------------------------------------------------------------------------------
// 10. SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==================================================");
  Serial.println(" Audio to Light - ESP32-C3 Studio v5.3 (Mesa DMX 16)");
  Serial.println("==================================================");

  prefs.begin("laser_cfg", true);
  calib.zoomMinimo = prefs.getInt("zmin", 190);
  calib.zoomMaximo = prefs.getInt("zmax", 255);
  calib.sensibilidadeVocal = prefs.getInt("voc", 175);
  calib.velocidadeRotacao = prefs.getInt("rot", 170);
  calib.batidasPorTroca = prefs.getInt("bat", 4);
  calib.thresholdVocal = prefs.getFloat("thr", 0.32f);
  calib.duracaoGlitchMs = prefs.getInt("glt", 95);

  String strB = prefs.getString("lstB", "70,90,110,130,40,55");
  parseListaString(strB, listaBumbo, totalListaBumbo);

  String strV = prefs.getString("lstV", "25,85,120,160,190,215");
  parseListaString(strV, listaVocal, totalListaVocal);
  prefs.end();

  pinMode(PIN_STROBE_BRANCO, OUTPUT);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  setStrobe(255); delay(200); setStrobe(0);
  setGloboRGB(100, 0, 0); delay(100);
  setGloboRGB(0, 100, 0); delay(100);
  setGloboRGB(0, 0, 100); delay(100);
  setGloboRGB(0, 0, 0);

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
  Serial.printf("\n[Wi-Fi] Conectado! IP do ESP32: %s\n", WiFi.localIP().toString().c_str());

  inicializarDMX();

  // ROTAS WEB
  server.on("/", handleRoot);
  server.on("/modo", handleModo);
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
  Serial.printf("[WEB] Painel de Controle ativo em: http://%s\n", WiFi.localIP().toString().c_str());

  udp.begin(UDP_PORT);
  Serial.printf("[UDP] Escutando porta %d...\n", UDP_PORT);
}

// ------------------------------------------------------------------------------
// 11. LOOP PRINCIPAL
// ------------------------------------------------------------------------------
unsigned long ultimoCicloMs = 0;

void loop() {
  unsigned long agora = millis();
  unsigned long agoraUs = micros();
  float tempo_s = agora / 1000.0f;
  float deltaTempo = max(0.001f, (agora - ultimoCicloMs) / 1000.0f);
  ultimoCicloMs = agora;

  server.handleClient();

  atualizarServo(agoraUs);

  // MODO 2: BANCADA DE TESTES
  if (modoOperacaoWeb == 2) {
    atualizarModoTeste(agora);
  }

  // TRANSMISSÃO CONTÍNUA DMX512 (a 30Hz)
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

          // 1. MODO 0: ÁUDIO AUTOMÁTICO (Só atualiza DMX se estiver no Modo 0)
          if (modoOperacaoWeb == 0) {
            atualizarLaserDMX_Audio(modo_atual, nivel_grave, pico_grave, nivel_vocal, tempo_s, deltaTempo, agora);
          }

          // 2. GLOBO RGB
          atualizarPaletaGlobo(modo_atual, nivel_vocal, tempo_s);

          // 3. SERVO SG90
          if (modo_atual == "alta_energia") {
            if (pico_grave && nivel_grave >= 0.70f) {
              ladoSaltoServo = (ladoSaltoServo <= 90.0f) ? 150.0f : 30.0f;
              setServoAngulo(ladoSaltoServo);
            } else {
              setServoVarredura(1.0f, 20.0f, 160.0f, tempo_s);
            }
          } 
          else if (modo_atual == "suave") {
            setServoVarredura(0.2f, 50.0f, 130.0f, tempo_s);
          } 
          else if (modo_atual == "standby") {
            if (ativo_grave || pico_grave) {
              setServoVarredura(0.4f, 45.0f, 135.0f, tempo_s);
            } else {
              setServoAngulo(90.0f);
            }
          } 
          else {
            setServoVarredura(0.5f, 30.0f, 150.0f, tempo_s);
          }

          // 4. STROBE BRANCO (GRAVES)
          if (modo_atual == "suave") {
            if (ativo_grave || nivel_grave > 0.3f) {
              setStrobe((int)(min(35.0f, nivel_grave * 35.0f) * 2.55f));
              fimPulsoStrobe = agora + 70;
            }
          } else {
            if (ativo_grave || pico_grave || nivel_grave >= 0.40f) {
              int valStrobe = (modo_atual == "alta_energia") ? 255 : 200;
              setStrobe(valStrobe);
              fimPulsoStrobe = agora + 70;
            }
          }
        }
      }
    }
  }

  if (agora >= fimPulsoStrobe) {
    setStrobe(0);
  }

  delay(1);
}
