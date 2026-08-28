/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ RECEPTOR ESP32-C3 SUPER MINI (COM DMX512 + WEB SERVER)
 * VERSÃO: 3.6 (Animações Grandes Contínuas + Reações Malucas no Beat + Copiar Configs)
 * ==============================================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>

// ------------------------------------------------------------------------------
// 1. CONFIGURAÇÃO WI-FI, UDP, WEB SERVER & PREFERENCES (NVS FLASH)
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
// 3. BUFFER DMX512 & PARÂMETROS DE CALIBRAÇÃO (COM MEMÓRIA FLASH PERSISTENTE)
// ------------------------------------------------------------------------------
uint8_t dmxCanais[16];
unsigned long ultimoEnvioDmx = 0;

int modoOperacaoWeb = 0; // 0 = Áudio Automático, 1 = Manual Web

struct ParametrosCalibracao {
  int zoomMinimo = 190;        // Animação GRANDE por padrão (190 a 240)
  int zoomMaximo = 255;        // Explosão no impacto
  int sensibilidadeVocal = 190;// Ondulação na voz
  int velocidadeRotacao = 180; // Rotação fluida majestosa
  int padraoBase = 70;         // Padrão amplo inicial (Túnel / Plano)
} calib;

// Lista de Padrões Grandes e Imersivos (Túnel, Planos, Espirais, Caixas 3D, Ondas)
const uint8_t padroesGrandes[] = {70, 90, 110, 130, 40, 55, 145, 175};
const int totalPadroesGrandes = sizeof(padroesGrandes) / sizeof(padroesGrandes[0]);
int indicePadraoGrande = 0;

// Lista de Padrões Malucos / Reativos (Figuras rápidas, estrelas, glitches, túneis múltiplos)
const uint8_t padroesMalucos[] = {25, 85, 120, 160, 190, 215, 235};
const int totalPadroesMalucos = sizeof(padroesMalucos) / sizeof(padroesMalucos[0]);
int indicePadraoMaluco = 0;

const uint8_t coresLaser[] = {12, 22, 32, 42, 52, 62, 72};
const int totalCores = sizeof(coresLaser) / sizeof(coresLaser[0]);
int indiceCor = 0;

int contadorBatidas = 0;
float zoomAtual = 190.0f;
unsigned long fimReacaoMaluca = 0;

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

void atualizarLaserDMX_Audio(String modo, float nivel_graves, bool pico_grave, float nivel_vocal, float tempo_s, float deltaTempo, unsigned long agora) {
  if (modo == "standby") {
    dmxCanais[0] = 0;
    dmxCanais[1] = 0;
    dmxCanais[2] = 0;
    dmxCanais[4] = 0;
    return;
  }

  dmxCanais[0] = 50;  // Manual Console
  dmxCanais[1] = 128;
  dmxCanais[13] = 0;
  dmxCanais[14] = 255;
  dmxCanais[15] = 0;
  dmxCanais[6] = 0;

  // -------------------------------------------------------------------------
  // A. DETECÇÃO DE EVENTO / REAÇÃO MALUCA NOS IMPACTOS
  // -------------------------------------------------------------------------
  if (pico_grave) {
    indiceCor = (indiceCor + 1) % totalCores; // Troca a cor no bumbo
    contadorBatidas++;
    
    // Dispara "Reação Maluca" temporária por 250ms
    fimReacaoMaluca = agora + 260;
    indicePadraoMaluco = (indicePadraoMaluco + 1) % totalPadroesMalucos;

    // A cada 8 batidas no fluxo normal, troca a grande animação de fundo
    if (contadorBatidas >= 8) {
      indicePadraoGrande = (indicePadraoGrande + 1) % totalPadroesGrandes;
      contadorBatidas = 0;
    }
  }

  dmxCanais[2] = coresLaser[indiceCor];
  dmxCanais[3] = 0;

  // -------------------------------------------------------------------------
  // B. SELEÇÃO DE PADRÃO: ANIMAÇÃO GRANDE vs REAÇÃO MALUCA
  // -------------------------------------------------------------------------
  bool emReacao = (agora < fimReacaoMaluca);

  if (emReacao) {
    // REAÇÃO MALUCA: padrão complexo, flip horizontal rápido, rotação extrema
    dmxCanais[4] = padroesMalucos[indicePadraoMaluco];
    dmxCanais[7] = 245; // Rotação rápida burst
    dmxCanais[8] = 180; // Flip horizontal que rebate o laser
  } else {
    // ANIMAÇÃO GRANDE PADRÃO: fluxo majestoso, túneis e planos contínuos
    dmxCanais[4] = padroesGrandes[indicePadraoGrande];
    dmxCanais[7] = (uint8_t)calib.velocidadeRotacao; // Rotação fluida suave
    dmxCanais[8] = 64;  // Normal
  }

  // -------------------------------------------------------------------------
  // C. TAMANHO AMPLO POR PADRÃO (ZOOM MINIMO GRANDE: 190 A 255)
  // -------------------------------------------------------------------------
  float zoomAlvo = (float)calib.zoomMinimo + (nivel_graves * ((float)calib.zoomMaximo - (float)calib.zoomMinimo));
  if (pico_grave) {
    zoomAlvo = (float)calib.zoomMaximo;
  }

  if (zoomAlvo > zoomAtual) {
    zoomAtual = zoomAlvo;
  } else {
    zoomAtual = max((float)calib.zoomMinimo, zoomAtual - (120.0f * deltaTempo));
  }

  dmxCanais[5] = (uint8_t)constrain(zoomAtual, (float)calib.zoomMinimo, 255.0f);

  // -------------------------------------------------------------------------
  // D. ONDULAÇÃO NO VOCAL & VARREDURA PANORÂMICA
  // -------------------------------------------------------------------------
  float vocal_curva = constrain(pow(nivel_vocal, 0.70f), 0.0f, 1.0f);
  dmxCanais[12] = (uint8_t)(vocal_curva * (float)calib.sensibilidadeVocal);

  // Varredura espacial panorâmica elegante que cobre o ambiente
  dmxCanais[10] = (uint8_t)(64 + sin(tempo_s * 0.5f) * 20); // Movimento X suave
  dmxCanais[11] = 64;
  dmxCanais[9] = 64;
}

// ------------------------------------------------------------------------------
// 6. FUNÇÕES DE STROBE, GLOBO RGB E SERVO SG90
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
  dmxCanais[0] = 0;
  enviarFrameDMX();
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
// 7. PÁGINA WEB HTML / CSS / JS EMBUTIDA COM EXPORTADOR DE CONFIGS
// ------------------------------------------------------------------------------

const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Laser DMX Studio & Calibrator</title>
<style>
  :root { --bg: #0b1329; --card: #17233f; --primary: #38bdf8; --accent: #f43f5e; --success: #10b981; --text: #f8fafc; }
  body { background: var(--bg); color: var(--text); font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 12px; }
  .container { max-width: 650px; margin: 0 auto; }
  h1 { text-align: center; color: var(--primary); font-size: 1.4rem; margin-bottom: 2px; }
  .subtitle { text-align: center; color: #94a3b8; font-size: 0.8rem; margin-bottom: 16px; }
  .card { background: var(--card); border-radius: 12px; padding: 14px; margin-bottom: 14px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); border: 1px solid #223254; }
  .card-title { font-weight: bold; color: var(--primary); font-size: 1.05rem; margin-bottom: 10px; border-bottom: 1px solid #2a3d66; padding-bottom: 6px; }
  .btn-group { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 10px; }
  button { background: #24355a; color: white; border: none; padding: 10px 12px; border-radius: 8px; font-weight: bold; cursor: pointer; flex: 1; min-width: 110px; transition: all 0.2s; font-size: 0.85rem; }
  button:hover { background: #324775; }
  button.active { background: var(--primary); color: #0b1329; }
  button.success { background: var(--success); color: white; }
  button.danger { background: var(--accent); }
  .slider-row { margin-bottom: 10px; }
  .slider-header { display: flex; justify-content: space-between; font-size: 0.85rem; margin-bottom: 4px; }
  .slider-val { font-weight: bold; color: var(--primary); }
  input[type=range] { width: 100%; height: 8px; border-radius: 4px; background: #24355a; outline: none; -webkit-appearance: none; }
  input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: var(--primary); cursor: pointer; }
  textarea { width: 100%; height: 110px; background: #0b1329; border: 1px solid #2a3d66; border-radius: 8px; color: #a5f3fc; font-family: monospace; font-size: 0.8rem; padding: 8px; box-sizing: border-box; resize: none; }
  .toast { display: none; background: var(--success); color: white; text-align: center; padding: 8px; border-radius: 6px; font-weight: bold; margin-top: 8px; font-size: 0.85rem; }
</style>
</head>
<body>
<div class="container">
  <h1>⚡ Laser DMX Studio</h1>
  <div class="subtitle">Animações Grandes + Reações no Beat + Exportador</div>

  <div class="card">
    <div class="card-title">🎮 Modo de Operação</div>
    <div class="btn-group">
      <button id="btnAuto" class="active" onclick="setModo(0)">🎵 Modo Automático (Áudio)</button>
      <button id="btnManual" onclick="setModo(1)">🎛️ Modo Manual (Sliders)</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">⚙️ Parâmetros de Ritmo & Tamanho</div>
    <div class="slider-row">
      <div class="slider-header"><span>Tamanho Mínimo (Animação Grande Padrão)</span><span class="slider-val" id="vZoomMin">190</span></div>
      <input type="range" min="100" max="240" value="190" oninput="updateCalib('zmin', this.value, 'vZoomMin')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>Tamanho Máximo (Explosão no Bumbo)</span><span class="slider-val" id="vZoomMax">255</span></div>
      <input type="range" min="180" max="255" value="255" oninput="updateCalib('zmax', this.value, 'vZoomMax')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>Sensibilidade Vocal (Ondulação X)</span><span class="slider-val" id="vVocal">190</span></div>
      <input type="range" min="0" max="255" value="190" oninput="updateCalib('voc', this.value, 'vVocal')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>Velocidade de Rotação 3D</span><span class="slider-val" id="vRot">180</span></div>
      <input type="range" min="100" max="255" value="180" oninput="updateCalib('rot', this.value, 'vRot')">
    </div>
  </div>

  <div class="card">
    <div class="card-title">🎛️ Teste Rápido de Padrões & Sliders DMX</div>
    <div class="btn-group">
      <button onclick="setAtalho(70)">🌀 Túnel Grande</button>
      <button onclick="setAtalho(110)">📐 Plano 3D</button>
      <button onclick="setAtalho(130)">✨ Espiral</button>
      <button onclick="setAtalho(25)">⚡ Zigzag Reativo</button>
      <button class="danger" onclick="setBlackout()">⬛ Blackout</button>
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>CH1: Modo</span><span class="slider-val" id="vCH1">50</span></div>
      <input type="range" min="0" max="255" value="50" oninput="setDMX(1, this.value, 'vCH1')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH3: Cor</span><span class="slider-val" id="vCH3">12</span></div>
      <input type="range" min="0" max="255" value="12" oninput="setDMX(3, this.value, 'vCH3')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH5: Padrão / Gobo</span><span class="slider-val" id="vCH5">70</span></div>
      <input type="range" min="0" max="255" value="70" oninput="setDMX(5, this.value, 'vCH5')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH6: Tamanho</span><span class="slider-val" id="vCH6">210</span></div>
      <input type="range" min="0" max="255" value="210" oninput="setDMX(6, this.value, 'vCH6')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH8: Rotação</span><span class="slider-val" id="vCH8">180</span></div>
      <input type="range" min="0" max="255" value="180" oninput="setDMX(8, this.value, 'vCH8')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH13: Onda X</span><span class="slider-val" id="vCH13">0</span></div>
      <input type="range" min="0" max="255" value="0" oninput="setDMX(13, this.value, 'vCH13')">
    </div>
  </div>

  <div class="card">
    <div class="card-title">📋 Exportar / Salvar Configurações</div>
    <textarea id="txtConfigs" readonly></textarea>
    <div class="btn-group" style="margin-top: 8px;">
      <button class="success" onclick="copiarConfigs()">📋 Copiar Configurações</button>
      <button onclick="salvarFlash()">💾 Salvar na Memória do ESP32</button>
    </div>
    <div id="toastMsg" class="toast">✅ Configurações copiadas com sucesso!</div>
  </div>
</div>

<script>
let configObj = {
  zmin: 190, zmax: 255, voc: 190, rot: 180,
  dmx: { ch1: 50, ch3: 12, ch5: 70, ch6: 210, ch8: 180, ch13: 0 }
};

function renderJson() {
  document.getElementById('txtConfigs').value = JSON.stringify(configObj, null, 2);
}

function setModo(m) {
  fetch('/modo?val=' + m);
  document.getElementById('btnAuto').className = (m === 0) ? 'active' : '';
  document.getElementById('btnManual').className = (m === 1) ? 'active' : '';
}

function updateCalib(param, val, labelId) {
  document.getElementById(labelId).innerText = val;
  configObj[param] = parseInt(val);
  renderJson();
  fetch('/calib?' + param + '=' + val);
}

function setDMX(ch, val, labelId) {
  document.getElementById(labelId).innerText = val;
  configObj.dmx['ch' + ch] = parseInt(val);
  renderJson();
  fetch('/dmx?ch=' + ch + '&val=' + val);
}

function setAtalho(padrao) {
  setModo(1);
  document.getElementById('vCH5').innerText = padrao;
  configObj.dmx.ch5 = padrao;
  renderJson();
  fetch('/dmx?ch=5&val=' + padrao);
}

function setBlackout() {
  setModo(1);
  document.getElementById('vCH1').innerText = 0;
  configObj.dmx.ch1 = 0;
  renderJson();
  fetch('/dmx?ch=1&val=0');
}

function copiarConfigs() {
  const el = document.getElementById('txtConfigs');
  el.select();
  navigator.clipboard.writeText(el.value);
  const toast = document.getElementById('toastMsg');
  toast.style.display = 'block';
  toast.innerText = '✅ Configurações copiadas para a Área de Transferência!';
  setTimeout(() => { toast.style.display = 'none'; }, 2500);
}

function salvarFlash() {
  fetch('/salvar').then(() => {
    const toast = document.getElementById('toastMsg');
    toast.style.display = 'block';
    toast.innerText = '💾 Salvo com sucesso na memória Flash do ESP32!';
    setTimeout(() => { toast.style.display = 'none'; }, 2500);
  });
}

window.onload = renderJson;
</script>
</body>
</html>
)rawliteral";

// ------------------------------------------------------------------------------
// 8. ROTAS DO WEB SERVER
// ------------------------------------------------------------------------------

void handleRoot() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", HTML_INDEX);
}

void handleModo() {
  if (server.hasArg("val")) {
    modoOperacaoWeb = server.arg("val").toInt();
    if (modoOperacaoWeb == 1) {
      dmxCanais[0] = 50;
      dmxCanais[14] = 255;
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleCalib() {
  if (server.hasArg("zmin")) calib.zoomMinimo = server.arg("zmin").toInt();
  if (server.hasArg("zmax")) calib.zoomMaximo = server.arg("zmax").toInt();
  if (server.hasArg("voc"))  calib.sensibilidadeVocal = server.arg("voc").toInt();
  if (server.hasArg("rot"))  calib.velocidadeRotacao = server.arg("rot").toInt();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleDMX() {
  if (server.hasArg("ch") && server.hasArg("val")) {
    int ch = server.arg("ch").toInt();
    int val = server.arg("val").toInt();
    if (ch >= 1 && ch <= 16) {
      dmxCanais[ch - 1] = constrain(val, 0, 255);
      modoOperacaoWeb = 1;
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleSalvar() {
  prefs.begin("laser_cfg", false);
  prefs.putInt("zmin", calib.zoomMinimo);
  prefs.putInt("zmax", calib.zoomMaximo);
  prefs.putInt("voc", calib.sensibilidadeVocal);
  prefs.putInt("rot", calib.velocidadeRotacao);
  prefs.end();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "SALVO");
}

// ------------------------------------------------------------------------------
// 9. SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==================================================");
  Serial.println(" Audio to Light - ESP32-C3 Studio (Grandes + Reativos)");
  Serial.println("==================================================");

  // Carrega configurações salvas na Flash se existirem
  prefs.begin("laser_cfg", true);
  calib.zoomMinimo = prefs.getInt("zmin", 190);
  calib.zoomMaximo = prefs.getInt("zmax", 255);
  calib.sensibilidadeVocal = prefs.getInt("voc", 190);
  calib.velocidadeRotacao = prefs.getInt("rot", 180);
  prefs.end();

  pinMode(PIN_STROBE_BRANCO, OUTPUT);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  // AUTO-TESTE
  setStrobe(255); delay(200); setStrobe(0);
  setGloboRGB(100, 0, 0); delay(100);
  setGloboRGB(0, 100, 0); delay(100);
  setGloboRGB(0, 0, 100); delay(100);
  setGloboRGB(0, 0, 0);

  // CONEXÃO WI-FI
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

  // INICIALIZA DMX APÓS WI-FI
  inicializarDMX();

  // ROTAS WEB
  server.on("/", handleRoot);
  server.on("/modo", handleModo);
  server.on("/calib", handleCalib);
  server.on("/dmx", handleDMX);
  server.on("/salvar", handleSalvar);
  server.begin();
  Serial.printf("[WEB] Painel de Controle ativo em: http://%s\n", WiFi.localIP().toString().c_str());

  // SOCKET UDP
  udp.begin(UDP_PORT);
  Serial.printf("[UDP] Escutando porta %d...\n", UDP_PORT);
}

// ------------------------------------------------------------------------------
// 10. LOOP PRINCIPAL
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

  // Envio contínuo DMX512 a ~30Hz (a cada 33ms)
  if (agora - ultimoEnvioDmx >= 33 && dmxInicializado) {
    ultimoEnvioDmx = agora;
    enviarFrameDMX();
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(100);
    return;
  }

  // LEITURA DOS PACOTES UDP
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
          bool ativo_agudo = agudos["ativo"] | false;

          float nivel_vocal = (nivel_med * 0.70f) + (nivel_med_grav * 0.20f) + (nivel_agud * 0.10f);
          if (nivel_vocal < 0.05f) nivel_vocal = nivel_med;
          nivel_vocal = constrain(nivel_vocal, 0.0f, 1.0f);

          // 1. ATUALIZA LASER DMX (Grandes Animações Contínuas + Reações Malucas no Beat)
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
            if (ativo_grave || pico_grave || ativo_agudo) {
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

  if (agora - ultimoPacoteAudio > 4000 && ultimoPacoteAudio > 0 && modoOperacaoWeb == 0) {
    desligarTudo();
  }

  delay(1);
}
