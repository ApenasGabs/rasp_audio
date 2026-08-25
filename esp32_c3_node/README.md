# 📡 Nó Receptor Sem Fio: ESP32-C3 Super Mini (Audio to Light)

Este firmware transforma a placa compacta **ESP32-C3 Super Mini** em um receptor sem fio UDP para o sistema de iluminação rítmica inteligente.

---

## 🔌 Pinagem do ESP32-C3 Super Mini

| Dispositivo | Função | Pino ESP32-C3 | Driver | Tipo de Controle |
| :--- | :--- | :--- | :--- | :--- |
| **⚪ Strobe Branco** | Graves / Kicks | **GPIO 0** | ULN2003 | PWM 15Hz (Strobe) |
| **🟢 Laser Verde** | Pratos / Hi-Hats | **GPIO 1** | ULN2003 | Digital Flash |
| **🔴 Laser Vermelho** | Caixas / Kicks fortes | **GPIO 3** | ULN2003 | Digital Flash |
| **🔴 Globo (R)** | Cor Vermelha Globo | **GPIO 4** | ULN2003 | PWM (Cores) |
| **🟢 Globo (G)** | Cor Verde Globo | **GPIO 5** | ULN2003 | PWM (Cores) |
| **🔵 Globo (B)** | Cor Azul Globo | **GPIO 6** | ULN2003 | PWM (Cores) |
| **⚙️ Motor Filtro Laser** | IN1 / IN2 Filtro | **GPIO 7 / GPIO 10** | Ponte H | PWM Bidirecional (Oscilação) |
| **🌐 Motor Globo** | IN1 / IN2 Globo | **GPIO 20 / GPIO 21** | Ponte H | PWM Bidirecional (Varredura) |
| **💡 Status Wi-Fi** | LED Onboard | **GPIO 8** | Interno | Pisca (Conectando) / Fixo (OK) |

---

## ⚡ Esquema de Conexão dos Drivers

```
[ ESP32-C3 Super Mini ] (3.3V Logic)
     │
     ├── GPIO 0, 1, 3, 4, 5, 6  ──►  [ ULN2003 IN1..IN6 ] ──► [ Lasers & LEDs 12V ]
     │                                     │
     │                                    GND (Comum)
     │
     └── GPIO 7, 10, 20, 21     ──►  [ Ponte H IN1..IN4 ] ──► [ Motores DC 5V/12V ]
                                           │
                                          GND (Comum)
```
*(Importante: Ligar o GND do ESP32-C3 ao GND do ULN2003, da Ponte H e da Fonte de 12V).*

---

## 🛠️ Como Gravar o Firmware

### Opção 1: Via Arduino IDE
1. Abra a **Arduino IDE**.
2. Vá em **Gerenciador de Placas** e instale o pacote `esp32` da Espressif.
3. Vá em **Gerenciador de Bibliotecas** e instale `ArduinoJson` (versão 6 ou 7).
4. Selecione a placa: **ESP32C3 Dev Module** (ou *ESP32-C3 Super Mini*).
5. Abra o arquivo `esp32_c3_node.ino`.
6. Edite as linhas `WIFI_SSID` e `WIFI_PASS` com a sua rede Wi-Fi.
7. Conecte o ESP32-C3 via cabo USB-C e clique em **Carregar (Upload)**.

### Opção 2: Via VS Code + PlatformIO
1. Abra a pasta `esp32_c3_node/` no VS Code com a extensão **PlatformIO**.
2. Edite `WIFI_SSID` e `WIFI_PASS` no `esp32_c3_node.ino`.
3. Clique em **Build** e **Upload**.

---

## 🚀 Operação em Conjunto com a Raspberry Pi

1. Ligue a **Raspberry Pi 3** e inicie os servidores:
   ```bash
   cd ~/rasp_audio
   python3 servidor.py
   python3 servidor_spotify.py
   ```
2. Ligue o **ESP32-C3 Super Mini**.
3. O ESP32-C3 se conectará ao mesmo Wi-Fi da Raspberry Pi, receberá automaticamente o broadcast UDP na porta `5005` e assumirá o show de iluminação sem precisar de nenhum cabo físico entre a Raspberry Pi e as luzes!
