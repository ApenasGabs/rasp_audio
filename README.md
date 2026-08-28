# 📝 CONTEXTO DO PROJETO: Audio to Light (Raspberry Pi + ESP32-C3 + DMX512)

Este documento fornece o **contexto completo do projeto**, arquitetura de software, pinagem detalhada dos atuadores (ULN2003, Servo Motor SG90 e Projetor Laser DMX512 via RS-485), firmware para o microcontrolador sem fio **ESP32-C3 Super Mini**, métodos de acesso remoto e guia operacional.

---

## 📌 1. Visão Geral e Objetivo

Sistema de **Show de Iluminação Rítmico Inteligente (Stage Lighting Controller)** operando de forma híbrida e modular:
1. **Central de Processamento (Raspberry Pi 3 ou PC):**
   - **O Reflexo:** Microfone USB com FFT e Onset Detection com Baseline Assimétrica em tempo real (`servidor.py`).
   - **O Cérebro:** Integração Spotify Web API via `spotipy` com inferência de gênero, energia e seções estruturais (`servidor_spotify.py`).
   - **Transmissão:** Broadcast UDP de pacotes multiplexados na porta `5005` via rede Wi-Fi.
   - **Gerador de Teste Contínuo:** `gerador_teste_udp.py` para testes sem fio sem precisar de som.
2. **Nó de Atuadores Sem Fio (ESP32-C3 Super Mini):**
   - **⚪ Strobe Branco (12V):** Pisca em frequência estroboscópica de 15Hz nas batidas de grave / kick.
   - **🌈 Globo RGB (Cores):** Modulação de paletas contextuais (cores frias no calmo, quentes no drop/refrão).
   - **🤖 Servo Motor SG90 (Movimento do Globo):** Varredura angular rítmica (0° a 180°) e saltos instantâneos de ângulo nas batidas fortes (*Drop Jumps*).
   - **🎛️ Projetor Laser Profissional DMX512 (RS-485):** Controle de 16 canais DMX em tempo real (Gobos, cores, túneis, zoom expansivo, rotação 3D e efeitos de onda).

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

    subgraph Nó Sem Fio (ESP32-C3 Super Mini)
        NET --> ESP[esp32_c3_node.ino]
        ESP -->|ULN2003| STROBE[Strobe 12V GPIO 0 + Globo RGB GPIO 4/5/6]
        ESP -->|Sinal PWM| SERVO[Servo SG90 GPIO 7]
        ESP -->|MAX485 UART| DMX[Projetor Laser DMX512 GPIO 21]
    end
```

---

## 🔌 3. Tabela de Pinagem do ESP32-C3 Super Mini

| Dispositivo / Protocolo | Atuador Físico | Pino ESP32-C3 | Driver | Tipo de Sinal |
| :--- | :--- | :--- | :--- | :--- |
| **⚪ Strobe Branco** | LED 12V Branco | **GPIO 0** | **ULN2003** | PWM 15Hz (Strobe no Bumbo) |
| **🔴 Globo - Red (R)** | LED Vermelho Globo | **GPIO 4** | **ULN2003** | PWM (Modulação de Cores) |
| **🟢 Globo - Green (G)** | LED Verde Globo | **GPIO 5** | **ULN2003** | PWM (Modulação de Cores) |
| **🔵 Globo - Blue (B)** | LED Azul Globo | **GPIO 6** | **ULN2003** | PWM (Modulação de Cores) |
| **🤖 Servo SG90 (Globo)** | Servo Motor Angular | **GPIO 7** | **Direto (Sinal PWM)** | **PWM 50Hz (0° a 180°)** |
| **🎛️ DMX512 TX** | Projetor Laser (Dados) | **GPIO 21** | **Módulo RS-485 (DI)** | **UART 250 kbps (DMX512)** |
| **🎛️ DMX512 Enable** | Habilita Transmissão | **GPIO 10** | **Módulo RS-485 (DE+RE)**| **Digital HIGH** |

---

## 🎛️ 4. Mapeamento dos 16 Canais DMX do Projetor Laser

| Canal | Função | Comportamento Dinâmico |
| :--- | :--- | :--- |
| **CH1** | Modo de Operação | `0` (Blackout seguro) / `50` (Manual Console ao tocar) |
| **CH2** | Velocidade | `128` |
| **CH3** | Seleção de Cor | Cores frias no calmo $ightarrow$ Multicolorido no drop |
| **CH4** | Velocidade de Fluxo | Acompanha a energia da música |
| **CH5** | Padrão Gráfico (Gobo) | **Troca de figura/túnel a cada batida forte de grave (`pico_grave`)** |
| **CH6** | Tamanho do Padrão | **Zoom expansivo no drop/refrão** |
| **CH7** | Dimensionamento | Zoom dinâmico contínuo |
| **CH8** | Rotação Central | Rotação 3D acelerada em alta energia |
| **CH9/10**| Flip H / V | Inversão espacial nos ataques |
| **CH11/12**| Posição X / Y | Varredura de feixes no ar |
| **CH13** | Ondulação X | Onda senoidal acompanhando médios e sintetizadores |
| **CH14** | Traçado Gradual | Efeito de desenho de feixes no ar |
| **CH15** | Scan Speed | `255` (Máxima nitidez dos galvanômetros) |
| **CH16** | Modo de Exibição | Destaques luminosos no drop |

---

## 🚀 5. Como Operar o Sistema

1. **Grave o firmware no ESP32-C3 Super Mini** com o cabo XLR do laser conectado no módulo RS-485.
2. Inicie o gerador de teste (ou o microfone + Spotify) na Raspberry Pi:
   ```bash
   cd ~/rasp_audio
   python3 gerador_teste_udp.py 192.168.31.62
   ```
