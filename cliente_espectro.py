import time
import socket
import json

PORTA_UDP = 5005

# Configura o Socket UDP para ouvir
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

        print("\033[H", end="")  # Volta ao topo do terminal
        print("===================== MONITOR HÍBRIDO (SPOTIFY + ESPECTRO) =====================")

        if spotify_conectado:
            status_spotify = "▶ TOCANDO" if spotify_info["tocando"] else "⏸ PAUSADO"
            barra_energia = "█" * int(spotify_info["energia"] * 20)
            print(f" Spotify: [{status_spotify}] {spotify_info['faixa']} - {spotify_info['artista']}")
            print(
                f" Energia: [{barra_energia:<20}] ({int(spotify_info['energia'] * 100):3d}%) | "
                f"Modo: {spotify_info['modo_sugerido'].upper():<14} | BPM: {spotify_info['tempo_bpm']:5.1f}"
            )
        else:
            print(" Spotify: [OFFLINE / FALLBACK] (Operando apenas via Microfone)")
            print(" Modo:    PADRÃO RÍTMICO")

        print("--------------------------------------------------------------------------------")
        print("Faixa            | Nível e Espectro                             | Status   | Valor / Limiar")
        print("--------------------------------------------------------------------------------")

        if "faixas" in payload:
            faixas = payload["faixas"]
            for nome, info in faixas.items():
                nivel = float(info.get("nivel", 0.0))
                pico = bool(info.get("pico", False))
                ativo = bool(info.get("ativo", False))
                valor = int(info.get("valor", 0))
                limiar = int(info.get("limiar", 0))

                tamanho_barra = int(nivel * 35)
                barra = "█" * tamanho_barra
                if pico:
                    status = "💥 [PICO!]"
                elif ativo:
                    status = "⚡ [ATIVO]"
                else:
                    status = "   [     ]"

                nome_formatado = f"{nome.replace('_', ' ').title():<16}"
                print(
                    f"{nome_formatado} | {barra:<35} ({int(nivel * 100):3d}%) | {status} | {valor:7d} / {limiar:7d}"
                )
        else:
            # Modo legado
            for nome, valor in payload.items():
                if isinstance(valor, (int, float)):
                    tamanho = min(35, int(valor / 20000))
                    barra = "█" * tamanho
                    nome_formatado = f"{nome.title():<16}"
                    print(f"{nome_formatado} | {barra:<35}        | [LEGADO] | {int(valor):7d}")

        print("--------------------------------------------------------------------------------")
        print("(Pressione Ctrl+C para sair)")

except KeyboardInterrupt:
    print("\nSaindo do monitor de espectro...")



