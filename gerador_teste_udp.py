# -*- coding: utf-8 -*-
"""
=============================================================================
 GERADOR DE TESTE CONTÍNUO UDP (Para Raspberry Pi ou PC)
=============================================================================
Envia continuamente um fluxo ininterrupto de dados de áudio e Spotify simulados
via UDP broadcast na porta 5005. Perfeito para calibrar e testar o ESP32-C3
sem precisar do microfone ou de música tocando.
=============================================================================
"""
import socket
import json
import time
import math
import sys

PORTA_UDP = 5005

# Cria socket UDP com permissão de broadcast
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

# Se o usuário passou um IP de destino no terminal (ex: python gerador_teste_udp.py 192.168.31.150)
ip_alvo = sys.argv[1] if len(sys.argv) > 1 else "<broadcast>"

print("=" * 65)
print(" 🚀 GERADOR CONTÍNUO DE TESTE UDP (Porta: 5005)")
print(f" -> Destino: {ip_alvo}")
print(" -> Pressione Ctrl+C para encerrar")
print("=" * 65)

contador = 0
inicio = time.time()

try:
    while True:
        agora = time.time()
        tempo_s = agora - inicio
        contador += 1

        # 1. Simulação de Batidas de Graves (Kick a cada ~500ms = 120 BPM)
        fase_batida = (tempo_s * 2.0) % 1.0  # 120 BPM
        eh_pico_grave = (fase_batida < 0.15)
        nivel_grave = 0.95 if eh_pico_grave else 0.15

        # 2. Simulação de Pratos / Agudos (Hi-Hats a cada ~250ms)
        fase_agudo = (tempo_s * 4.0) % 1.0
        eh_pico_agudo = (fase_agudo < 0.10)
        nivel_agudo = 0.80 if eh_pico_agudo else 0.10

        # 3. Simulação de Médios (Onda senoidal para modulação de cores do Globo)
        nivel_medios = (math.sin(tempo_s * 1.5) + 1.0) / 2.0

        # 4. Modos cíclicos a cada 10 segundos: Alta Energia -> Média Energia -> Suave
        ciclo_modo = int(tempo_s / 10.0) % 3
        if ciclo_modo == 0:
            modo_atual = "alta_energia"
            energia_spotify = 0.85
        elif ciclo_modo == 1:
            modo_atual = "media_energia"
            energia_spotify = 0.60
        else:
            modo_atual = "suave"
            energia_spotify = 0.30

        # A cada 500ms (10 ciclos de 50ms), envia pacote Spotify
        if contador % 10 == 0:
            payload_spotify = {
                "tipo": "spotify",
                "tocando": True,
                "faixa": "Faixa Teste Sincronizada",
                "artista": "Stage Engine",
                "energia": energia_spotify,
                "danceabilidade": 0.75,
                "modo_sugerido": modo_atual,
                "tempo_bpm": 120.0
            }
            msg_sp = json.dumps(payload_spotify).encode("utf-8")
            sock.sendto(msg_sp, (ip_alvo, PORTA_UDP))
            if ip_alvo == "<broadcast>":
                sock.sendto(msg_sp, ("255.255.255.255", PORTA_UDP))

        # Payload principal de Áudio (20 vezes por segundo = 50ms)
        payload_audio = {
            "tipo": "audio",
            "faixas": {
                "sub_graves": {"nivel": nivel_grave, "pico": eh_pico_grave, "ativo": eh_pico_grave},
                "graves": {"nivel": nivel_grave, "pico": eh_pico_grave, "ativo": eh_pico_grave},
                "medios": {"nivel": nivel_medios, "pico": False, "ativo": True},
                "agudos": {"nivel": nivel_agudo, "pico": eh_pico_agudo, "ativo": eh_pico_agudo},
                "super_agudos": {"nivel": nivel_agudo, "pico": False, "ativo": False}
            }
        }

        msg_audio = json.dumps(payload_audio).encode("utf-8")
        sock.sendto(msg_audio, (ip_alvo, PORTA_UDP))
        if ip_alvo == "<broadcast>":
            sock.sendto(msg_audio, ("255.255.255.255", PORTA_UDP))

        # Log visual no terminal a cada 100ms
        if contador % 2 == 0:
            strobe_icon = "💥 [KICK!]" if eh_pico_grave else "   [    ]"
            angulo_simulado = int(30 + ((math.sin(tempo_s * 2.0) + 1.0) / 2.0) * 120)
            print(
                f"\r[UDP #{contador:05d}] Modo: {modo_atual.upper():<12} | Strobe: {strobe_icon} | "
                f"Médios: {int(nivel_medios*100):2d}% | Servo: {angulo_simulado:3d}° ",
                end="",
                flush=True
            )

        time.sleep(0.05)

except KeyboardInterrupt:
    print("\n\nEncerrando gerador de teste UDP...")
