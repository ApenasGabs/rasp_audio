# 📝 CONTEXTO DO PROJETO: Audio to Light (Raspberry Pi Híbrido)

Este documento fornece o **contexto completo do projeto**, arquitetura de software, pinagem detalhada dos atuadores (ULN2003 e Ponte H Bidirecional), métodos de acesso remoto à Raspberry Pi 3 e guia operacional.

---

## 📌 1. Visão Geral e Objetivo

Sistema de **Show de Iluminação Rítmico Inteligente (Stage Lighting Controller)** rodando em uma **Raspberry Pi 3**. O sistema analisa o áudio ambiente em tempo real e orquestra 8 canais de atuadores físicos (Lasers, Globo RGB, Strobe e Motores Bidirecionais de Efeito com controle de avanço, recuo, oscilação e aceleração).

### O Conceito do Sistema Híbrido:
* **O Microfone é o "Reflexo"** (Sincronia em milissegundos): Captura o áudio via microfone USB, calcula FFT em Hz reais e aplica *Onset Detection* com Baseline Assimétrica para detectar batidas e pratos instantaneamente sem latência.
* **A API do Spotify é o "Cérebro"** (Contexto musical): Monitora o player do usuário via `spotipy`, identifica gênero musical, nível de energia, danceabilidade e seções estruturais.
* **O Orquestrador executa a "Matriz de Decisão"**: Modula o comportamento de cada laser, cor de LED e aceleração/sentido dos motores de acordo com a energia musical.

---

## 🔌 2. Hardware e Pinagem Completa (BCM da Raspberry Pi 3)

| Dispositivo / Carga | Atuador Físico | Pino BCM | Pino Físico | Driver de Potência | Comportamento Musical |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **⚪ Strobe Branco** | LED 12V Branco | **GPIO 17** | Pino 11 | **ULN2003** (PWM 15Hz) | Graves / Kicks / Sub-Bass |
| **🟢 Laser Verde** | Diodo Laser Verde | **GPIO 27** | Pino 13 | **ULN2003** (Digital) | Agudos / Pratos / Hi-Hats |
| **🔴 Laser Vermelho** | Diodo Laser Vermelho | **GPIO 22** | Pino 15 | **ULN2003** (Digital) | Caixas / Kicks pesados / Transientes |
| **🔴 Globo - Red (R)** | LED Vermelho do Globo | **GPIO 23** | Pino 16 | **ULN2003** (PWM) | Modulação de Médios / Atmosfera |
| **🟢 Globo - Green (G)** | LED Verde do Globo | **GPIO 24** | Pino 18 | **ULN2003** (PWM) | Modulação de Médios / Atmosfera |
| **🔵 Globo - Blue (B)** | LED Azul do Globo | **GPIO 25** | Pino 22 | **ULN2003** (PWM) | Modulação de Médios / Atmosfera |
| **⚙️ Motor Filtro Laser**| Motor DC Filtro Óptico | **IN1: GPIO 18<br>IN2: GPIO 13** | Pino 12<br>Pino 33 | **Ponte H** (Bidirecional) | **Oscilação rápida (vai e volta a 2.5Hz)** nos pratos |
| **🌐 Motor Globo** | Motor DC Globo Giratório | **IN1: GPIO 26<br>IN2: GPIO 19** | Pino 37<br>Pino 35 | **Ponte H** (Bidirecional) | **Varredura (Sweep vai-e-volta)** e inversão no drop |

*Nota: Todas as configurações de pinos estão externalizadas em `config_hardware.json`.*

---

## 🔑 3. Acesso à Raspberry Pi (Rede e Comandos)

* **Endereço IP:** `192.168.31.3`
* **Usuário:** `gabs`
* **Diretório do Projeto na RPi:** `/home/gabs/rasp_audio`
* **Autenticação SSH:** Chave SSH já configurada (sem senha via WSL).

```bash
# Acesso SSH direto
wsl bash -c "ssh gabs@192.168.31.3"

# Enviar arquivos modificados (Deploy)
wsl bash -c "scp *.py *.json gabs@192.168.31.3:~/rasp_audio/"

# Teste de bancada bidirecional dos motores e luzes
wsl bash -c "ssh gabs@192.168.31.3 'cd ~/rasp_audio && python3 test_hardware.py'"
```

---

## 🏗️ 4. Arquitetura de Software (Produtor/Consumidor UDP :5005)

```mermaid
graph TD
    subgraph O Reflexo (Tempo Real)
        MIC[Microfone USB] -->|44.1kHz / 1024 chunks| S_AUD[servidor.py]
        S_AUD -->|UDP Broadcast :5005 tipo: audio| NET((Rede UDP :5005))
    end

    subgraph O Cérebro (Contexto)
        SP[Spotify Web API] -->|Polling 3.0s + Interpolação| S_SP[servidor_spotify.py]
        S_SP -->|UDP Broadcast :5005 tipo: spotify| NET
    end

    subgraph Orquestração de Efeitos
        NET --> C_LED[cliente_leds.py - Orquestrador Multi-Canais]
        NET --> C_ESP[cliente_espectro.py - Monitor Híbrido]
        
        C_LED -->|ULN2003| GPIO_ULN[Strobe BCM 17 + Lasers BCM 27/22 + Globo RGB BCM 23/24/25]
        C_LED -->|Ponte H Bidirecional| GPIO_MOT[Filtro Laser: BCM 18/13 + Globo: BCM 26/19]
    end
```

---

## 🎛️ 5. Coreografia dos Efeitos Musicais & Dinâmica dos Motores

| Modo Spotify / Áudio | Strobe Branco (Graves) | Lasers (Verde / Vermelho) | Globo RGB (Cores) | Motores Bidirecionais (Ponte H) |
| :--- | :--- | :--- | :--- | :--- |
| **Alta Energia / Drop** | **Strobe Intenso 15Hz @ 50%** | **Flashes simultâneos** em agudos e transientes | **Cores Quentes & Rotação Eufórica** | **Globo:** 100% de velocidade com inversão repentina a cada 4 batidas fortes.<br>**Filtro Laser:** Oscilação rápida (vai e volta a 2.5Hz). |
| **Média Energia / Versos** | Strobe Rítmico @ 45% | Disparos alternados nos pratos | Transição Suave (Violeta, Azul, Dourado) | **Globo:** Varredura suave (vai e volta a 60%).<br>**Filtro Laser:** Rotação suave (35-45%). |
| **Suave / Lofi / Acústica** | Brilho pulsante suave (sem estrobo) | Desligados (evita disparos falsos) | Tons Frios Relaxantes (Azul / Ciano) | **Globo:** Movimento lento (vai e volta a 25%).<br>**Filtro Laser:** Parado (0%). |
| **Standby** | Apagado (ou reflexo se houver som físico) | Desligado | Desligado | Parados (0%) com retorno automático se houver som. |

---

## 🚀 6. Como Executar na Raspberry Pi

```bash
cd ~/rasp_audio

# Teste de Bancada inicial (opcional):
python3 test_hardware.py

# Terminal 1: Captura de Áudio (Microfone)
python3 servidor.py

# Terminal 2: Contexto Spotify
python3 servidor_spotify.py

# Terminal 3: Orquestrador dos 8 Atuadores
python3 cliente_leds.py

# Terminal 4 (Opcional): Monitor Visual
python3 cliente_espectro.py
```
