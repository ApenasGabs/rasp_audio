# 📝 CONTEXTO DO PROJETO: Audio to Light (Raspberry Pi + ESP32-C3 + DMX512 + Web Server)

Este documento fornece o **contexto completo do projeto**, arquitetura de software, pinagem detalhada dos atuadores (ULN2003, Servo Motor SG90 e Projetor Laser DMX512 via RS-485), firmware com **Web Server embutido para calibração em tempo real pelo navegador**, métodos de acesso remoto e guia operacional.

---

## 📌 1. Visão Geral e Objetivo

Sistema de **Show de Iluminação Rítmico Inteligente (Stage Lighting Controller)** operando de forma híbrida e modular:
1. **Central de Processamento (Raspberry Pi 3 ou PC):**
   - **O Reflexo:** Microfone USB com FFT e Onset Detection com Baseline Assimétrica em tempo real (`servidor.py`).
   - **O Cérebro:** Integração Spotify Web API via `spotipy` com inferência de gênero, energia e seções estruturais (`servidor_spotify.py`).
   - **Transmissão:** Broadcast UDP de pacotes multiplexados na porta `5005` via rede Wi-Fi.
   - **Gerador de Teste Contínuo:** `gerador_teste_udp.py` para testes sem fio sem precisar de som.
2. **Nó de Atuadores Sem Fio com Web Server (ESP32-C3 Super Mini):**
   - **⚪ Strobe Branco (12V):** Pisca em frequência estroboscópica de 15Hz nas batidas de grave / kick.
   - **🌈 Globo RGB (Cores):** Modulação de paletas contextuais (cores frias no calmo, quentes no drop/refrão).
   - **🤖 Servo Motor SG90 (Movimento do Globo):** Varredura angular rítmica (0° a 180°) e saltos instantâneos de ângulo nas batidas fortes (*Drop Jumps*).
   - **🎛️ Projetor Laser Profissional DMX512 (RS-485):** Controle de 16 canais DMX em tempo real.
   - **🌐 Web Server de Calibração HTTP (Porta 80):** Painel Web interativo acessível pelo navegador (PC ou celular) com sliders para testar canais DMX e calibrar tamanhos, cores, padrões e sensibilidade sem precisar regravar o código!

---

## 🌐 2. Como Usar o Painel Web de Calibração no Navegador

1. Conecte o ESP32-C3 no Wi-Fi.
2. Abra o navegador no seu celular ou PC e acesse o IP do ESP32:
   ```
   http://192.168.31.62
   ```
3. **No Painel Web você tem:**
   - **Modo Manual:** Sliders em tempo real para os 16 canais DMX (Modo, Cor, Padrão, Tamanho, Rotação, Onda X) + Botões de atalho (Círculo, Zigzag, Túnel, Onda, Blackout).
   - **Modo Automático:** Ajuste fino em tempo real do *Tamanho Mínimo (Repouso)*, *Tamanho Máximo (Pico)*, *Sensibilidade Vocal* e *Velocidade de Rotação*.
