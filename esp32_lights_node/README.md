# 💡 ESP32 Nó 1: Iluminação & Movimento (Strobe + Globo RGB + Servo SG90)

Firmware independente dedicado a:
1. **⚪ Strobe Branco 12V (GPIO 0 via ULN2003):** Com ajuste web de **duração do disparo (delay)**, **quantidade de piscadas (1 a 10 flashes)** e sensibilidade ao bumbo.
2. **🌈 Globo RGB (GPIO 4, 5, 6 via ULN2003):** Cores contextuais e modulação por voz com ajuste de brilho máximo.
3. **🤖 Servo Motor SG90 (GPIO 7):** Movimento do globo com varredura angular (min/max) e Drop Jumps no kick.
4. **🌐 Painel Web HTTP (Porta 80):** Calibração em tempo real de cada item com botão para salvar na Flash!

### 🔌 Pinagem (ESP32-C3 Super Mini):
* `GPIO 0`: Strobe Branco (ULN2003)
* `GPIO 4`: Globo Vermelho (ULN2003)
* `GPIO 5`: Globo Verde (ULN2003)
* `GPIO 6`: Globo Azul (ULN2003)
* `GPIO 7`: Servo Motor SG90 (Sinal PWM 50Hz)
* `GPIO 8`: LED Onboard
