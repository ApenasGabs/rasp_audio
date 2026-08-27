# 📝 CONTEXTO DO PROJETO: Audio to Light (Raspberry Pi + ESP32-C3)

Este documento fornece o **contexto completo do projeto**, arquitetura de software, pinagem detalhada dos atuadores (ULN2003 e Servo Motor SG90), firmware para o microcontrolador sem fio **ESP32-C3 Super Mini**, métodos de acesso remoto e guia operacional.

---

## 📌 1. Visão Geral e Objetivo

Sistema de **Show de Iluminação Rítmico Inteligente (Stage Lighting Controller)** operando de forma híbrida e modular:
1. **Central de Processamento (Raspberry Pi 3):**
   - **O Reflexo:** Microfone USB com FFT e Onset Detection com Baseline Assimétrica em tempo real.
   - **O Cérebro:** Integração Spotify Web API via `spotipy` com inferência de gênero, energia e seções estruturais.
   - **Transmissão:** Broadcast UDP de pacotes multiplexados na porta `5005` via rede Wi-Fi.
2. **Nó de Atuadores (Raspberry Pi GPIO ou ESP32-C3 Super Mini Sem Fio):**
   - **⚪ Strobe Branco (12V):** Pisca em frequência estroboscópica de 15Hz nas batidas de grave / kick.
   - **🌈 Globo RGB (Cores):** Modulação de paletas contextuais (cores frias no calmo, quentes no drop/refrão).
   - **🤖 Servo Motor SG90 (Movimento do Globo):** Varredura angular rítmica (0° a 180°) e saltos instantâneos de ângulo nas batidas fortes (*Drop Jumps*).

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
        C_LED --> GPIO_RPi[GPIO RPi: ULN2003 + Servo SG90]
    end

    subgraph Opção 2: Nó Sem Fio (ESP32-C3 Super Mini)
        NET --> ESP[esp32_c3_node.ino]
        ESP --> GPIO_ESP[GPIO ESP32: ULN2003 + Servo SG90]
    end
```

---

## 🔌 3. Tabela de Pinagem Comparativa

| Dispositivo / Carga | Atuador Físico | Pino Raspberry Pi (BCM) | Pino ESP32-C3 | Driver | Tipo de Sinal |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **⚪ Strobe Branco** | LED 12V Branco | **GPIO 17** (Pino 11) | **GPIO 0** | **ULN2003** | PWM 15Hz (Strobe no Bumbo) |
| **🔴 Globo - Red (R)** | LED Vermelho Globo | **GPIO 23** (Pino 16) | **GPIO 4** | **ULN2003** | PWM (Modulação de Cores) |
| **🟢 Globo - Green (G)** | LED Verde Globo | **GPIO 24** (Pino 18) | **GPIO 5** | **ULN2003** | PWM (Modulação de Cores) |
| **🔵 Globo - Blue (B)** | LED Azul Globo | **GPIO 25** (Pino 22) | **GPIO 6** | **ULN2003** | PWM (Modulação de Cores) |
| **🤖 Servo SG90 (Globo)** | Servo Motor Angular | **GPIO 18** (Pino 12) | **GPIO 7** | **Direto (Sinal PWM)** | **PWM 50Hz (0° a 180°)** |

---

## 🎛️ 4. Coreografia e Dinâmica do Servo SG90

| Modo Spotify / Áudio | Strobe Branco (Graves) | Globo RGB (Cores) | Servo Motor SG90 (Movimento) |
| :--- | :--- | :--- | :--- |
| **Alta Energia / Drop** | **Strobe Intenso 15Hz @ 50%** | **Cores Quentes & Rotação Eufórica** (Magenta, Vermelho, Amarelo) | **Varredura rápida (20°–160° a 1.0Hz) + Saltos de 30° a 150° a cada kick forte.** |
| **Média Energia / Versos** | Strobe Rítmico @ 45% | Transição Suave (Violeta, Azul, Dourado) | **Varredura senoidal contínua (30°–150° a 0.5Hz).** |
| **Suave / Lofi / Acústica** | Brilho pulsante suave (sem estrobo) | Tons Frios Relaxantes (Azul / Ciano) | **Movimento pendular lento (50°–130° a 0.2Hz).** |
| **Standby** | Apagado (ou reflexo se houver som físico) | Desligado | **Centralizado em 90° (repouso silencioso).** |

---

## 📂 5. Estrutura dos Arquivos do Repositório

```
rasp_audio/
├── servidor.py             # Reflexo: Microfone + FFT + Detecção Rítmica
├── servidor_spotify.py     # Cérebro: Contexto Spotify + Proteção Rate Limit
├── cliente_leds.py         # Orquestrador local na Raspberry Pi (Strobe + Globo + SG90)
├── cliente_espectro.py     # Monitor visual ASCII híbrido
├── config_hardware.json    # Configuração de pinagem e limites do servo
├── drivers_hardware.py     # Classes de hardware (LuzPWM, GloboRGB, ServoSG90)
├── test_hardware.py        # Teste de bancada (Strobe + Globo RGB + Servo SG90)
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

## 🚀 6. Como Executar

### Opção A: Com o ESP32-C3 Super Mini Sem Fio
1. Na **Raspberry Pi 3**:
   ```bash
   cd ~/rasp_audio
   python3 servidor.py
   python3 servidor_spotify.py
   ```
2. Ligue o **ESP32-C3 Super Mini** (alimentado com 5V e conectado ao ULN2003 e Servo SG90).

### Opção B: Direto na Raspberry Pi
```bash
cd ~/rasp_audio
python3 servidor.py
python3 servidor_spotify.py
python3 cliente_leds.py
```
