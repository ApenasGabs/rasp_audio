/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ RECEPTOR ESP32-C3 SUPER MINI (COM DMX512)
 * ATUADORES:
 *  - Strobe Branco (GPIO 0 - ULN2003)
 *  - Globo RGB (GPIO 4, 5, 6 - ULN2003)
 *  - Servo Motor SG90 (GPIO 7)
 *  - Projetor Laser Profissional DMX512 (GPIO 21 via Módulo RS-485 / MAX485)
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
uint8_t padroesGraficosLaser[] = {15, 28, 42, 60, 85, 110, 135, 160, 190, 220};
int indicePadraoLaser = 0;

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
int pulsoServoUs = 1500; // 90 graus (centro)
float ladoSaltoServo = 30.0f;

// ------------------------------------------------------------------------------
// 5. DRIVER DE TRANSMISSÃO DMX512 (UART a 250 kbps com Break/MAB)
// ------------------------------------------------------------------------------

void enviarFrameDMX() {
  // 1. Break (Linha em LOW por 100us)
  Serial1.flush();
  pinMode(PIN_DMX_TX, OUTPUT);
  digitalWrite(PIN_DMX_TX, LOW);
  delayMicroseconds(100);

  // 2. MAB - Mark After Break (Linha em HIGH por 12us)
  digitalWrite(PIN_DMX_TX, HIGH);
  delayMicroseconds(12);

  // 3. Inicia UART a 250.000 baud, 8N2
  Serial1.begin(250000, SERIAL_8N2, -1, PIN_DMX_TX);

  // 4. Start Code DMX (0x00 para iluminação padrão)
  Serial1.write((uint8_t)0x00);

  // 5. Envia os 16 canais DMX do Laser
  Serial1.write(dmxCanais, 16);
}

void atualizarLaserDMX(String modo, float nivel_graves, bool pico_grave, float nivel_medios, float tempo_s) {
  if (modo == "standby") {
    // Modo Blackout Total (Seguro)
    dmxCanais[0] = 0;   // CH1: Modo fechado / desligado
    dmxCanais[1] = 0;   // CH2: Velocidade 0
    dmxCanais[2] = 0;   // CH3: Cor inicial
    dmxCanais[4] = 0;   // CH5: Padrão 0
    return;
  }

  // CH1: Modo Manual do Console (Controle DMX total)
  dmxCanais[0] = 50;  // 40-79 = Manual

  // CH2: Velocidade do sistema
  dmxCanais[1] = 128;

  // CH15: Velocidade de escaneamento máxima para feixes nítidos
  dmxCanais[14] = 255;

  if (modo == "suave") {
    dmxCanais[2] = 45;   // CH3: Cor Ciano/Azul fixa suave
    dmxCanais[3] = 0;    // CH4: Sem fluxo rápido
    dmxCanais[4] = 15;   // CH5: Padrão suave (círculo / linha calma)
    dmxCanais[5] = 100;  // CH6: Zoom médio relaxante
    dmxCanais[6] = 0;    // CH7: Sem zoom dinâmico agressivo
    dmxCanais[7] = 135;  // CH8: Rotação lenta
    dmxCanais[8] = 64;   // CH9: Posição horizontal centro
    dmxCanais[9] = 64;   // CH10: Posição vertical centro
    dmxCanais[10] = 64;  // CH11: Mov X centro
    dmxCanais[11] = 64;  // CH12: Mov Y centro
    dmxCanais[12] = 0;   // CH13: Sem ondas
    dmxCanais[13] = 0;   // CH14: Sem desenho gradual
    dmxCanais[15] = 0;   // CH16: Exibição padrão
  }
  else if (modo == "alta_energia") {
    dmxCanais[2] = 92;   // CH3: Cores coloridas / multicoloridas eufóricas
    dmxCanais[3] = 100;  // CH4: Fluxo de cor veloz

    // Troca o desenho do laser a cada batida forte de bumbo/grave!
    if (pico_grave && nivel_graves >= 0.70f) {
      indicePadraoLaser = (indicePadraoLaser + 1) % (sizeof(padroesGraficosLaser) / sizeof(padroesGraficosLaser[0]));
    }
    dmxCanais[4] = padroesGraficosLaser[indicePadraoLaser]; // CH5: Padrão geométrico atual

    // Zoom expansivo que explode nas batidas fortes
    dmxCanais[5] = (pico_grave) ? 240 : (int)(150.0f + (nivel_graves * 80.0f)); // CH6: Tamanho
    dmxCanais[6] = 35;   // CH7: Dimensionamento dinâmico
    dmxCanais[7] = 240;  // CH8: Rotação veloz
    dmxCanais[8] = (pico_grave) ? 180 : 135;  // CH9: Inversão horizontal dinâmica
    dmxCanais[9] = 64;   // CH10: Vertical
    dmxCanais[10] = (uint8_t)(64 + sin(tempo_s * 2.0f) * 45); // CH11: Movimento X
    dmxCanais[11] = 64;  // CH12: Mov Y
    dmxCanais[12] = (uint8_t)(nivel_medios * 180.0f); // CH13: Onda X
    dmxCanais[13] = 160; // CH14: Traçado gradual (drawing effect)
    dmxCanais[15] = (pico_grave) ? 80 : 0; // CH16: Destaques luminosos no drop
  }
  else { // media_energia / fallback
    dmxCanais[2] = 85;   // CH3: Troca geral de cores
    dmxCanais[3] = 50;   // CH4: Velocidade moderada de fluxo
    if (pico_grave) {
      indicePadraoLaser = (indicePadraoLaser + 1) % (sizeof(padroesGraficosLaser) / sizeof(padroesGraficosLaser[0]));
    }
    dmxCanais[4] = padroesGraficosLaser[indicePadraoLaser];
    dmxCanais[5] = (int)(120.0f + (nivel_graves * 60.0f));
    dmxCanais[6] = 20;
    dmxCanais[7] = 165;  // CH8: Rotação moderada
    dmxCanais[8] = 64;
    dmxCanais[9] = 64;
    dmxCanais[10] = 64;
    dmxCanais[11] = 64;
    dmxCanais[12] = (uint8_t)(nivel_medios * 100.0f);
    dmxCanais[13] = 60;
    dmxCanais[15] = 0;
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

void atualizarPaletaGlobo(String modo, float nivel_medios, float tempo_s) {
  float brilho_base = max(30.0f, nivel_medios * 100.0f);

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
      onda_r * 100.0f * max(0.6f, nivel_medios),
      onda_g * 80.0f * max(0.4f, nivel_medios),
      onda_b * 100.0f * max(0.6f, nivel_medios)
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
  Serial.println(" Audio to Light - ESP32-C3 (Com Projetor DMX512)");
  Serial.println("==================================================");

  pinMode(PIN_STROBE_BRANCO, OUTPUT);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  // Habilita transmissão no Módulo RS-485 (DE e RE em nível HIGH)
  pinMode(PIN_DMX_ENABLE, OUTPUT);
  digitalWrite(PIN_DMX_ENABLE, HIGH);

  // Inicializa UART do DMX512
  pinMode(PIN_DMX_TX, OUTPUT);
  digitalWrite(PIN_DMX_TX, HIGH);
  memset(dmxCanais, 0, sizeof(dmxCanais));
  enviarFrameDMX();

  // AUTO-TESTE INICIAL
  Serial.println("[AUTO-TESTE] 1. Testando Strobe (GPIO 0)...");
  setStrobe(255); delay(300); setStrobe(0);

  Serial.println("[AUTO-TESTE] 2. Testando Globo RGB...");
  setGloboRGB(100, 0, 0); delay(200);
  setGloboRGB(0, 100, 0); delay(200);
  setGloboRGB(0, 0, 100); delay(200);
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
  Serial.printf("[DMX] Transmissao ativa no GPIO %d (RS-485)\n", PIN_DMX_TX);
}

// ------------------------------------------------------------------------------
// 8. LOOP PRINCIPAL
// ------------------------------------------------------------------------------
void loop() {
  unsigned long agora = millis();
  unsigned long agoraUs = micros();
  float tempo_s = agora / 1000.0f;

  atualizarServo(agoraUs);

  // Transmite frame DMX a ~30Hz (a cada 33ms) para o projetor laser
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
          JsonObject agudos = faixas["agudos"];

          bool ativo_grave = graves["ativo"] | false;
          bool pico_grave = graves["pico"] | false;
          float nivel_grave = graves["nivel"] | 0.0f;
          float nivel_medios = medios["nivel"] | 0.3f;
          bool ativo_agudo = agudos["ativo"] | false;

          // 1. ATUALIZA PROJETOR LASER VIA DMX512
          atualizarLaserDMX(modo_atual, nivel_grave, pico_grave, nivel_medios, tempo_s);

          // 2. GLOBO RGB
          atualizarPaletaGlobo(modo_atual, nivel_medios, tempo_s);

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
