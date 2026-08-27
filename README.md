# 📝 CONTEXTO DO PROJETO: Audio to Light (Raspberry Pi + ESP32-C3)

Este documento fornece o **contexto completo do projeto**, arquitetura de software, pinagem detalhada dos atuadores (ULN2003 e Servo Motor SG90), firmware para o microcontrolador sem fio **ESP32-C3 Super Mini**, métodos de acesso remoto e guia operacional.

---

## 📌 1. Visão Geral e Objetivo

Sistema de **Show de Iluminação Rítmico Inteligente (Stage Lighting Controller)** operando de forma híbrida e modular:
1. **Central de Processamento (Raspberry Pi 3 ou PC):**
   - **O Reflexo:** Microfone USB com FFT e Onset Detection com Baseline Assimétrica em tempo real (`servidor.py`).
   - **O Cérebro:** Integração Spotify Web API via `spotipy` com inferência de gênero, energia e seções estruturais (`servidor_spotify.py`).
   - **Transmissão:** Broadcast UDP de pacotes multiplexados na porta `5005` via rede Wi-Fi.
   - **Gerador de Teste Contínuo:** `gerador_teste_udp.py` para testes sem fio sem precisar de som.
2. **Nó de Atuadores (Raspberry Pi GPIO ou ESP32-C3 Super Mini Sem Fio):**
   - **⚪ Strobe Branco (12V):** Pisca em frequência estroboscópica de 15Hz nas batidas de grave / kick.
   - **🌈 Globo RGB (Cores):** Modulação de paletas contextuais (cores frias no calmo, quentes no drop/refrão).
   - **🤖 Servo Motor SG90 (Movimento do Globo):** Varredura angular rítmica (0° a 180°) e saltos instantâneos de ângulo nas batidas fortes (*Drop Jumps*).

---

## 🌐 2. Diagrama da Arquitetura do Sistema

```mermaid
graph TD
    subgraph Central (Raspberry Pi 3 ou PC)
        MIC[Microfone USB] --> S_AUD[servidor.py]
        SP[Spotify Web API] --> S_SP[servidor_spotify.py]
        TEST[Gerador de Teste] --> G_UDP[gerador_teste_udp.py]
        S_AUD -->|UDP Broadcast :5005 Wi-Fi| NET((Rede Wi-Fi / UDP))
        S_SP -->|UDP Broadcast :5005 Wi-Fi| NET
        G_UDP -->|UDP Broadcast :5005 Wi-Fi| NET
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

## 📂 4. Estrutura dos Arquivos do Repositório

```
rasp_audio/
├── servidor.py             # Reflexo: Microfone + FFT + Detecção Rítmica
├── servidor_spotify.py     # Cérebro: Contexto Spotify + Proteção Rate Limit
├── gerador_teste_udp.py    # Gerador contínuo de teste UDP para calibrar o ESP32-C3
├── cliente_leds.py         # Orquestrador local na Raspberry Pi (Strobe + Globo + SG90)
├── cliente_espectro.py     # Monitor visual ASCII híbrido
├── config_hardware.json    # Configuração de pinagem e limites do servo
├── drivers_hardware.py     # Classes de hardware (LuzPWM, GloboRGB, ServoSG90)
├── test_hardware.py        # Teste de bancada (Strobe + Globo RGB + Servo SG90)
├── esp32_c3_node/          # Nó Receptor Sem Fio para ESP32-C3 Super Mini
│   ├── esp32_c3_node.ino   # Firmware Arduino / C++ com auto-teste e LEDC universal
│   ├── platformio.ini      # Configuração para compilação via PlatformIO
│   └── README.md           # Guia de gravação e ligação do ESP32-C3
├── CONTEXTO.md             # Guia completo para desenvolvedores e IAs
├── README.md               # Documentação principal
├── requirements.txt        # Dependências Python
└── .gitignore              # Proteção de credenciais (.env e .cache_spotify)
```

---

## 🚀 5. Como Testar e Operar o ESP32-C3 Sem Fio

### Passo 1: Testar a transmissão com o Gerador Contínuo
1. Na **Raspberry Pi** (ou no seu PC):
   ```bash
   cd ~/rasp_audio
   python3 gerador_teste_udp.py
   ```
   *(Ou se quiser enviar direto para o IP do ESP32: `python3 gerador_teste_udp.py 192.168.1.150`)*

2. O ESP32-C3 receberá o fluxo simulado de 120 BPM e começará a mover o Servo SG90 e piscar o Strobe e as cores do Globo imediatamente!

### Passo 2: Executar o Show Real com Microfone + Spotify
```bash
# Terminal 1: Captura de Áudio (Microfone USB)
python3 servidor.py

# Terminal 2: Contexto Spotify
python3 servidor_spotify.py
```
