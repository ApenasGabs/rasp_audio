#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
==============================================================================
COLETOR E ANALISADOR DE LOGS UDP EM TEMPO REAL - Audio to Light
==============================================================================
Este script escuta os pacotes UDP transmitidos pela Raspberry Pi na porta 5005,
exibe um painel de telemetria visual no terminal e grava os dados em JSON para
análise do perfil sonoro das músicas que você ouve.
==============================================================================
"""

import socket
import json
import time
import os
import sys
from datetime import datetime

UDP_PORT = 5005
BUFFER_SIZE = 4096

def criar_barra(valor, max_chars=20, char="█"):
    """Gera uma barra visual de intensidade no terminal."""
    valor = max(0.0, min(1.0, float(valor)))
    preenchido = int(round(valor * max_chars))
    vazio = max_chars - preenchido
    return f"[{char * preenchido}{'░' * vazio}] {valor:4.2f}"

def main():
    print("=" * 70)
    print(" 📡 COLETOR DE LOGS E TELEMETRIA UDP EM TEMPO REAL")
    print("=" * 70)
    print(f"[*] Abrindo socket UDP na porta {UDP_PORT}...")

    # Configuração do socket UDP para escutar broadcast
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("", UDP_PORT))
    except Exception as e:
        print(f"[ERRO] Não foi possível vincular a porta {UDP_PORT}: {e}")
        print("[DICA] Verifique se outro programa já está escutando na porta 5005.")
        sys.exit(1)

    print(f"[OK] Escutando pacotes UDP na porta {UDP_PORT}...")
    print("[*] Pressione Ctrl+C a qualquer momento para finalizar e ver o resumo.")
    print("=" * 70)

    # Inicialização de variáveis de estatística
    inicio_sessao = time.time()
    pacotes_audio = 0
    pacotes_spotify = 0
    picos_graves_total = 0
    soma_graves = 0.0
    soma_medios = 0.0
    soma_agudos = 0.0
    max_grave = 0.0
    max_medios = 0.0

    spotify_atual = {
        "musica": "Aguardando...",
        "artista": "Aguardando...",
        "modo": "fallback",
        "energia": 0.0,
        "tocando": False
    }

    logs_coletados = []
    nome_arquivo_log = f"log_sessao_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"

    ultimo_print = 0

    try:
        while True:
            data, addr = sock.recvfrom(BUFFER_SIZE)
            agora = time.time()

            try:
                msg = json.loads(data.decode("utf-8", errors="ignore"))
            except Exception:
                continue

            tipo = msg.get("tipo", "audio")

            if tipo == "spotify":
                pacotes_spotify += 1
                spotify_atual["musica"] = msg.get("faixa", msg.get("musica", "Desconhecida"))
                spotify_atual["artista"] = msg.get("artista", "Desconhecido")
                spotify_atual["modo"] = msg.get("modo_sugerido", "media_energia")
                spotify_atual["energia"] = msg.get("energia", 0.6)
                spotify_atual["tocando"] = msg.get("tocando", True)

            elif tipo == "audio":
                pacotes_audio += 1
                faixas = msg.get("faixas", {})
                graves = faixas.get("graves", {})
                medios_graves = faixas.get("medios_graves", {})
                medios = faixas.get("medios", {})
                agudos = faixas.get("agudos", {})

                n_grave = float(graves.get("nivel", 0.0))
                p_grave = bool(graves.get("pico", False))
                n_med_grav = float(medios_graves.get("nivel", 0.0))
                n_med = float(medios.get("nivel", 0.0))
                n_agud = float(agudos.get("nivel", 0.0))
                p_agud = bool(agudos.get("pico", False))

                # Extração do sinal vocal
                n_vocal = (n_med * 0.70) + (n_med_grav * 0.20) + (n_agud * 0.10)

                # Estatísticas
                if p_grave:
                    picos_graves_total += 1
                soma_graves += n_grave
                soma_medios += n_med
                soma_agudos += n_agud
                if n_grave > max_grave: max_grave = n_grave
                if n_med > max_medios: max_medios = n_med

                # Gravação de amostra a cada ~100ms
                if len(logs_coletados) == 0 or (agora - logs_coletados[-1]["t"] >= 0.1):
                    logs_coletados.append({
                        "t": round(agora - inicio_sessao, 2),
                        "grave": round(n_grave, 3),
                        "pico_grave": p_grave,
                        "vocal": round(n_vocal, 3),
                        "medios": round(n_med, 3),
                        "agudos": round(n_agud, 3),
                        "spotify_modo": spotify_atual["modo"],
                        "spotify_energia": spotify_atual["energia"]
                    })

                # Atualização do Display a cada 60ms (~16 fps)
                if agora - ultimo_print >= 0.06:
                    ultimo_print = agora
                    
                    status_kick = "🔥 [KICK / DROP!]" if p_grave else "                "
                    status_voz = "🎤 [VOCAL FORTE!]" if n_vocal > 0.50 else "                "
                    
                    barra_g = criar_barra(n_grave, 18, "█")
                    barra_v = criar_barra(n_vocal, 18, "█")
                    barra_m = criar_barra(n_med, 18, "█")
                    barra_a = criar_barra(n_agud, 18, "█")

                    # Limpa e redesenha painel
                    sys.stdout.write("[H[J") # Limpa tela ANSI
                    print("=" * 70)
                    print(f" 🎵 TELEMETRIA EM TEMPO REAL | {datetime.now().strftime('%H:%M:%S')}")
                    print("=" * 70)
                    print(f" 🎶 Spotify : {spotify_atual['artista']} - {spotify_atual['musica']}")
                    print(f" ⚡ Modo    : {spotify_atual['modo'].upper()} (Energia Spotify: {spotify_atual['energia']:.2f})")
                    print("-" * 70)
                    print(f" 🔴 GRAVES (Kick/Bumbo) : {barra_g}  {status_kick}")
                    print(f" 🟣 VOCAL (Médios+Harm) : {barra_v}  {status_voz}")
                    print(f" 🟡 MÉDIOS (Sintetiz)   : {barra_m}")
                    print(f" 🔵 AGUDOS (Hi-Hats)    : {barra_a}")
                    print("-" * 70)
                    bpm_est = (picos_graves_total / max(0.1, (agora - inicio_sessao))) * 60
                    print(f" 📊 Pacotes: {pacotes_audio} | Kicks Detectados: {picos_graves_total} | BPM Aprox: {bpm_est:.0f}")
                    print("=" * 70)
                    print(" [DICA] Deixe a música tocando para coletar dados reais do seu estilo!")
                    sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n\n" + "=" * 70)
        print(" 📋 RESUMO DA SESSÃO DE COLETA MUSICAL")
        print("=" * 70)
        duracao = time.time() - inicio_sessao
        print(f" ⏱️ Duração da Sessão   : {duracao:.1f} segundos")
        print(f" 📦 Pacotes de Áudio    : {pacotes_audio} pacotes")
        print(f" 🎶 Pacotes Spotify     : {pacotes_spotify} pacotes")
        print(f" 🥁 Kicks / Picos Grav. : {picos_graves_total} batidas")
        
        if pacotes_audio > 0:
            media_g = soma_graves / pacotes_audio
            media_m = soma_medios / pacotes_audio
            media_a = soma_agudos / pacotes_audio
            print(f" 🔊 Média de Graves     : {media_g:.2f} (Pico Máximo: {max_grave:.2f})")
            print(f" 🎤 Média de Médios/Voz : {media_m:.2f} (Pico Máximo: {max_medios:.2f})")
            print(f" ✨ Média de Agudos     : {media_a:.2f}")

        # Salva o arquivo JSON
        caminho_log = os.path.join(os.getcwd(), nome_arquivo_log)
        with open(caminho_log, "w", encoding="utf-8") as f:
            json.dump({
                "resumo": {
                    "duracao_s": round(duracao, 2),
                    "total_pacotes": pacotes_audio,
                    "kicks_detectados": picos_graves_total,
                    "media_graves": round(soma_graves / max(1, pacotes_audio), 3),
                    "media_medios": round(soma_medios / max(1, pacotes_audio), 3),
                    "max_grave": round(max_grave, 3),
                    "max_medios": round(max_medios, 3)
                },
                "amostras": logs_coletados
            }, f, indent=2)

        print(f"\n[OK] Dados gravados com sucesso em: {nome_arquivo_log}")
        print("=" * 70)

if __name__ == "__main__":
    main()
