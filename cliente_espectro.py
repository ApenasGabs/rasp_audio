# -*- coding: utf-8 -*-
import time
import socket
import json

PORTA_UDP = 5005

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(socket, "SO_REUSEPORT"):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sock.bind(("", PORTA_UDP))

print("\033[2J\033[H", end="")  # Limpa a tela
print("Aguardando pacotes UDP de Áudio e Spotify...")

spotify_info = {
    "ativo": False,
    "tocando": False,
    "faixa": "Nenhuma",
    "artista": "-",
    "energia": 0.0,
    "modo_sugerido": "fallback",
    "secao_loudness": -8.0,
    "tempo_bpm": 120.0,
    "ultimo_tempo": 0.0
}

try:
    while True:
        data, addr = sock.recvfrom(2048)
        payload = json.loads(data.decode("utf-8"))
        agora = time.time()

        tipo_pacote = payload.get("tipo", "audio")

        if tipo_pacote == "spotify":
            spotify_info["ativo"] = True
            spotify_info["tocando"] = payload.get("tocando", False)
            spotify_info["faixa"] = payload.get("faixa", "Nenhuma")
            spotify_info["artista"] = payload.get("artista", "-")
            spotify_info["energia"] = payload.get("energia", 0.0)
            spotify_info["modo_sugerido"] = payload.get("modo_sugerido", "fallback")
            spotify_info["secao_loudness"] = payload.get("secao_loudness", -8.0)
            spotify_info["tempo_bpm"] = payload.get("tempo_bpm", 120.0)
            spotify_info["ultimo_tempo"] = agora
            continue

        # Verifica se o Spotify ainda está ativo
        spotify_conectado = spotify_info["ativo"] and (agora - spotify_info["ultimo_tempo"] < 4.0)

        faixas = payload.get("faixas", {})
        dados_graves = faixas.get("graves", {})
        dados_medios = faixas.get("medios", {})
        dados_agudos = faixas.get("agudos", {})
        dados_super = faixas.get("super_agudos", {})

        pico_grave = dados_graves.get("pico", False)
        pico_agudo = dados_agudos.get("pico", False)
        pico_super = dados_super.get("pico", False)
        nivel_medios = dados_medios.get("nivel", 0.3)

        # Status estimado dos atuadores
        st_strobe = "⚡ [ON]" if (pico_grave or dados_graves.get("ativo")) else "   [OFF]"
        st_laser_g = "🟢 [ON]" if (pico_agudo or dados_agudos.get("ativo")) else "   [OFF]"
        st_laser_r = "🔴 [ON]" if ((pico_grave and dados_graves.get("nivel", 0) > 0.7) or pico_super) else "   [OFF]"

        modo_nome = spotify_info["modo_sugerido"] if spotify_conectado else "fallback"
        vel_mot = 100 if modo_nome == "alta_energia" else (20 if modo_nome == "suave" else 55)

        print("\033[H", end="")  # Volta ao topo do terminal
        print("====================== SISTEMA HÍBRIDO DE ILUMINAÇÃO ======================")

        if spotify_conectado:
            status_spotify = "▶ TOCANDO" if spotify_info["tocando"] else "⏸ PAUSADO"
            barra_energia = "█" * int(spotify_info["energia"] * 18)
            print(f" Spotify: [{status_spotify}] {spotify_info['faixa']} - {spotify_info['artista']}")
            print(
                f" Energia: [{barra_energia:<18}] ({int(spotify_info['energia'] * 100):3d}%) | "
                f"Modo: {modo_nome.upper():<14} | BPM: {spotify_info['tempo_bpm']:5.1f}"
            )
        else:
            print(" Spotify: [OFFLINE / FALLBACK] (Operando apenas via Microfone)")
            print(" Modo:    PADRÃO RÍTMICO DINÂMICO")

        print("----------------------------------------------------------------------------")
        print(f" Atuadores: Strobe: {st_strobe} | Laser Vd: {st_laser_g} | Laser Vm: {st_laser_r} | Motores: {vel_mot}%")
        print("----------------------------------------------------------------------------")
        print("Faixa            | Nível e Espectro                             | Status   | Valor / Limiar")
        print("----------------------------------------------------------------------------")

        for nome, info in faixas.items():
            nivel = float(info.get("nivel", 0.0))
            pico = bool(info.get("pico", False))
            ativo = bool(info.get("ativo", False))
            valor = int(info.get("valor", 0))
            limiar = int(info.get("limiar", 0))

            tamanho_barra = int(nivel * 30)
            barra = "█" * tamanho_barra
            if pico:
                status = "💥 [PICO!]"
            elif ativo:
                status = "⚡ [ATIVO]"
            else:
                status = "   [     ]"

            nome_formatado = f"{nome.replace('_', ' ').title():<16}"
            print(
                f"{nome_formatado} | {barra:<30} ({int(nivel * 100):3d}%) | {status} | {valor:7d} / {limiar:7d}"
            )

        print("----------------------------------------------------------------------------")
        print("(Pressione Ctrl+C para sair)")

except KeyboardInterrupt:
    print("\nSaindo do monitor de espectro...")
