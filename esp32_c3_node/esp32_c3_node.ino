/*
 * ==============================================================================
 * PROJETO: Audio to Light - NÓ RECEPTOR ESP32-C3 SUPER MINI (COM DMX512)
 * VERSÃO: 3.1 (Laser DMX com feixe 100% contínuo e sem apagões de transição)
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

// Lista de padrões geométricos contínuos e bem definidos (túneis, planos, círculos, ondas)
const uint8_t padroesLaser[] = {12, 25, 38, 52, 70, 95, 120, 145, 175, 205};
const int totalPadroes = sizeof(padroesLaser) / sizeof(padroesLaser[0]);
int indicePadrao = 0;
unsigned long ultimoTrocaPadrao = 0;

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

void atualizarLaserDMX(String modo, float nivel_graves, bool pico_grave, float nivel_medios, float tempo_s, unsigned long agora) {
  if (modo == "standby") {
    // Modo Blackout Total Seguro em silêncio/pausa
    dmxCanais[0] = 0;   // CH1: Modo fechado
    dmxCanais[1] = 0;   // CH2: Velocidade 0
    dmxCanais[2] = 0;   // CH3: Cor
    dmxCanais[4] = 0;   // CH5: Padrão
    return;
  }

  // CH1: Modo Manual do Console (Controle DMX total e contínuo)
  dmxCanais[0] = 50;  // 40-79 = Manual

  // CH2: Velocidade padrão do sistema
  dmxCanais[1] = 128;

  // CH14: PINTURA GRADUAL DESATIVADA (0 = Sem desenho gradual, laser NÃO apaga na troca de padrão!)
  dmxCanais[13] = 0;

  // CH15: Velocidade máxima dos galvanômetros para feixes nítidos e sólidos
  dmxCanais[14] = 255;

  // CH16: Exibição Padrão Contínua (0 = Sem piscar ou cortar feixes)
  dmxCanais[15] = 0;

  // CH7: Sem cortes de zoom irregular
  dmxCanais[6] = 0;

  // Gerenciamento suave de troca de padrão (mínimo de 1.8s entre trocas para manter o feixe sólido)
  if (pico_grave && (agora - ultimoTrocaPadrao >= 1800)) {
    indicePadrao = (indicePadrao + 1) % totalPadroes;
    ultimoTrocaPadrao = agora;
  }
  dmxCanais[4] = padroesLaser[indicePadrao]; // CH5: Padrão atual contínuo

  if (modo == "suave") {
    dmxCanais[2] = 45;   // CH3: Cor Ciano/Azul suave contínua
    dmxCanais[3] = 0;    // CH4: Cor estável
    dmxCanais[5] = 120;  // CH6: Tamanho médio relaxante
    dmxCanais[7] = 135;  // CH8: Rotação lenta e constante
    dmxCanais[8] = 64;   // CH9: Centro
    dmxCanais[9] = 64;   // CH10: Centro
    dmxCanais[10] = 64;  // CH11: Centro
    dmxCanais[11] = 64;  // CH12: Centro
    dmxCanais[12] = 0;   // CH13: Sem distorções
  }
  else if (modo == "alta_energia") {
    dmxCanais[2] = 92;   // CH3: Cores multicoloridas brilhantes
    dmxCanais[3] = 80;   // CH4: Fluxo de cores suave e contínuo

    // Zoom expansivo que acompanha a intensidade do grave (sem nunca diminuir de 160)
    dmxCanais[5] = (uint8_t)(160.0f + (nivel_graves * 85.0f)); // CH6: Tamanho (160 a 245)

    // Rotação 3D contínua acelerada
    dmxCanais[7] = 230;  // CH8: Rotação rápida contínua

    // Efeitos espaciais suaves nos eixos sem corte de feixe
    dmxCanais[8] = (pico_grave) ? 160 : 64;  // CH9: Inversão horizontal no kick
    dmxCanais[9] = 64;                       // CH10: Vertical
    dmxCanais[10] = (uint8_t)(64 + sin(tempo_s * 1.5f) * 35); // CH11: Varredura X contínua
    dmxCanais[11] = 64;                      // CH12: Mov Y
    dmxCanais[12] = (uint8_t)(nivel_medios * 120.0f); // CH13: Ondulação suave
  }
  else { // media_energia / fallback
    dmxCanais[2] = 85;   // CH3: Troca de cores fluida
    dmxCanais[3] = 40;   // CH4: Fluxo suave
    dmxCanais[5] = (uint8_t)(140.0f + (nivel_graves * 60.0f)); // CH6: Tamanho
    dmxCanais[7] = 160;  // CH8: Rotação moderada
    dmxCanais[8] = 64;
    dmxCanais[9] = 64;
    dmxCanais[10] = 64;
    dmxCanais[11] = 64;
    dmxCanais[12] = (uint8_t)(nivel_medios * 70.0f);
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
  Serial.println(" Audio to Light - ESP32-C3 (Laser DMX Continuo)");
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
void loop() {
  unsigned long agora = millis();
  unsigned long agoraUs = micros();
  float tempo_s = agora / 1000.0f;

  atualizarServo(agoraUs);

  // Envio contínuo DMX512 a ~30Hz (a cada 33ms) para manter o laser fluido
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

          // 1. ATUALIZA LASER DMX (Feixes contínuos e sólidos)
          atualizarLaserDMX(modo_atual, nivel_grave, pico_grave, nivel_medios, tempo_s, agora);

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
