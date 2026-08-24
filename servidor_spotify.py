# -*- coding: utf-8 -*-
import os
import time
import socket
import json
import threading
import logging

# Silencia logs verbosos de bibliotecas HTTP
logging.getLogger("spotipy").setLevel(logging.CRITICAL)
logging.getLogger("spotipy.client").setLevel(logging.CRITICAL)
logging.getLogger("urllib3").setLevel(logging.CRITICAL)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

try:
    from dotenv import load_dotenv
    load_dotenv(os.path.join(BASE_DIR, ".env"))
except ImportError:
    pass

import spotipy
from spotipy.oauth2 import SpotifyOAuth

# Configuração da Rede UDP
PORTA_UDP = int(os.getenv("PORTA_UDP", 5005))
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

# Validação das credenciais do Spotify
CLIENT_ID = os.getenv("SPOTIPY_CLIENT_ID")
CLIENT_SECRET = os.getenv("SPOTIPY_CLIENT_SECRET")
REDIRECT_URI = os.getenv("SPOTIPY_REDIRECT_URI", "http://127.0.0.1:8888/callback")

if not CLIENT_ID or not CLIENT_SECRET or "seu_client_id" in CLIENT_ID:
    print("=" * 65)
    print(" [AVISO] Credenciais do Spotify não configuradas!")
    print(f" Por favor, configure o arquivo: {os.path.join(BASE_DIR, '.env')}")
    print("=" * 65)
    exit(1)

ESCOPO = "user-read-playback-state user-read-currently-playing"

auth_manager = SpotifyOAuth(
    client_id=CLIENT_ID,
    client_secret=CLIENT_SECRET,
    redirect_uri=REDIRECT_URI,
    scope=ESCOPO,
    open_browser=False,
    cache_path=os.path.join(BASE_DIR, ".cache_spotify")
)
sp = spotipy.Spotify(auth_manager=auth_manager)

# Estado global compartilhado
estado = {
    "tocando": False,
    "faixa": "",
    "artista": "",
    "duracao_ms": 0,
    "progresso_base_ms": 0,
    "timestamp_base": time.time(),
    "energia": 0.6,
    "danceabilidade": 0.6,
    "tempo_bpm": 120.0,
    "secoes": [],
    "lock": threading.Lock()
}

# Cache de análise por artista
cache_artistas = {}
cache_faixas = {}

GENEROS_ALTA_ENERGIA = {
    "rock", "metal", "electronic", "techno", "house", "edm", "trap", "funk",
    "punk", "dance", "dubstep", "drum and bass", "dnb", "hardcore", "hip hop",
    "rap", "pop", "phonk", "pagode", "sertanejo", "eletro", "club"
}

GENEROS_SUAVES = {
    "acoustic", "lo-fi", "lofi", "ambient", "classical", "folk", "chill",
    "sleep", "piano", "meditation", "bossa nova", "jazz", "indie folk", "soul"
}


def estimar_perfil_por_genero(artist_id):
    """Estima a energia musical a partir dos gêneros do artista."""
    if not artist_id:
        return 0.60

    if artist_id in cache_artistas:
        return cache_artistas[artist_id]

    try:
        artista_info = sp.artist(artist_id)
        generos = [g.lower() for g in artista_info.get("genres", [])]
        texto_generos = " ".join(generos)

        for g in GENEROS_SUAVES:
            if g in texto_generos:
                cache_artistas[artist_id] = 0.30
                return 0.30

        for g in GENEROS_ALTA_ENERGIA:
            if g in texto_generos:
                cache_artistas[artist_id] = 0.85
                return 0.85

        cache_artistas[artist_id] = 0.60
        return 0.60
    except Exception:
        return 0.60


def gerar_secoes_estruturais(duracao_ms, tempo_bpm=120.0):
    """Gera mapa estrutural clássico de seções musicais (Intro, Versos, Refrão, Outro)."""
    duracao_s = max(30.0, duracao_ms / 1000.0)
    return [
        {"inicio_s": 0.0, "duracao_s": duracao_s * 0.12, "loudness": -10.0, "tempo": tempo_bpm, "nome": "Intro"},
        {"inicio_s": duracao_s * 0.12, "duracao_s": duracao_s * 0.23, "loudness": -7.5, "tempo": tempo_bpm, "nome": "Verso 1"},
        {"inicio_s": duracao_s * 0.35, "duracao_s": duracao_s * 0.25, "loudness": -4.0, "tempo": tempo_bpm, "nome": "Refrão 1 (Drop)"},
        {"inicio_s": duracao_s * 0.60, "duracao_s": duracao_s * 0.15, "loudness": -7.0, "tempo": tempo_bpm, "nome": "Verso 2"},
        {"inicio_s": duracao_s * 0.75, "duracao_s": duracao_s * 0.17, "loudness": -3.5, "tempo": tempo_bpm, "nome": "Refrão Final"},
        {"inicio_s": duracao_s * 0.92, "duracao_s": duracao_s * 0.08, "loudness": -11.0, "tempo": tempo_bpm, "nome": "Outro"},
    ]


def buscar_contexto_musica(track_id, artist_id=None, duracao_ms=180000):
    """Retorna contexto musical estruturado sem chamadas extras a endpoints bloqueados."""
    if not track_id:
        return {"energia": 0.6, "danceabilidade": 0.6, "tempo_bpm": 120.0, "secoes": []}

    if track_id in cache_faixas:
        return cache_faixas[track_id]

    energia = estimar_perfil_por_genero(artist_id)
    secoes = gerar_secoes_estruturais(duracao_ms, 120.0)

    dados = {
        "energia": energia,
        "danceabilidade": 0.65,
        "tempo_bpm": 120.0,
        "secoes": secoes
    }
    cache_faixas[track_id] = dados
    return dados


def thread_polling_spotify():
    """Consulta o playback do Spotify a cada ~3.0s (protegendo contra Rate Limit)."""
    print("[Spotify] Thread de monitoramento iniciada (Intervalo: 3.0s).")
    ultima_faixa_log = ""
    aviso_rate_limit = False

    while True:
        try:
            playback = None
            try:
                playback = sp.current_playback()
            except Exception as epb:
                # Se falhar no current_playback, tenta currently_playing
                if "rate" in str(epb).lower() or "429" in str(epb):
                    if not aviso_rate_limit:
                        print("[Spotify] ⚠️ Rate Limit atingido temporariamente. O sistema operará em Modo Fallback Dinâmico.")
                        aviso_rate_limit = True
                else:
                    playback = sp.currently_playing()

            agora = time.time()

            if playback and playback.get("is_playing") and playback.get("item"):
                aviso_rate_limit = False
                item = playback["item"]
                track_id = item.get("id")
                track_name = item.get("name", "Desconhecido")
                artist_name = item["artists"][0]["name"] if item.get("artists") else "Desconhecido"
                artist_id = item["artists"][0].get("id") if item.get("artists") else None
                progress_ms = playback.get("progress_ms", 0)
                duration_ms = item.get("duration_ms", 0)

                contexto = buscar_contexto_musica(track_id, artist_id, duration_ms)

                faixa_id_str = f"{track_name} - {artist_name}"
                if faixa_id_str != ultima_faixa_log:
                    print(f"[Spotify] ▶ Tocando agora: {faixa_id_str} (Energia: {int(contexto['energia']*100)}%)")
                    ultima_faixa_log = faixa_id_str

                with estado["lock"]:
                    estado["tocando"] = True
                    estado["faixa"] = track_name
                    estado["artista"] = artist_name
                    estado["duracao_ms"] = duration_ms
                    estado["progresso_base_ms"] = progress_ms
                    estado["timestamp_base"] = agora
                    estado["energia"] = contexto["energia"]
                    estado["danceabilidade"] = contexto["danceabilidade"]
                    estado["tempo_bpm"] = contexto["tempo_bpm"]
                    estado["secoes"] = contexto["secoes"]
            else:
                if ultima_faixa_log != "PAUSADO":
                    print("[Spotify] ⏸ Nenhuma música ativa ou Spotify pausado.")
                    ultima_faixa_log = "PAUSADO"

                with estado["lock"]:
                    estado["tocando"] = False

        except Exception as e:
            if "rate" in str(e).lower() or "429" in str(e):
                if not aviso_rate_limit:
                    print("[Spotify] ⚠️ Rate Limit ativo na API do Spotify. Aguardando recuperação...")
                    aviso_rate_limit = True
            else:
                print(f"[Spotify] Aviso de consulta: {e}")

        time.sleep(3.0)



def thread_transmissao_udp():
    """Transmite os metadados contextuais via UDP a ~10Hz com tempo interpolado."""
    print(f"[Spotify] Broadcast UDP ativo na porta {PORTA_UDP}...")
    while True:
        agora = time.time()
        with estado["lock"]:
            tocando = estado["tocando"]
            faixa = estado["faixa"]
            artista = estado["artista"]
            energia = estado["energia"]
            danceabilidade = estado["danceabilidade"]
            tempo_bpm = estado["tempo_bpm"]
            secoes = estado["secoes"]
            duracao_ms = estado["duracao_ms"]
            progresso_base_ms = estado["progresso_base_ms"]
            timestamp_base = estado["timestamp_base"]

        if tocando:
            # Extrapolação do tempo local em alta precisão
            progresso_ms = int(progresso_base_ms + ((agora - timestamp_base) * 1000))
            if duracao_ms > 0:
                progresso_ms = min(progresso_ms, duracao_ms)
            progresso_s = progresso_ms / 1000.0

            # Identifica a seção atual da música
            secao_atual = None
            secao_idx = 0
            for idx, s in enumerate(secoes):
                if s["inicio_s"] <= progresso_s < (s["inicio_s"] + s["duracao_s"]):
                    secao_atual = s
                    secao_idx = idx
                    break

            secao_loudness = secao_atual.get("loudness", -8.0) if secao_atual else -8.0
            secao_tempo = secao_atual.get("tempo", tempo_bpm) if secao_atual else tempo_bpm


            # Classificação do modo de iluminação baseado no contexto
            if energia >= 0.70 or secao_loudness > -6.0:
                modo_sugerido = "alta_energia"
            elif energia < 0.40:
                modo_sugerido = "suave"
            else:
                modo_sugerido = "media_energia"

            payload = {
                "tipo": "spotify",
                "tocando": True,
                "faixa": faixa,
                "artista": artista,
                "progresso_ms": progresso_ms,
                "duracao_ms": duracao_ms,
                "energia": round(energia, 3),
                "danceabilidade": round(danceabilidade, 3),
                "tempo_bpm": round(tempo_bpm, 1),
                "secao_idx": secao_idx,
                "secao_loudness": round(secao_loudness, 2),
                "secao_tempo": round(secao_tempo, 1),
                "modo_sugerido": modo_sugerido
            }
        else:
            payload = {
                "tipo": "spotify",
                "tocando": False,
                "modo_sugerido": "standby"
            }

        mensagem = json.dumps(payload).encode("utf-8")
        sock.sendto(mensagem, ("<broadcast>", PORTA_UDP))
        time.sleep(0.1)  # 10Hz de transmissão contínua


def main():
    print("==================================================")
    print(" Servidor Spotify Contextual Iniciado")
    print(" Fornecendo inteligência e estrutura musical via UDP")
    print("==================================================")

    t_poll = threading.Thread(target=thread_polling_spotify, daemon=True)
    t_udp = threading.Thread(target=thread_transmissao_udp, daemon=True)

    t_poll.start()
    t_udp.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nParando Servidor Spotify...")


if __name__ == "__main__":
    main()
