/*
 * TESTE INFALÍVEL: TODAS AS PORTAS EM 3.3V (HIGH) PERMANENTE
 * Placa: ESP32-C3 Super Mini
 */

#include <Arduino.h>

// Lista de todos os GPIOs da placa
const int pinos[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 20, 21};
const int totalPinos = sizeof(pinos) / sizeof(pinos[0]);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n>>> FORCANDO TODAS AS PORTAS EM 3.3V (HIGH CONTINUO) <<<");

  for (int i = 0; i < totalPinos; i++) {
    pinMode(pinos[i], OUTPUT);
    digitalWrite(pinos[i], HIGH); // Coloca 3.3V constante
  }
}

void loop() {
  // Mantém todas as portas ligadas em 3.3V sem desligar nunca
  for (int i = 0; i < totalPinos; i++) {
    digitalWrite(pinos[i], HIGH);
  }

  Serial.println("[STATUS] Todas as portas estao em 3.3V (HIGH)");
  delay(2000);
}