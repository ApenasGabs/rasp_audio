# 📡 Nó Receptor Sem Fio: ESP32-C3 Super Mini (Servo SG90 + Strobe + Globo RGB)

Este firmware transforma o **ESP32-C3 Super Mini** em um receptor sem fio de iluminação inteligente via rede Wi-Fi UDP.

---

## 🔌 Pinagem no ESP32-C3 Super Mini

| Dispositivo | Função | Pino ESP32-C3 | Driver | Tipo de Sinal |
| :--- | :--- | :--- | :--- | :--- |
| **⚪ Strobe Branco** | Graves / Kicks | **GPIO 0** | ULN2003 | PWM 15Hz (Strobe) |
| **🔴 Globo - R** | Cor Vermelha Globo | **GPIO 4** | ULN2003 | PWM (Cores) |
| **🟢 Globo - G** | Cor Verde Globo | **GPIO 5** | ULN2003 | PWM (Cores) |
| **🔵 Globo - B** | Cor Azul Globo | **GPIO 6** | ULN2003 | PWM (Cores) |
| **🤖 Servo SG90** | Movimento do Globo | **GPIO 7** | Direto (Sinal PWM) | PWM 50Hz (0° a 180°) |
| **💡 Status Wi-Fi** | LED Onboard | **GPIO 8** | Interno | Pisca (Conectando) / Fixo (OK) |

---

## ⚡ Esquema de Ligação

```
[ ESP32-C3 Super Mini ] (3.3V Logic)
     │
     ├── GPIO 0, 4, 5, 6  ──►  [ ULN2003 IN1..IN4 ] ──► [ Strobe 12V & Globo RGB ]
     │                                │
     │                               GND (Comum)
     │
     └── GPIO 7 (Sinal)   ──►  [ Servo Motor SG90 ] (Fio Laranja/Amarelo)
                                      │
                               5V e GND (Fios Vermelho e Marrom)
```
*(Importante: O Servo SG90 deve ser alimentado na linha de 5V e ter o GND unificado com o ESP32-C3 e o ULN2003).*

---

## 🛠️ Como Gravar o Firmware

1. Abra o arquivo `esp32_c3_node.ino` na **Arduino IDE** ou VS Code + PlatformIO.
2. Instale a biblioteca **ArduinoJson** (Gerenciador de Bibliotecas).
3. Altere `WIFI_SSID` e `WIFI_PASS` com os dados do seu Wi-Fi.
4. Selecione a placa: **ESP32C3 Dev Module** (ou *ESP32-C3 Super Mini*).
5. Conecte o ESP32-C3 via cabo USB-C e clique em **Carregar (Upload)**.
