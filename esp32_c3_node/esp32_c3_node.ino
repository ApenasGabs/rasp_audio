/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ RECEPTOR ESP32-C3 SUPER MINI (COM DMX512)
 * VERSÃO: 3.3 (Laser Rítmico Dinâmico: Pulsação no Beat, Troca de Cor e Padrão)
 * ==============================================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <math.h>

// ------------------------------------------------------------------------------
// 1. CONFIGURAÇÃO WI-FI & UDP
// ------------------------------------------------------------------------------
const char* WIFI_SSID = "SEU_WIFI_NOME";        // << Coloque o nome do seu Wi-Fi
const char* WIFI_PASS = "SUA_WIFI_SENHA";       // << Coloque a senha do seu Wi-Fi
const unsigned int UDP_PORT = 5005;

WiFiUDP udp;
char packetBuffer[2048];

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
// 3. BUFFER E ESTRUTURA DMX512 (16 Canais do Projetor Laser)
// ------------------------------------------------------------------------------
uint8_t dmxCanais[16];
unsigned long ultimoEnvioDmx = 0;

// Lista de Padrões Geométricos Nítidos (Círculo, Zigzag, Quadrado, Triângulo, Túnel, Onda)
const uint8_t padroesLaser[] = {10, 25, 40, 55, 70, 90, 110, 130};
const int totalPadroes = sizeof(padroesLaser) / sizeof(padroesLaser[0]);
int indicePadrao = 0;

// Cores Sólidas de Alto Contraste (Vermelho, Verde, Azul, Amarelo, Ciano, Magenta, Branco)
const uint8_t coresLaser[] = {12, 22, 32, 42, 52, 62, 72};
const int totalCores = sizeof(coresLaser) / sizeof(coresLaser[0]);
int indiceCor = 0;

int contadorBatidas = 0;
float zoomAtual = 60.0f; // Envelope dinâmico de tamanho

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

void enviarFrameDMX() {
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

void atualizarLaserDMX(String modo, float nivel_graves, bool pico_grave, float nivel_vocal, float deltaTempo) {
  if (modo == "standby") {
    dmxCanais[0] = 0;   // CH1: Modo fechado (Blackout)
    dmxCanais[1] = 0;
    dmxCanais[2] = 0;
    dmxCanais[4] = 0;
    return;
  }

  // CH1: Modo Manual do Console (Controle DMX total)
  dmxCanais[0] = 50;

  // CH2: Velocidade padrão
  dmxCanais[1] = 128;

  // CH14 e CH16: Sem corte ou desenho gradual (feixe 100% visível)
  dmxCanais[13] = 0;
  dmxCanais[14] = 255;
  dmxCanais[15] = 0;
  dmxCanais[6] = 0;

  // -------------------------------------------------------------------------
  // A. TROCA DE COR E PADRÃO NO RITMO DO KICK / GRAVE
  // -------------------------------------------------------------------------
  if (pico_grave) {
    // A cada batida forte, troca a cor instantaneamente (Color Beat)
    indiceCor = (indiceCor + 1) % totalCores;
    
    // A cada 4 batidas, troca o desenho (Círculo -> Zigzag -> Túnel -> etc)
    contadorBatidas++;
    if (contadorBatidas >= 4) {
      indicePadrao = (indicePadrao + 1) % totalPadroes;
      contadorBatidas = 0;
    }
  }

  // CH3: Cor atual sólida de alto impacto
  dmxCanais[2] = coresLaser[indiceCor];
  dmxCanais[3] = 0; // Sem fluxo confuso; cor pura no ritmo

  // CH5: Padrão geométrico atual
  dmxCanais[4] = padroesLaser[indicePadrao];

  // -------------------------------------------------------------------------
  // B. PULSAÇÃO DE TAMANHO / ZOOM (BEAT PUMP & BOUNCE)
  // -------------------------------------------------------------------------
  // Tamanho alvo: compacto em repouso (~60), explode nas batidas e na voz forte (~220)
  float zoomAlvo = 60.0f + (nivel_graves * 140.0f) + (nivel_vocal * 50.0f);
  if (pico_grave) {
    zoomAlvo = 230.0f; // Explosão imediata no bumbo
  }

  // Envelope Follower: Sobe instantaneamente, decai suavemente no ritmo
  if (zoomAlvo > zoomAtual) {
    zoomAtual = zoomAlvo; // Ataque rápido
  } else {
    zoomAtual = max(60.0f, zoomAtual - (180.0f * deltaTempo)); // Decaimento suave
  }

  dmxCanais[5] = (uint8_t)constrain(zoomAtual, 50.0f, 245.0f); // CH6: Tamanho do desenho

  // -------------------------------------------------------------------------
  // C. ONDULAÇÃO NO VOCAL & ROTAÇÃO RÍTMICA
  // -------------------------------------------------------------------------
  // CH13: Ondulação ativada pelo canto/voz
  float vocal_curva = constrain(pow(nivel_vocal, 0.70f), 0.0f, 1.0f);
  dmxCanais[12] = (uint8_t)(vocal_curva * 200.0f);

  if (modo == "alta_energia") {
    dmxCanais[7] = 210; // CH8: Rotação rápida
    dmxCanais[8] = (pico_grave) ? 160 : 64; // CH9: Flip horizontal nos kicks
    dmxCanais[9] = 64;
    dmxCanais[10] = 64;
    dmxCanais[11] = 64;
  }
  else if (modo == "suave") {
    dmxCanais[7] = 135; // Rotação lenta
    dmxCanais[8] = 64;
    dmxCanais[9] = 64;
    dmxCanais[10] = 64;
    dmxCanais[11] = 64;
  }
  else { // media_energia / fallback
    dmxCanais[7] = 160;
    dmxCanais[8] = 64;
    dmxCanais[9] = 64;
    dmxCanais[10] = 64;
    dmxCanais[11] = 64;
  }
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
  dmxCanais[0] = 0; // Blackout Laser
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
  else { // media_energia / fallback
    float onda = (sin(tempo_s * 1.5f) + 1.0f) / 2.0f;
    float r = onda * 80.0f * (brilho_base / 100.0f);
    float g = (1.0f - onda) * 50.0f * (brilho_base / 100.0f);
    float b = 90.0f * (brilho_base / 100.0f);
    setGloboRGB(r, g, b);
  }
}

// ------------------------------------------------------------------------------
// 7. SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n==================================================");
  Serial.println(" Audio to Light - ESP32-C3 (Laser Ritmico 120BPM)");
  Serial.println("==================================================");

  pinMode(PIN_STROBE_BRANCO, OUTPUT);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  // Habilita transmissão RS-485
  pinMode(PIN_DMX_ENABLE, OUTPUT);
  digitalWrite(PIN_DMX_ENABLE, HIGH);

  // Inicializa UART DMX512
  pinMode(PIN_DMX_TX, OUTPUT);
  digitalWrite(PIN_DMX_TX, HIGH);
  memset(dmxCanais, 0, sizeof(dmxCanais));
  enviarFrameDMX();

  // AUTO-TESTE INICIAL
  setStrobe(255); delay(250); setStrobe(0);
  setGloboRGB(100, 0, 0); delay(150);
  setGloboRGB(0, 100, 0); delay(150);
  setGloboRGB(0, 0, 100); delay(150);
  setGloboRGB(0, 0, 0);

  // Conexão Wi-Fi
  Serial.printf("\n[Wi-Fi] Conectando a: %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_ONBOARD, !digitalRead(PIN_LED_ONBOARD));
    delay(200);
    Serial.print(".");
  }

  digitalWrite(PIN_LED_ONBOARD, LOW);
  Serial.printf("\n[Wi-Fi] Conectado! IP do ESP32: %s\n", WiFi.localIP().toString().c_str());

  udp.begin(UDP_PORT);
  Serial.printf("[UDP] Escutando porta %d...\n", UDP_PORT);
}

// ------------------------------------------------------------------------------
// 8. LOOP PRINCIPAL
// ------------------------------------------------------------------------------
unsigned long ultimoCicloMs = 0;

void loop() {
  unsigned long agora = millis();
  unsigned long agoraUs = micros();
  float tempo_s = agora / 1000.0f;
  float deltaTempo = max(0.001f, (agora - ultimoCicloMs) / 1000.0f);
  ultimoCicloMs = agora;

  atualizarServo(agoraUs);

  // Envio contínuo DMX512 a ~30Hz (a cada 33ms)
  if (agora - ultimoEnvioDmx >= 33) {
    ultimoEnvioDmx = agora;
    enviarFrameDMX();
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(200);
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

          // 1. ATUALIZA LASER DMX COM PULSAÇÃO RÍTMICA E TROCA DE COR NO BEAT
          atualizarLaserDMX(modo_atual, nivel_grave, pico_grave, nivel_vocal, deltaTempo);

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
          else { // media_energia / fallback
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

  if (agora - ultimoPacoteAudio > 4000 && ultimoPacoteAudio > 0) {
    desligarTudo();
  }
}
