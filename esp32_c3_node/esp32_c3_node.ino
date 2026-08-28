/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ RECEPTOR ESP32-C3 SUPER MINI (COM DMX512 + WEB SERVER)
 * VERSÃO: 3.5 (Alta Estabilidade Wi-Fi, WiFi.setSleep(false), Inicialização DMX Pós-Boot)
 * ==============================================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
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
// 3. BUFFER DMX512 & PARÂMETROS DE CALIBRAÇÃO EM TEMPO REAL
// ------------------------------------------------------------------------------
uint8_t dmxCanais[16];
unsigned long ultimoEnvioDmx = 0;

// Modo de Operação: 0 = Modo Áudio Automático (UDP), 1 = Modo Manual Web (Sliders)
int modoOperacaoWeb = 0;

// Parâmetros ajustáveis via Web em tempo real para o Modo Automático
struct ParametrosCalibracao {
  int zoomMinimo = 135;       // Tamanho de repouso (0-255)
  int zoomMaximo = 255;       // Tamanho no pico da batida (0-255)
  int sensibilidadeVocal = 180;// Intensidade de onda na voz (0-255)
  int velocidadeRotacao = 200; // Velocidade de rotação (0-255)
} calib;

// Lista de Padrões Geométricos e Cores
const uint8_t padroesLaser[] = {12, 25, 40, 55, 70, 90, 110, 130};
const int totalPadroes = sizeof(padroesLaser) / sizeof(padroesLaser[0]);
int indicePadrao = 0;

const uint8_t coresLaser[] = {12, 22, 32, 42, 52, 62, 72};
const int totalCores = sizeof(coresLaser) / sizeof(coresLaser[0]);
int indiceCor = 0;

int contadorBatidas = 0;
float zoomAtual = 135.0f;

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
  digitalWrite(PIN_DMX_ENABLE, HIGH); // Habilita modo TX no MAX485
  pinMode(PIN_DMX_TX, OUTPUT);
  digitalWrite(PIN_DMX_TX, HIGH);
  memset(dmxCanais, 0, sizeof(dmxCanais));
  dmxInicializado = true;
}

void enviarFrameDMX() {
  if (!dmxInicializado) return;

  // 1. Break (Linha em LOW por 100us)
  Serial1.flush();
  pinMode(PIN_DMX_TX, OUTPUT);
  digitalWrite(PIN_DMX_TX, LOW);
  delayMicroseconds(100);

  // 2. MAB (Linha em HIGH por 12us)
  digitalWrite(PIN_DMX_TX, HIGH);
  delayMicroseconds(12);

  // 3. Inicia UART a 250 kbps, 8N2
  Serial1.begin(250000, SERIAL_8N2, -1, PIN_DMX_TX);

  // 4. Start Code DMX (0x00)
  Serial1.write((uint8_t)0x00);

  // 5. Envia os 16 canais DMX do Laser
  Serial1.write(dmxCanais, 16);
}

void atualizarLaserDMX_Audio(String modo, float nivel_graves, bool pico_grave, float nivel_vocal, float deltaTempo) {
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

  // Troca de cor e padrão no kick
  if (pico_grave) {
    indiceCor = (indiceCor + 1) % totalCores;
    contadorBatidas++;
    if (contadorBatidas >= 4) {
      indicePadrao = (indicePadrao + 1) % totalPadroes;
      contadorBatidas = 0;
    }
  }

  dmxCanais[2] = coresLaser[indiceCor];
  dmxCanais[3] = 0;
  dmxCanais[4] = padroesLaser[indicePadrao];

  // Zoom dinâmico parametrizado
  float amplitudeZoom = (float)(calib.zoomMaximo - calib.zoomMinimo);
  float zoomAlvo = (float)calib.zoomMinimo + (nivel_graves * (amplitudeZoom * 0.7f)) + (nivel_vocal * (amplitudeZoom * 0.3f));
  if (pico_grave) {
    zoomAlvo = (float)calib.zoomMaximo;
  }

  if (zoomAlvo > zoomAtual) {
    zoomAtual = zoomAlvo;
  } else {
    zoomAtual = max((float)calib.zoomMinimo, zoomAtual - (150.0f * deltaTempo));
  }

  dmxCanais[5] = (uint8_t)constrain(zoomAtual, (float)calib.zoomMinimo, 255.0f);

  // Ondulação Vocal parametrizada
  float vocal_curva = constrain(pow(nivel_vocal, 0.70f), 0.0f, 1.0f);
  dmxCanais[12] = (uint8_t)(vocal_curva * (float)calib.sensibilidadeVocal);

  dmxCanais[7] = (uint8_t)calib.velocidadeRotacao;
  dmxCanais[8] = (pico_grave) ? 160 : 64;
  dmxCanais[9] = 64;
  dmxCanais[10] = 64;
  dmxCanais[11] = 64;
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
// 7. PÁGINA WEB HTML / CSS / JS EMBUTIDA
// ------------------------------------------------------------------------------

const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Laser DMX Controller & Calibrator</title>
<style>
  :root { --bg: #0f172a; --card: #1e293b; --primary: #38bdf8; --accent: #f43f5e; --text: #f8fafc; }
  body { background: var(--bg); color: var(--text); font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 15px; }
  .container { max-width: 650px; margin: 0 auto; }
  h1 { text-align: center; color: var(--primary); font-size: 1.5rem; margin-bottom: 5px; }
  .subtitle { text-align: center; color: #94a3b8; font-size: 0.85rem; margin-bottom: 20px; }
  .card { background: var(--card); border-radius: 12px; padding: 16px; margin-bottom: 16px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); }
  .card-title { font-weight: bold; color: var(--primary); font-size: 1.1rem; margin-bottom: 12px; border-bottom: 1px solid #334155; padding-bottom: 6px; }
  .btn-group { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 12px; }
  button { background: #334155; color: white; border: none; padding: 10px 14px; border-radius: 8px; font-weight: bold; cursor: pointer; flex: 1; min-width: 120px; transition: all 0.2s; }
  button:hover { background: #475569; }
  button.active { background: var(--primary); color: #0f172a; }
  button.danger { background: var(--accent); }
  .slider-row { margin-bottom: 12px; }
  .slider-header { display: flex; justify-content: space-between; font-size: 0.9rem; margin-bottom: 4px; }
  .slider-val { font-weight: bold; color: var(--primary); }
  input[type=range] { width: 100%; height: 8px; border-radius: 4px; background: #334155; outline: none; -webkit-appearance: none; }
  input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 20px; height: 20px; border-radius: 50%; background: var(--primary); cursor: pointer; }
</style>
</head>
<body>
<div class="container">
  <h1>⚡ Laser DMX Calibrator</h1>
  <div class="subtitle">ESP32-C3 Super Mini | Controle em Tempo Real</div>

  <div class="card">
    <div class="card-title">🎮 Modo de Operação</div>
    <div class="btn-group">
      <button id="btnAuto" class="active" onclick="setModo(0)">🎵 Modo Áudio Automático</button>
      <button id="btnManual" onclick="setModo(1)">🎛️ Modo Manual (Sliders)</button>
    </div>
  </div>

  <div class="card" id="cardAuto">
    <div class="card-title">⚙️ Calibração de Parâmetros do Ritmo</div>
    <div class="slider-row">
      <div class="slider-header"><span>Tamanho Mínimo (Repouso)</span><span class="slider-val" id="vZoomMin">135</span></div>
      <input type="range" min="50" max="200" value="135" oninput="updateCalib('zmin', this.value, 'vZoomMin')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>Tamanho Máximo (Pico do Bumbo)</span><span class="slider-val" id="vZoomMax">255</span></div>
      <input type="range" min="150" max="255" value="255" oninput="updateCalib('zmax', this.value, 'vZoomMax')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>Sensibilidade Vocal (Ondulação X)</span><span class="slider-val" id="vVocal">180</span></div>
      <input type="range" min="0" max="255" value="180" oninput="updateCalib('voc', this.value, 'vVocal')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>Velocidade de Rotação 3D</span><span class="slider-val" id="vRot">200</span></div>
      <input type="range" min="100" max="255" value="200" oninput="updateCalib('rot', this.value, 'vRot')">
    </div>
  </div>

  <div class="card">
    <div class="card-title">🎛️ Sliders Diretos DMX512 (Canais do Projetor)</div>
    
    <div class="btn-group">
      <button onclick="setAtalho(10)">⭕ Círculo</button>
      <button onclick="setAtalho(25)">⚡ Zigzag</button>
      <button onclick="setAtalho(70)">🌀 Túnel</button>
      <button onclick="setAtalho(90)">🌊 Onda</button>
      <button class="danger" onclick="setBlackout()">⬛ Blackout</button>
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>CH1: Modo (0=Off, 50=Manual)</span><span class="slider-val" id="vCH1">50</span></div>
      <input type="range" min="0" max="255" value="50" oninput="setDMX(1, this.value, 'vCH1')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH3: Cor (12=Vm, 22=Vd, 32=Az, 52=Ciano)</span><span class="slider-val" id="vCH3">12</span></div>
      <input type="range" min="0" max="255" value="12" oninput="setDMX(3, this.value, 'vCH3')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH5: Padrão Gráfico / Gobo</span><span class="slider-val" id="vCH5">25</span></div>
      <input type="range" min="0" max="255" value="25" oninput="setDMX(5, this.value, 'vCH5')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH6: Tamanho / Zoom Manual</span><span class="slider-val" id="vCH6">160</span></div>
      <input type="range" min="0" max="255" value="160" oninput="setDMX(6, this.value, 'vCH6')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH8: Rotação Centro (128-255)</span><span class="slider-val" id="vCH8">180</span></div>
      <input type="range" min="0" max="255" value="180" oninput="setDMX(8, this.value, 'vCH8')">
    </div>
    <div class="slider-row">
      <div class="slider-header"><span>CH13: Onda no Eixo X</span><span class="slider-val" id="vCH13">0</span></div>
      <input type="range" min="0" max="255" value="0" oninput="setDMX(13, this.value, 'vCH13')">
    </div>
  </div>
</div>

<script>
function setModo(m) {
  fetch('/modo?val=' + m);
  document.getElementById('btnAuto').className = (m === 0) ? 'active' : '';
  document.getElementById('btnManual').className = (m === 1) ? 'active' : '';
}

function updateCalib(param, val, labelId) {
  document.getElementById(labelId).innerText = val;
  fetch('/calib?' + param + '=' + val);
}

function setDMX(ch, val, labelId) {
  document.getElementById(labelId).innerText = val;
  fetch('/dmx?ch=' + ch + '&val=' + val);
}

function setAtalho(padrao) {
  setModo(1);
  document.getElementById('vCH5').innerText = padrao;
  fetch('/dmx?ch=5&val=' + padrao);
}

function setBlackout() {
  setModo(1);
  document.getElementById('vCH1').innerText = 0;
  fetch('/dmx?ch=1&val=0');
}
</script>
</body>
</html>
)rawliteral";

// ------------------------------------------------------------------------------
// 8. ROTAS DO WEB SERVER (Com Headers de Liberação Rápida de Conexão)
// ------------------------------------------------------------------------------

void handleRoot() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", HTML_INDEX);
}

void handleModo() {
  if (server.hasArg("val")) {
    modoOperacaoWeb = server.arg("val").toInt();
    if (modoOperacaoWeb == 1) {
      dmxCanais[0] = 50; // Habilita modo manual no laser
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
      modoOperacaoWeb = 1; // Ativa modo manual web
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

// ------------------------------------------------------------------------------
// 9. SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==================================================");
  Serial.println(" Audio to Light - ESP32-C3 (Alta Estabilidade Wi-Fi)");
  Serial.println("==================================================");

  // Configuração dos atuadores nos pinos dedicados
  pinMode(PIN_STROBE_BRANCO, OUTPUT);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  // AUTO-TESTE RÁPIDO
  setStrobe(255); delay(200); setStrobe(0);
  setGloboRGB(100, 0, 0); delay(100);
  setGloboRGB(0, 100, 0); delay(100);
  setGloboRGB(0, 0, 100); delay(100);
  setGloboRGB(0, 0, 0);

  // 1. CONEXÃO WI-FI OTIMIZADA (SEM DORMIR O RÁDIO)
  Serial.printf("\n[Wi-Fi] Conectando a: %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_ONBOARD, !digitalRead(PIN_LED_ONBOARD));
    delay(150);
    Serial.print(".");
  }

  // DESATIVA O POWER-SAVE DO WI-FI (Mantém o Web Server sempre responsivo sem quedas!)
  WiFi.setSleep(false);

  digitalWrite(PIN_LED_ONBOARD, LOW); // LED Aceso = Conectado
  Serial.printf("\n[Wi-Fi] Conectado! IP do ESP32: %s\n", WiFi.localIP().toString().c_str());

  // 2. INICIALIZAÇÃO DO DMX512 (APENAS APÓS O WI-FI ESTAR CONECTADO)
  // Isso evita qualquer conflito na UART durante a inicialização do rádio!
  inicializarDMX();
  Serial.println("[DMX512] Transmissao RS-485 inicializada com sucesso!");

  // 3. INICIA ROTAS WEB
  server.on("/", handleRoot);
  server.on("/modo", handleModo);
  server.on("/calib", handleCalib);
  server.on("/dmx", handleDMX);
  server.begin();
  Serial.printf("[WEB] Servidor ativo em: http://%s\n", WiFi.localIP().toString().c_str());

  // 4. INICIA SOCKET UDP
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

  // Processa requisições Web do navegador com prioridade
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

          // 1. ATUALIZA LASER DMX (Se estiver no Modo Automático Áudio)
          if (modoOperacaoWeb == 0) {
            atualizarLaserDMX_Audio(modo_atual, nivel_grave, pico_grave, nivel_vocal, deltaTempo);
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

  delay(1); // Cede tempo para a pilha TCP/IP do LwIP manter o Web Server ultra-estável
}
