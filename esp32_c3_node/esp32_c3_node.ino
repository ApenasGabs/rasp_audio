/*
 * ==============================================================================
 * PROJETO: Audio to Light (Nó Receptor Sem Fio ESP32-C3 Super Mini)
 * VERSÃO: 2.1 (Com analogWrite/digitalWrite universal para 100% de compatibilidade)
 * ==============================================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <math.h>

// ------------------------------------------------------------------------------
// 1. CONFIGURAÇÕES DE REDE WI-FI & UDP
// ------------------------------------------------------------------------------
const char* WIFI_SSID = "SEU_WIFI_NOME";        // << Substitua pelo seu Wi-Fi
const char* WIFI_PASS = "SUA_WIFI_SENHA";       // << Substitua pela senha
const unsigned int UDP_PORT = 5005;

WiFiUDP udp;
char packetBuffer[2048];

// ------------------------------------------------------------------------------
// 2. PINAGEM GPIO (ESP32-C3 Super Mini)
// ------------------------------------------------------------------------------
#define PIN_STROBE_BRANCO    0   // Pino marcado como 0 (Strobe nos Graves)
#define PIN_GLOBO_R          4   // Pino marcado como 4 (Globo Vermelho)
#define PIN_GLOBO_G          5   // Pino marcado como 5 (Globo Verde)
#define PIN_GLOBO_B          6   // Pino marcado como 6 (Globo Azul)
#define PIN_SERVO_GLOBO      7   // Pino marcado como 7 (Servo SG90)
#define PIN_LED_ONBOARD      8   // Pino marcado como 8 (LED azul onboard)

// ------------------------------------------------------------------------------
// 3. VARIÁVEIS DE ESTADO E TEMPORIZAÇÃO
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

// Temporizador do Servo Motor SG90 (geração de pulsos a 50Hz / 20ms)
unsigned long ultimoPulsoServo = 0;
int pulsoServoUs = 1500; // 1500us = 90 graus (centro)
float ladoSaltoServo = 30.0f;

// ------------------------------------------------------------------------------
// 4. FUNÇÕES DE CONTROLE DE HARDWARE (ANALOGWRITE & DIGITALWRITE)
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
  // Converte 0 a 180 graus em pulso de 550us a 2400us
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

// Atualiza o pulso do servo sem travar a CPU (a cada 20ms = 50Hz)
void atualizarServo(unsigned long agoraUs) {
  if (agoraUs - ultimoPulsoServo >= 20000) {
    ultimoPulsoServo = agoraUs;
    digitalWrite(PIN_SERVO_GLOBO, HIGH);
    delayMicroseconds(pulsoServoUs);
    digitalWrite(PIN_SERVO_GLOBO, LOW);
  }
}

// ------------------------------------------------------------------------------
// 5. PALETA DE CORES CONTEXTUAL DO GLOBO
// ------------------------------------------------------------------------------
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
// 6. SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n==================================================");
  Serial.println(" Audio to Light - ESP32-C3 (v2.1 Universal)");
  Serial.println("==================================================");

  // Configuração dos pinos como saída
  pinMode(PIN_STROBE_BRANCO, OUTPUT);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  // AUTO-TESTE INICIAL DE HARDWARE (Pisca cada canal para validar na hora)
  Serial.println("[AUTO-TESTE] 1. Testando Strobe (GPIO 0)...");
  setStrobe(255); delay(400); setStrobe(0);

  Serial.println("[AUTO-TESTE] 2. Testando Globo Vermelho (GPIO 4)...");
  setGloboRGB(100, 0, 0); delay(300);

  Serial.println("[AUTO-TESTE] 3. Testando Globo Verde (GPIO 5)...");
  setGloboRGB(0, 100, 0); delay(300);

  Serial.println("[AUTO-TESTE] 4. Testando Globo Azul (GPIO 6)...");
  setGloboRGB(0, 0, 100); delay(300);

  Serial.println("[AUTO-TESTE] 5. Testando Branco Total...");
  setGloboRGB(100, 100, 100); delay(400);
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

  digitalWrite(PIN_LED_ONBOARD, LOW); // LED Aceso Fixo = Conectado
  Serial.printf("\n[Wi-Fi] Conectado! IP do ESP32: %s\n", WiFi.localIP().toString().c_str());

  udp.begin(UDP_PORT);
  Serial.printf("[UDP] Escutando porta %d...\n", UDP_PORT);
}

// ------------------------------------------------------------------------------
// 7. LOOP PRINCIPAL
// ------------------------------------------------------------------------------
void loop() {
  unsigned long agora = millis();
  unsigned long agoraUs = micros();
  float tempo_s = agora / 1000.0f;

  // Atualiza o sinal do Servo SG90 a 50Hz
  atualizarServo(agoraUs);

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

      // Pisca LED onboard indicando pacote recebido
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

          // Exibe log a cada 40 pacotes (~2s)
          if (totalPacotesRecebidos % 40 == 0) {
            Serial.printf("[UDP #%lu] Modo: %s | Grave: %.2f | Strobe/Globo/Servo Ativos\n",
              totalPacotesRecebidos, modo_atual.c_str(), nivel_grave);
          }

          // 1. GLOBO RGB
          atualizarPaletaGlobo(modo_atual, nivel_medios, tempo_s);

          // 2. SERVO SG90
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

          // 3. STROBE BRANCO (GRAVES)
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

  // Desliga o Strobe após a sustentação
  if (agora >= fimPulsoStrobe) {
    setStrobe(0);
  }

  // Failsafe
  if (agora - ultimoPacoteAudio > 4000 && ultimoPacoteAudio > 0) {
    desligarTudo();
  }
}
