/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ DE ILUMINAÇÃO & MOVIMENTO (ESP32-C3 SUPER MINI)
 * ATUADORES:
 *   - Strobe Branco 12V (GPIO 0 via ULN2003) com contagem e delay de piscadas
 *   - Globo RGB (GPIO 4, 5, 6 via ULN2003) com paletas e sensibilidade
 *   - Servo Motor SG90 (GPIO 7) com varredura angular e Drop Jumps
 *   - Servidor Web HTTP (Porta 80) para calibração de sensibilidades e testes
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
#define PIN_STROBE_BRANCO    0   // ULN2003 - Strobe nos Graves
#define PIN_GLOBO_R          4   // ULN2003 - Globo Vermelho
#define PIN_GLOBO_G          5   // ULN2003 - Globo Verde
#define PIN_GLOBO_B          6   // ULN2003 - Globo Azul
#define PIN_SERVO_GLOBO      7   // Sinal do Servo Motor SG90 (50Hz)
#define PIN_LED_ONBOARD      8   // Status Wi-Fi

// ------------------------------------------------------------------------------
// 3. PARÂMETROS DE CALIBRAÇÃO & CONTROLE DOS ATUADORES
// ------------------------------------------------------------------------------
int modoOperacao = 0; // 0 = Áudio Automático, 1 = Manual / Teste

struct ConfigLuzes {
  // STROBE
  float threshGrave = 0.40f;    // Limiar para disparar o strobe (0.10 a 0.90)
  int duracaoTotalMs = 400;     // Duração total do disparo do strobe (100 a 1000ms)
  int quantidadeFlashes = 4;    // Número de piscadas durante o disparo (1 a 10 flashes)
  int intensidadeStrobe = 255;  // Brilho do Strobe (0 a 255)

  // GLOBO RGB
  int brilhoGloboPct = 100;     // Brilho máximo do globo (0 a 100%)
  int sensibilidadeVocal = 150; // Intensidade da modulação por voz/médios

  // SERVO SG90
  int anguloMin = 20;           // Ângulo mínimo de varredura (0 a 80 graus)
  int anguloMax = 160;          // Ângulo máximo de varredura (100 a 180 graus)
  float freqVarredura = 0.4f;   // Velocidade da oscilação (0.1 a 2.0 Hz)
  bool jumpNoKick = true;       // Se dá salto brusco no bumbo
} cfg;

// Estados e Temporização do Strobe Estroboscópico (Multi-Flash)
bool strobeEmDisparo = false;
unsigned long inicioDisparoStrobe = 0;
int flashAtual = 0;

// Variáveis do Servo SG90
unsigned long ultimoPulsoServo = 0;
int pulsoServoUs = 1500; // 90 graus
float anguloAtualServo = 90.0f;
float ladoSaltoServo = 30.0f;

// Controle Rítmico
unsigned long ultimoKickValido = 0;
const unsigned long COOLDOWN_KICK_MS = 260; // Filtro anti-glitch
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

void setStrobe(int valor_0_a_255) {
  analogWrite(PIN_STROBE_BRANCO, constrain(valor_0_a_255, 0, 255));
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

// Dispara a sequência estroboscópica de piscadas
void dispararStrobe() {
  strobeEmDisparo = true;
  inicioDisparoStrobe = millis();
}

// Motor de piscadas do Strobe (Calcula ciclos ON / OFF com base na quantidade escolhida)
void processarStrobe(unsigned long agora) {
  if (!strobeEmDisparo) {
    setStrobe(0);
    return;
  }

  unsigned long decorrido = agora - inicioDisparoStrobe;

  if (decorrido >= cfg.duracaoTotalMs) {
    strobeEmDisparo = false;
    setStrobe(0);
    return;
  }

  // Divide o tempo total pelo número de piscadas
  int periodoFlash = cfg.duracaoTotalMs / max(1, cfg.quantidadeFlashes);
  int tempoOn = periodoFlash / 2; // Metade aceso, metade apagado

  int fase = decorrido % periodoFlash;
  if (fase < tempoOn) {
    setStrobe(cfg.intensidadeStrobe);
  } else {
    setStrobe(0);
  }
}

void desligarTudo() {
  strobeEmDisparo = false;
  setStrobe(0);
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
  else { // media_energia / fallback
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
<title>Painel de Controle: Iluminação & Servo</title>
<style>
  :root { --bg: #0b1329; --card: #17233f; --primary: #38bdf8; --accent: #f43f5e; --success: #10b981; --warning: #f59e0b; --text: #f8fafc; }
  body { background: var(--bg); color: var(--text); font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 12px; }
  .container { max-width: 650px; margin: 0 auto; }
  h1 { text-align: center; color: var(--primary); font-size: 1.4rem; margin-bottom: 2px; }
  .subtitle { text-align: center; color: #94a3b8; font-size: 0.8rem; margin-bottom: 14px; }
  .card { background: var(--card); border-radius: 12px; padding: 14px; margin-bottom: 14px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); border: 1px solid #223254; }
  .card-title { font-weight: bold; color: var(--primary); font-size: 1.05rem; margin-bottom: 10px; border-bottom: 1px solid #2a3d66; padding-bottom: 6px; }
  .btn-group { display: flex; gap: 6px; flex-wrap: wrap; margin-bottom: 8px; }
  button { background: #24355a; color: white; border: none; padding: 9px 12px; border-radius: 8px; font-weight: bold; cursor: pointer; flex: 1; min-width: 100px; transition: all 0.2s; font-size: 0.85rem; }
  button:hover { background: #324775; }
  button.active { background: var(--primary); color: #0b1329; }
  button.selected { background: #0284c7; color: white; border: 1px solid #38bdf8; }
  button.success { background: var(--success); color: white; }
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
  <h1>⚡ Controle de Iluminação & Servo</h1>
  <div class="subtitle">Strobe (Piscadas/Delay) | Globo RGB | Servo SG90</div>

  <div class="card">
    <div class="card-title">🎮 Modo de Operação</div>
    <div class="btn-group">
      <button id="btnAuto" class="active" onclick="setModo(0)">🎵 Modo Áudio Automático</button>
      <button id="btnManual" onclick="setModo(1)">🎛️ Modo Manual / Bancada</button>
    </div>
  </div>

  <!-- SEÇÃO 1: STROBE BRANCO (PISCADAS E DELAY) -->
  <div class="card" style="border-left: 4px solid #f8fafc;">
    <div class="card-title">⚪ 1. Strobe Branco (Graves / Kicks)</div>
    
    <div class="slider-row">
      <div class="slider-header"><span>Duração Total do Disparo (Delay)</span><span class="slider-val" id="vDelay">400 ms</span></div>
      <input type="range" min="100" max="1000" step="50" value="400" oninput="updateCfg('del', this.value, 'vDelay', ' ms')">
    </div>

    <div class="slider-row">
      <div class="slider-header"><span>Quantidade de Piscadas por Batida</span><span class="slider-val" id="vFlashes">4 Piscadas</span></div>
      <input type="range" min="1" max="10" step="1" value="4" oninput="updateCfg('fls', this.value, 'vFlashes', ' Piscadas')">
    </div>

    <div class="grid-2">
      <div class="slider-row">
        <div class="slider-header"><span>Sensibilidade ao Bumbo</span><span class="slider-val" id="vThresh">0.40</span></div>
        <input type="range" min="15" max="85" value="40" oninput="updateCfgFloat('thr', this.value, 'vThresh')">
      </div>
      <div class="slider-row">
        <div class="slider-header"><span>Intensidade / Brilho</span><span class="slider-val" id="vIntStrobe">255</span></div>
        <input type="range" min="50" max="255" value="255" oninput="updateCfg('istr', this.value, 'vIntStrobe', '')">
      </div>
    </div>

    <div class="btn-group" style="margin-top: 6px;">
      <button class="selected" onclick="testarStrobe()">⚡ Testar Disparo do Strobe Agora</button>
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
    <div class="card-title">💾 Persistência & Salvar</div>
    <div class="btn-group">
      <button class="success" onclick="salvarFlash()">💾 Salvar Todas as Configurações na Flash</button>
    </div>
    <div id="toastMsg" class="toast"></div>
  </div>
</div>

<script>
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
  fetch('/salvar').then(() => showToast('💾 Configurações salvas com sucesso na Memória Flash!'));
}
</script>
</body>
</html>
)rawliteral";

// ------------------------------------------------------------------------------
// 6. ROTAS DO WEB SERVER
// ------------------------------------------------------------------------------

void handleRoot() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", HTML_INDEX);
}

void handleModo() {
  if (server.hasArg("val")) {
    modoOperacao = server.arg("val").toInt();
    if (modoOperacao == 0) {
      setStrobe(0);
      setGloboRGB(0, 0, 0);
    }
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleCfg() {
  if (server.hasArg("del"))  cfg.duracaoTotalMs = server.arg("del").toInt();
  if (server.hasArg("fls"))  cfg.quantidadeFlashes = server.arg("fls").toInt();
  if (server.hasArg("istr")) cfg.intensidadeStrobe = server.arg("istr").toInt();
  if (server.hasArg("thr"))  cfg.threshGrave = server.arg("thr").toFloat();
  if (server.hasArg("bglo")) cfg.brilhoGloboPct = server.arg("bglo").toInt();
  if (server.hasArg("amin")) cfg.anguloMin = server.arg("amin").toInt();
  if (server.hasArg("amax")) cfg.anguloMax = server.arg("amax").toInt();
  if (server.hasArg("fser")) cfg.freqVarredura = server.arg("fser").toFloat();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
}

void handleTestStrobe() {
  dispararStrobe();
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
  prefs.putInt("del", cfg.duracaoTotalMs);
  prefs.putInt("fls", cfg.quantidadeFlashes);
  prefs.putInt("istr", cfg.intensidadeStrobe);
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
  Serial.println(" ESP32-C3: NÓ DE ILUMINAÇÃO & SERVO (Com Painel Web)");
  Serial.println("==================================================");

  // Carrega configurações da Flash
  prefs.begin("lights_cfg", true);
  cfg.duracaoTotalMs = prefs.getInt("del", 400);
  cfg.quantidadeFlashes = prefs.getInt("fls", 4);
  cfg.intensidadeStrobe = prefs.getInt("istr", 255);
  cfg.threshGrave = prefs.getFloat("thr", 0.40f);
  cfg.brilhoGloboPct = prefs.getInt("bglo", 100);
  cfg.anguloMin = prefs.getInt("amin", 20);
  cfg.anguloMax = prefs.getInt("amax", 160);
  cfg.freqVarredura = prefs.getFloat("fser", 0.4f);
  prefs.end();

  pinMode(PIN_STROBE_BRANCO, OUTPUT);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  // Auto-teste
  setStrobe(255); delay(200); setStrobe(0);
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
  Serial.printf("\n[Wi-Fi] Conectado! IP do ESP32: %s\n", WiFi.localIP().toString().c_str());

  // Rotas Web
  server.on("/", handleRoot);
  server.on("/modo", handleModo);
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

          // A. CONTROLE DE DISPARO DO STROBE (Com Contagem e Delay)
          if (modoOperacao == 0) {
            bool kickValido = (pico_grave || nivel_grave >= cfg.threshGrave) && (agora - ultimoKickValido >= COOLDOWN_KICK_MS);
            if (kickValido) {
              ultimoKickValido = agora;
              dispararStrobe();
            }

            // B. GLOBO RGB
            atualizarPaletaGlobo(modo_atual, nivel_vocal, tempo_s);

            // C. SERVO SG90
            if (modo_atual == "alta_energia") {
              if (kickValido && cfg.jumpNoKick) {
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
            else { // media_energia / fallback
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
