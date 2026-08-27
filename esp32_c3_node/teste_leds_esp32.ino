/*
 * ==============================================================================
 * TESTE FÍSICO DE LEDs E SERVO - ESP32-C3 SUPER MINI
 * ==============================================================================
 * Este código NÃO precisa de Wi-Fi nem de UDP.
 * Ele serve para você testar se os LEDs estão acendendo fisicamente (direto ou via ULN2003)
 * e se o Servo SG90 está girando.
 * ==============================================================================
 */

#include <Arduino.h>

// PINOS DOS LEDs E STROBE
#define PIN_STROBE_BRANCO    0   // Pino marcado como 0
#define PIN_GLOBO_R          4   // Pino marcado como 4
#define PIN_GLOBO_G          5   // Pino marcado como 5
#define PIN_GLOBO_B          6   // Pino marcado como 6

// PINO DO SERVO SG90
#define PIN_SERVO_GLOBO      7   // Pino marcado como 7

// LED ONBOARD DA PLACA
#define PIN_LED_ONBOARD      8   // Pino marcado como 8

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n==================================================");
  Serial.println(" TESTE DIRETO DE HARDWARE - ESP32-C3 SUPER MINI");
  Serial.println("==================================================");

  // Configura todos os pinos como SAÍDA DIGITAL pura (3.3V)
  pinMode(PIN_STROBE_BRANCO, OUTPUT);
  pinMode(PIN_GLOBO_R, OUTPUT);
  pinMode(PIN_GLOBO_G, OUTPUT);
  pinMode(PIN_GLOBO_B, OUTPUT);
  pinMode(PIN_SERVO_GLOBO, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);

  // Desliga tudo inicialmente
  digitalWrite(PIN_STROBE_BRANCO, LOW);
  digitalWrite(PIN_GLOBO_R, LOW);
  digitalWrite(PIN_GLOBO_G, LOW);
  digitalWrite(PIN_GLOBO_B, LOW);
  digitalWrite(PIN_SERVO_GLOBO, LOW);
  digitalWrite(PIN_LED_ONBOARD, HIGH); // Apagado
}

void loop() {
  Serial.println("\n--- INICIANDO CICLO DE TESTE DOS LEDs ---");

  // TESTE 1: Acende apenas o STROBE (GPIO 0)
  Serial.println("1. [GPIO 0] STROBE BRANCO -> LIGADO (HIGH / 3.3V)");
  digitalWrite(PIN_STROBE_BRANCO, HIGH);
  digitalWrite(PIN_LED_ONBOARD, LOW); // Acende LED onboard
  delay(1500);
  digitalWrite(PIN_STROBE_BRANCO, LOW);
  digitalWrite(PIN_LED_ONBOARD, HIGH);
  delay(500);

  // TESTE 2: Acende apenas o GLOBO VERMELHO (GPIO 4)
  Serial.println("2. [GPIO 4] GLOBO VERMELHO (R) -> LIGADO (HIGH / 3.3V)");
  digitalWrite(PIN_GLOBO_R, HIGH);
  delay(1500);
  digitalWrite(PIN_GLOBO_R, LOW);
  delay(500);

  // TESTE 3: Acende apenas o GLOBO VERDE (GPIO 5)
  Serial.println("3. [GPIO 5] GLOBO VERDE (G) -> LIGADO (HIGH / 3.3V)");
  digitalWrite(PIN_GLOBO_G, HIGH);
  delay(1500);
  digitalWrite(PIN_GLOBO_G, LOW);
  delay(500);

  // TESTE 4: Acende apenas o GLOBO AZUL (GPIO 6)
  Serial.println("4. [GPIO 6] GLOBO AZUL (B) -> LIGADO (HIGH / 3.3V)");
  digitalWrite(PIN_GLOBO_B, HIGH);
  delay(1500);
  digitalWrite(PIN_GLOBO_B, LOW);
  delay(500);

  // TESTE 5: Acende TODOS OS LEDs JUNTOS no máximo por 3 segundos
  Serial.println("5. [TODOS OS PINOS: 0, 4, 5, 6] -> TODOS LIGADOS 100%!");
  digitalWrite(PIN_STROBE_BRANCO, HIGH);
  digitalWrite(PIN_GLOBO_R, HIGH);
  digitalWrite(PIN_GLOBO_G, HIGH);
  digitalWrite(PIN_GLOBO_B, HIGH);
  delay(3000);

  // Apaga todos
  digitalWrite(PIN_STROBE_BRANCO, LOW);
  digitalWrite(PIN_GLOBO_R, LOW);
  digitalWrite(PIN_GLOBO_G, LOW);
  digitalWrite(PIN_GLOBO_B, LOW);
  delay(1000);

  // TESTE 6: Pisca em frequência de Strobe (15Hz) no GPIO 0
  Serial.println("6. [GPIO 0] Testando efeito Strobe rapido (15 piscadas)...");
  for (int i = 0; i < 30; i++) {
    digitalWrite(PIN_STROBE_BRANCO, HIGH);
    delay(33);
    digitalWrite(PIN_STROBE_BRANCO, LOW);
    delay(33);
  }

  delay(1000);
}
