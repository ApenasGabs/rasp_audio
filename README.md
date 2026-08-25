# 📝 CONTEXTO DO PROJETO: Audio to Light (Raspberry Pi + ESP32-C3 Híbrido)

Este documento fornece o **contexto completo do projeto**, arquitetura de software, pinagem detalhada dos atuadores (ULN2003 e Ponte H Bidirecional), firmware para o microcontrolador sem fio **ESP32-C3 Super Mini**, métodos de acesso remoto à Raspberry Pi 3 e guia operacional.

---

## 📌 1. Visão Geral e Objetivo

Sistema de **Show de Iluminação Rítmico Inteligente (Stage Lighting Controller)** operando de forma híbrida e modular:
1. **Central de Processamento (Raspberry Pi 3):**
   - **O Reflexo:** Microfone USB com FFT e Onset Detection com Baseline Assimétrica em tempo real.
   - **O Cérebro:** Integração Spotify Web API via `spotipy` com inferência de gênero, energia e seções estruturais.
   - **Transmissão:** Broadcast UDP de pacotes multiplexados na porta `5005` via rede Wi-Fi.
2. **Nó Receptor de Hardware (Raspberry Pi GPIO ou ESP32-C3 Super Mini Sem Fio):**
   - Recebe os pacotes UDP via Wi-Fi e controla os 8 canais de atuadores físicos (Lasers, Globo RGB, Strobe e Motores Bidirecionais com varredura e oscilação).

---

## 🌐 2. Diagrama da Arquitetura do Sistema

```mermaid
graph TD
    subgraph Central (Raspberry Pi 3)
        MIC[Microfone USB] --> S_AUD[servidor.py]
        SP[Spotify Web API] --> S_SP[servidor_spotify.py]
        S_AUD -->|UDP Broadcast :5005 Wi-Fi| NET((Rede Wi-Fi / UDP))
        S_SP -->|UDP Broadcast :5005 Wi-Fi| NET
    end

    subgraph Opção 1: Controle Direto na RPi
        NET --> C_LED[cliente_leds.py]
        C_LED --> GPIO_RPi[GPIO RPi: ULN2003 + Ponte H]
    end

    subgraph Opção 2: Nó Sem Fio (ESP32-C3 Super Mini)
        NET --> ESP[esp32_c3_node.ino]
        ESP --> GPIO_ESP[GPIO ESP32: ULN2003 + Ponte H]
    end
```

---

## 🔌 3. Tabela de Pinagem Comparativa (Raspberry Pi vs ESP32-C3 Super Mini)

| Dispositivo / Carga | Atuador Físico | Pino Raspberry Pi (BCM) | Pino ESP32-C3 | Driver | Tipo de Sinal |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **⚪ Strobe Branco** | LED 12V Branco | **GPIO 17** | **GPIO 0** | **ULN2003** | PWM 15Hz (Strobe no Bumbo) |
| **🟢 Laser Verde** | Diodo Laser Verde | **GPIO 27** | **GPIO 1** | **ULN2003** | Digital (Flashes em Pratos) |
| **🔴 Laser Vermelho** | Diodo Laser Vermelho | **GPIO 22** | **GPIO 3** | **ULN2003** | Digital (Ataques / Caixas) |
| **🔴 Globo - Red (R)** | LED Vermelho Globo | **GPIO 23** | **GPIO 4** | **ULN2003** | PWM (Modulação de Cores) |
| **🟢 Globo - Green (G)** | LED Verde Globo | **GPIO 24** | **GPIO 5** | **ULN2003** | PWM (Modulação de Cores) |
| **🔵 Globo - Blue (B)** | LED Azul Globo | **GPIO 25** | **GPIO 6** | **ULN2003** | PWM (Modulação de Cores) |
| **⚙️ Motor Filtro Laser**| Motor DC Filtro Óptico | **IN1: 18 / IN2: 13** | **IN1: 7 / IN2: 10** | **Ponte H** | **PWM Bidirecional (Oscilação a 2.5Hz)** |
| **🌐 Motor Globo** | Motor DC Globo Giratório | **IN1: 26 / IN2: 19** | **IN1: 20 / IN2: 21**| **Ponte H** | **PWM Bidirecional (Varredura / Sweep)** |

---

## 📂 4. Estrutura dos Arquivos do Repositório

```
rasp_audio/
├── servidor.py             # Reflexo: Microfone + FFT + Detecção Rítmica
├── servidor_spotify.py     # Cérebro: Contexto Spotify + Proteção Rate Limit
├── cliente_leds.py         # Orquestrador local na Raspberry Pi
├── cliente_espectro.py     # Monitor visual ASCII híbrido
├── config_hardware.json    # Configuração de pinagem da Raspberry Pi
├── drivers_hardware.py     # Classes de hardware (LuzPWM, GloboRGB, MotorBidirecional)
├── test_hardware.py        # Teste de bancada interativo na Raspberry Pi
├── esp32_c3_node/          # Nó Receptor Sem Fio para ESP32-C3 Super Mini
│   ├── esp32_c3_node.ino   # Firmware Arduino / C++ para o ESP32-C3
│   ├── platformio.ini      # Configuração para compilação via PlatformIO
│   └── README.md           # Guia de gravação e ligação do ESP32-C3
├── CONTEXTO.md             # Guia completo para desenvolvedores e IAs
├── README.md               # Documentação principal
├── requirements.txt        # Dependências Python
└── .gitignore              # Proteção de credenciais (.env e .cache_spotify)
```

---

## 🚀 5. Como Operar o Sistema

### Cenário A: Com o ESP32-C3 Super Mini Sem Fio (Recomendado)
1. Na **Raspberry Pi 3**, inicie os servidores:
   ```bash
   cd ~/rasp_audio
   python3 servidor.py
   python3 servidor_spotify.py
   ```
2. Ligue o **ESP32-C3 Super Mini** (conectado na luminária via ULN2003 e Ponte H). Ele se conectará no Wi-Fi e sincronizará automaticamente via UDP.

### Cenário B: Direto na Raspberry Pi (Com Fios nos GPIOs da RPi)
```bash
cd ~/rasp_audio
python3 servidor.py
python3 servidor_spotify.py
python3 cliente_leds.py
```
