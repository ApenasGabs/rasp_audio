/*
 * ==============================================================================
 * PROJETO: Audio to Light (Nó Receptor Sem Fio ESP32-C3 Super Mini)
 * DESCRIÇÃO: Escuta pacotes UDP :5005 transmitidos pela Raspberry Pi (Áudio + Spotify)
 *            e orquestra 8 atuadores via ULN2003 e Ponte H.
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
#define PIN_LASER_VERDE      1   // ULN2003 - Pratos / Hi-Hats (Digital)
#define PIN_LASER_VERMELHO   3   // ULN2003 - Caixas / Transientes (Digital)
#define PIN_GLOBO_R          4   // ULN2003 - Globo LED Vermelho (PWM)
#define PIN_GLOBO_G          5   // ULN2003 - Globo LED Verde (PWM)
#define PIN_GLOBO_B          6   // ULN2003 - Globo LED Azul (PWM)

// Ponte H Bidirecional (Motores)
#define PIN_MOT_LASER_IN1    7   // Ponte H - Motor Filtro Laser IN1
#define PIN_MOT_LASER_IN2   10   // Ponte H - Motor Filtro Laser IN2
#define PIN_MOT_GLOBO_IN1   20   // Ponte H - Motor Globo Giratório IN1
#define PIN_MOT_GLOBO_IN2   21   // Ponte H - Motor Globo Giratório IN2

#define PIN_LED_ONBOARD      8   // LED de status onboard do ESP32-C3

// ------------------------------------------------------------------------------
// 3. CANAIS E CONFIGURAÇÃO PWM (LEDC ESP32)
// ------------------------------------------------------------------------------
#define PWM_FREQ_STROBE     15    // 15Hz para efeito Strobe autêntico
#define PWM_FREQ_GLOBO     100    // 100Hz para blend suave de cores
#define PWM_FREQ_MOTORES  1000    // 1kHz para motores DC sem ruído audível
#define PWM_RESOLUTION       8    // 8 bits (0 a 255)

// Canais LEDC (ESP32 Core v2 / v3 compatível)
#define CH_STROBE     0
#define CH_GLOBO_R    1
#define CH_GLOBO_G    2
#define CH_GLOBO_B    3
#define CH_MOT_L_IN1  4
#define CH_MOT_L_IN2  5
#define CH_MOT_G_IN1  6
#define CH_MOT_G_IN2  7

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

// Temporizadores não-bloqueantes (millis)
unsigned long fimPulsoStrobe = 0;
unsigned long fimPulsoLaserG = 0;
unsigned long fimPulsoLaserR = 0;
unsigned long ultimoInversaoGlobo = 0;
unsigned long ultimoPacoteAudio = 0;

int direcaoGlobo = 1;
int contadorKicks = 0;

// Variáveis de controle de motor
float velGloboAtual = 0.0;
float velGloboAlvo = 0.0;
float velLaserAtual = 0.0;
float velLaserAlvo = 0.0;
int dirLaserAlvo = 1;

// ------------------------------------------------------------------------------
// 5. FUNÇÕES AUXILIARES DE ATUADORES
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

void setMotorPonteH(int ch_in1, int ch_in2, float velocidade_pct, int direcao) {
  int duty = (int)(constrain(velocidade_pct, 0.0, 100.0) * 2.55);
  if (direcao == 1) {
    setPwmDuty(ch_in2, 0);
    setPwmDuty(ch_in1, duty);
  } else if (direcao == -1) {
    setPwmDuty(ch_in1, 0);
    setPwmDuty(ch_in2, duty);
  } else {
    setPwmDuty(ch_in1, 0);
    setPwmDuty(ch_in2, 0);
  }
}

void desligarTudo() {
  setPwmDuty(CH_STROBE, 0);
  digitalWrite(PIN_LASER_VERDE, LOW);
  digitalWrite(PIN_LASER_VERMELHO, LOW);
  setGloboRGB(0, 0, 0);
  setMotorPonteH(CH_MOT_L_IN1, CH_MOT_L_IN2, 0, 0);
  setMotorPonteH(CH_MOT_G_IN1, CH_MOT_G_IN2, 0, 0);
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
  Serial.println(" Audio to Light - ESP32-C3 Super Mini Node");
  Serial.println("==================================================");

  // Configuração dos Pinos Digitais
  pinMode(PIN_LASER_VERDE, OUTPUT);
  pinMode(PIN_LASER_VERMELHO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);
  digitalWrite(PIN_LASER_VERDE, LOW);
  digitalWrite(PIN_LASER_VERMELHO, LOW);
  digitalWrite(PIN_LED_ONBOARD, HIGH); // Apagado inicial

  // Configuração dos Canais LEDC / PWM
  ledcSetup(CH_STROBE, PWM_FREQ_STROBE, PWM_RESOLUTION);
  ledcAttachPin(PIN_STROBE_BRANCO, CH_STROBE);

  ledcSetup(CH_GLOBO_R, PWM_FREQ_GLOBO, PWM_RESOLUTION);
  ledcAttachPin(PIN_GLOBO_R, CH_GLOBO_R);

  ledcSetup(CH_GLOBO_G, PWM_FREQ_GLOBO, PWM_RESOLUTION);
  ledcAttachPin(PIN_GLOBO_G, CH_GLOBO_G);

  ledcSetup(CH_GLOBO_B, PWM_FREQ_GLOBO, PWM_RESOLUTION);
  ledcAttachPin(PIN_GLOBO_B, CH_GLOBO_B);

  ledcSetup(CH_MOT_L_IN1, PWM_FREQ_MOTORES, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOT_LASER_IN1, CH_MOT_L_IN1);
  ledcSetup(CH_MOT_L_IN2, PWM_FREQ_MOTORES, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOT_LASER_IN2, CH_MOT_L_IN2);

  ledcSetup(CH_MOT_G_IN1, PWM_FREQ_MOTORES, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOT_GLOBO_IN1, CH_MOT_G_IN1);
  ledcSetup(CH_MOT_G_IN2, PWM_FREQ_MOTORES, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOT_GLOBO_IN2, CH_MOT_G_IN2);

  desligarTudo();

  // Conexão Wi-Fi
  Serial.printf("Conectando ao Wi-Fi: %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_ONBOARD, !digitalRead(PIN_LED_ONBOARD)); // Pisca LED
    delay(200);
    Serial.print(".");
  }

  digitalWrite(PIN_LED_ONBOARD, LOW); // LED Aceso (Conectado)
  Serial.printf("\nWi-Fi Conectado! IP do ESP32: %s\n", WiFi.localIP().toString().c_str());

  // Inicia Socket UDP
  udp.begin(UDP_PORT);
  Serial.printf("Escutando pacotes UDP na porta %d...\n", UDP_PORT);
}

// ------------------------------------------------------------------------------
// 8. LOOP PRINCIPAL
// ------------------------------------------------------------------------------
void loop() {
  unsigned long agora = millis();
  float tempo_s = agora / 1000.0f;

  // Reconexão Wi-Fi automática
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(500);
    return;
  }

  // 1. LEITURA DE PACOTES UDP
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = '\0';

      StaticJsonDocument<1536> doc;
      DeserializationError error = deserializeJson(doc, packetBuffer);

      if (!error) {
        const char* tipo = doc["tipo"] | "audio";

        // --- PACOTE SPOTIFY (O CÉREBRO) ---
        if (strcmp(tipo, "spotify") == 0) {
          spotify.ativo = true;
          spotify.tocando = doc["tocando"] | false;
          spotify.energia = doc["energia"] | 0.6f;
          spotify.danceabilidade = doc["danceabilidade"] | 0.6f;
          spotify.modo_sugerido = doc["modo_sugerido"] | "media_energia";
          spotify.ultimo_timestamp = agora;
        }
        // --- PACOTE ÁUDIO (O REFLEXO) ---
        else {
          ultimoPacoteAudio = agora;

          // Determina o modo atual
          bool spotify_online = spotify.ativo && (agora - spotify.ultimo_timestamp < 4000);
          String modo_atual = "fallback";
          if (spotify_online) {
            modo_atual = spotify.tocando ? spotify.modo_sugerido : "standby";
          }

          // Extração das frequências
          JsonObject faixas = doc["faixas"];
          JsonObject graves = faixas["graves"];
          JsonObject medios = faixas["medios"];
          JsonObject agudos = faixas["agudos"];
          JsonObject super_agudos = faixas["super_agudos"];

          bool ativo_grave = graves["ativo"] | false;
          bool pico_grave = graves["pico"] | false;
          float nivel_grave = graves["nivel"] | 0.0f;

          float nivel_medios = medios["nivel"] | 0.3f;
          bool ativo_medios = medios["ativo"] | false;

          bool ativo_agudo = agudos["ativo"] | false;
          bool pico_agudo = agudos["pico"] | false;
          float nivel_agudo = agudos["nivel"] | 0.0f;

          bool pico_super = super_agudos["pico"] | false;

          // --- COREOGRAFIA 1: GLOBO RGB ---
          atualizarPaletaGlobo(modo_atual, nivel_medios, tempo_s);

          // --- COREOGRAFIA 2: MOTORES BIDIRECIONAIS (PONTE H) ---
          unsigned long tempoVarredura = (modo_atual == "alta_energia") ? 4000 : 6500;
          if (agora - ultimoInversaoGlobo >= tempoVarredura) {
            direcaoGlobo = (direcaoGlobo == 1) ? -1 : 1;
            ultimoInversaoGlobo = agora;
          }

          // Inversão por impacto no Drop/Kick pesado
          if (pico_grave && nivel_grave >= 0.85f) {
            contadorKicks++;
            if (contadorKicks >= 4) {
              direcaoGlobo = (direcaoGlobo == 1) ? -1 : 1;
              contadorKicks = 0;
              ultimoInversaoGlobo = agora;
            }
          }

          if (modo_atual == "alta_energia") {
            velGloboAlvo = 100.0f;
            // Oscilação rápida do filtro do laser a 2.5Hz
            float senoLaser = sin(2.0f * PI * 2.5f * tempo_s);
            dirLaserAlvo = (senoLaser >= 0) ? 1 : -1;
            velLaserAlvo = fabs(senoLaser) * 100.0f;
          } 
          else if (modo_atual == "suave") {
            velGloboAlvo = 25.0f;
            velLaserAlvo = 0.0f;
          } 
          else if (modo_atual == "standby") {
            if (ativo_grave || pico_grave || ativo_agudo) {
              velGloboAlvo = 45.0f;
              velLaserAlvo = 30.0f;
              dirLaserAlvo = 1;
            } else {
              velGloboAlvo = 0.0f;
              velLaserAlvo = 0.0f;
            }
          } 
          else { // media_energia / fallback
            velGloboAlvo = 60.0f;
            if (ativo_agudo || pico_agudo) {
              float senoLaser = sin(2.0f * PI * 1.8f * tempo_s);
              dirLaserAlvo = (senoLaser >= 0) ? 1 : -1;
              velLaserAlvo = fabs(senoLaser) * 75.0f;
            } else {
              velLaserAlvo = 35.0f;
              dirLaserAlvo = 1;
            }
          }

          setMotorPonteH(CH_MOT_G_IN1, CH_MOT_G_IN2, velGloboAlvo, direcaoGlobo);
          setMotorPonteH(CH_MOT_L_IN1, CH_MOT_L_IN2, velLaserAlvo, dirLaserAlvo);

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

          // --- COREOGRAFIA 4: LASERS VERDE E VERMELHO ---
          if (modo_atual != "suave") {
            if (pico_agudo || (ativo_agudo && nivel_agudo >= 0.40f)) {
              digitalWrite(PIN_LASER_VERDE, HIGH);
              fimPulsoLaserG = agora + 50;
            }
            if ((pico_grave && nivel_grave >= 0.70f) || pico_super) {
              digitalWrite(PIN_LASER_VERMELHO, HIGH);
              fimPulsoLaserR = agora + 60;
            }
          }
        }
      }
    }
  }

  // 2. GESTÃO DE SUSTENTAÇÃO TEMPORAL (PULSOS)
  if (agora >= fimPulsoStrobe) {
    setPwmDuty(CH_STROBE, 0);
  }
  if (agora >= fimPulsoLaserG) {
    digitalWrite(PIN_LASER_VERDE, LOW);
  }
  if (agora >= fimPulsoLaserR) {
    digitalWrite(PIN_LASER_VERMELHO, LOW);
  }

  // 3. FAILSAFE: Se ficar sem pacotes por mais de 4 segundos, desliga suavemente
  if (agora - ultimoPacoteAudio > 4000 && ultimoPacoteAudio > 0) {
    desligarTudo();
  }
}
