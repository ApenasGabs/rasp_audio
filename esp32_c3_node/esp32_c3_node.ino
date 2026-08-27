/*
 * ==============================================================================
 * PROJETO: Audio to Light (Nó Receptor Sem Fio ESP32-C3 Super Mini)
 * DESCRIÇÃO: Escuta pacotes UDP :5005 transmitidos pela Raspberry Pi (Áudio + Spotify)
 *            e orquestra o Strobe Branco, Globo RGB e Servo Motor SG90.
 * ==============================================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <math.h>

// ------------------------------------------------------------------------------
// 1. CONFIGURAÇÕES DE REDE WI-FI & UDP
// ------------------------------------------------------------------------------
const char* WIFI_SSID = "SEU_WIFI_NOME";        // << Substitua pelo nome do seu Wi-Fi
const char* WIFI_PASS = "SUA_WIFI_SENHA";       // << Substitua pela senha do seu Wi-Fi
const unsigned int UDP_PORT = 5005;

WiFiUDP udp;
char packetBuffer[2048];

// ------------------------------------------------------------------------------
// 2. MAPEAMENTO DE PINOS GPIO (ESP32-C3 Super Mini)
// ------------------------------------------------------------------------------
#define PIN_STROBE_BRANCO    0   // ULN2003 - Graves / Kicks (PWM 15Hz)
#define PIN_GLOBO_R          4   // ULN2003 - Globo LED Vermelho (PWM)
#define PIN_GLOBO_G          5   // ULN2003 - Globo LED Verde (PWM)
#define PIN_GLOBO_B          6   // ULN2003 - Globo LED Azul (PWM)
#define PIN_SERVO_GLOBO      7   // Sinal de Controle do Servo SG90 (PWM 50Hz)

#define PIN_LED_ONBOARD      8   // LED de status onboard do ESP32-C3

// ------------------------------------------------------------------------------
// 3. CANAIS E CONFIGURAÇÃO PWM (LEDC ESP32)
// ------------------------------------------------------------------------------
#define PWM_FREQ_STROBE     15    // 15Hz para efeito Strobe autêntico
#define PWM_FREQ_GLOBO     100    // 100Hz para blend suave de cores
#define PWM_FREQ_SERVO      50    // 50Hz padrão de servo motores (20ms)

#define CH_STROBE     0
#define CH_GLOBO_R    1
#define CH_GLOBO_G    2
#define CH_GLOBO_B    3
#define CH_SERVO      4

// ------------------------------------------------------------------------------
// 4. ESTADO CONTEXTUAL E VARIÁVEIS DE CONTROLE
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
float ladoSaltoServo = 30.0f;

// ------------------------------------------------------------------------------
// 5. FUNÇÕES AUXILIARES DE ATUADORES & SERVO
// ------------------------------------------------------------------------------

void setPwmDuty(int channel, int duty_0_to_255) {
  duty_0_to_255 = constrain(duty_0_to_255, 0, 255);
  ledcWrite(channel, duty_0_to_255);
}

void setGloboRGB(float r_pct, float g_pct, float b_pct) {
  int valR = (int)(constrain(r_pct, 0.0, 100.0) * 2.55);
  int valG = (int)(constrain(g_pct, 0.0, 100.0) * 2.55);
  int valB = (int)(constrain(b_pct, 0.0, 100.0) * 2.55);
  setPwmDuty(CH_GLOBO_R, valR);
  setPwmDuty(CH_GLOBO_G, valG);
  setPwmDuty(CH_GLOBO_B, valB);
}

// Converte ângulo de 0° a 180° para Duty Cycle de 14 bits a 50Hz (500µs a 2400µs)
void setServoAngulo(float graus) {
  graus = constrain(graus, 0.0f, 180.0f);
  // 50Hz = período de 20000us. Em 14 bits (16383):
  // 500us (0°)   -> 409
  // 2400us (180°) -> 1966
  int duty = 409 + (int)((graus / 180.0f) * (1966 - 409));
  ledcWrite(CH_SERVO, duty);
}

void setServoVarredura(float freq_hz, float ang_min, float ang_max, float tempo_s) {
  float seno = (sin(2.0f * PI * freq_hz * tempo_s) + 1.0f) / 2.0f;
  float angulo = ang_min + (seno * (ang_max - ang_min));
  setServoAngulo(angulo);
}

void desligarTudo() {
  setPwmDuty(CH_STROBE, 0);
  setGloboRGB(0, 0, 0);
  setServoAngulo(90.0f);
}

// ------------------------------------------------------------------------------
// 6. PROCESSAMENTO DE CORES & PALETA CONTEXTUAL
// ------------------------------------------------------------------------------
void atualizarPaletaGlobo(String modo, float nivel_medios, float tempo_s) {
  float brilho_base = max(20.0f, nivel_medios * 100.0f);

  if (modo == "suave") {
    float onda = (sin(tempo_s * 0.8f) + 1.0f) / 2.0f;
    float r = 0.0f;
    float g = onda * 40.0f * (brilho_base / 100.0f);
    float b = (1.0f - onda * 0.5f) * 80.0f * (brilho_base / 100.0f);
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
    float r = onda * 70.0f * (brilho_base / 100.0f);
    float g = (1.0f - onda) * 40.0f * (brilho_base / 100.0f);
    float b = 85.0f * (brilho_base / 100.0f);
    setGloboRGB(r, g, b);
  }
}

// ------------------------------------------------------------------------------
// 7. SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==================================================");
  Serial.println(" Audio to Light - ESP32-C3 Super Mini (Servo SG90)");
  Serial.println("==================================================");

  pinMode(PIN_LED_ONBOARD, OUTPUT);
  digitalWrite(PIN_LED_ONBOARD, HIGH);

  // Strobe Branco (15Hz / 8 bits)
  ledcSetup(CH_STROBE, PWM_FREQ_STROBE, 8);
  ledcAttachPin(PIN_STROBE_BRANCO, CH_STROBE);

  // Globo RGB (100Hz / 8 bits)
  ledcSetup(CH_GLOBO_R, PWM_FREQ_GLOBO, 8);
  ledcAttachPin(PIN_GLOBO_R, CH_GLOBO_R);

  ledcSetup(CH_GLOBO_G, PWM_FREQ_GLOBO, 8);
  ledcAttachPin(PIN_GLOBO_G, CH_GLOBO_G);

  ledcSetup(CH_GLOBO_B, PWM_FREQ_GLOBO, 8);
  ledcAttachPin(PIN_GLOBO_B, CH_GLOBO_B);

  // Servo SG90 (50Hz / 14 bits para precisão angular)
  ledcSetup(CH_SERVO, PWM_FREQ_SERVO, 14);
  ledcAttachPin(PIN_SERVO_GLOBO, CH_SERVO);

  desligarTudo();

  // Conexão Wi-Fi
  Serial.printf("Conectando ao Wi-Fi: %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_ONBOARD, !digitalRead(PIN_LED_ONBOARD));
    delay(200);
    Serial.print(".");
  }

  digitalWrite(PIN_LED_ONBOARD, LOW);
  Serial.printf("\nWi-Fi Conectado! IP do ESP32: %s\n", WiFi.localIP().toString().c_str());

  udp.begin(UDP_PORT);
  Serial.printf("Escutando pacotes UDP na porta %d...\n", UDP_PORT);
}

// ------------------------------------------------------------------------------
// 8. LOOP PRINCIPAL
// ------------------------------------------------------------------------------
void loop() {
  unsigned long agora = millis();
  float tempo_s = agora / 1000.0f;

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(500);
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

          // --- COREOGRAFIA 1: GLOBO RGB ---
          atualizarPaletaGlobo(modo_atual, nivel_medios, tempo_s);

          // --- COREOGRAFIA 2: SERVO MOTOR SG90 ---
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

          // --- COREOGRAFIA 3: STROBE BRANCO (GRAVES) ---
          if (modo_atual == "suave") {
            if (ativo_grave || nivel_grave > 0.3f) {
              int duty = (int)(min(35.0f, nivel_grave * 35.0f) * 2.55f);
              setPwmDuty(CH_STROBE, duty);
              fimPulsoStrobe = agora + 70;
            }
          } else {
            if (ativo_grave || pico_grave || nivel_grave >= 0.40f) {
              int duty = (modo_atual == "alta_energia") ? (int)(50.0f * 2.55f) : (int)(45.0f * 2.55f);
              setPwmDuty(CH_STROBE, duty);
              fimPulsoStrobe = agora + 70;
            }
          }
        }
      }
    }
  }

  if (agora >= fimPulsoStrobe) {
    setPwmDuty(CH_STROBE, 0);
  }

  // Failsafe se ficar sem pacotes por mais de 4s
  if (agora - ultimoPacoteAudio > 4000 && ultimoPacoteAudio > 0) {
    desligarTudo();
  }
}
