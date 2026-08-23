import os
import time
import socket
import json
import threading

try:
    from dotenv import load_dotenv
    load_dotenv()
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
    print(" Por favor, crie/edite o arquivo .env com:")
    print(" SPOTIPY_CLIENT_ID=seu_id")
    print(" SPOTIPY_CLIENT_SECRET=seu_secret")
    print(" SPOTIPY_REDIRECT_URI=http://127.0.0.1:8888/callback")
    print("=" * 65)
    exit(1)

ESCOPO = "user-read-playback-state user-read-currently-playing"

# Inicializa o cliente Spotify com suporte a login headless (no terminal da Raspberry Pi)
auth_manager = SpotifyOAuth(
    client_id=CLIENT_ID,
    client_secret=CLIENT_SECRET,
    redirect_uri=REDIRECT_URI,
    scope=ESCOPO,
    open_browser=False,  # Imprime URL no terminal caso precise autorizar
    cache_path=".cache_spotify"
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
    "energia": 0.5,
    "danceabilidade": 0.5,
    "tempo_bpm": 120.0,
    "valencia": 0.5,
    "secoes": [],
    "lock": threading.Lock()
}

# Cache de análise musical por ID de faixa
cache_faixas = {}

def buscar_contexto_musica(track_id):
    """Busca features e análise de seções da música, com fallback inteligente."""
    if not track_id:
        return {"energia": 0.5, "danceabilidade": 0.5, "tempo_bpm": 120.0, "valencia": 0.5, "secoes": []}

    if track_id in cache_faixas:
        return cache_faixas[track_id]

    features = {}
    secoes = []

    try:
        audio_feat = sp.audio_features([track_id])
        if audio_feat and audio_feat[0]:
            f = audio_feat[0]
            features = {
                "energia": f.get("energy", 0.5),
                "danceabilidade": f.get("danceability", 0.5),
                "tempo_bpm": f.get("tempo", 120.0),
                "valencia": f.get("valence", 0.5)
            }
    except Exception as e:
        print(f"[Spotify] Aviso ao buscar audio_features: {e}")
        features = {"energia": 0.5, "danceabilidade": 0.5, "tempo_bpm": 120.0, "valencia": 0.5}

    try:
        analysis = sp.audio_analysis(track_id)
        if analysis and "sections" in analysis:
            secoes = [
                {
                    "inicio_s": s.get("start", 0.0),
                    "duracao_s": s.get("duration", 0.0),
                    "loudness": s.get("loudness", -8.0),
                    "tempo": s.get("tempo", features.get("tempo_bpm", 120.0)),
                    "confianca": s.get("confidence", 0.5)
                }
                for s in analysis["sections"]
            ]
    except Exception as e:
        # Fallback caso audio_analysis não esteja disponível para a conta/app
        pass

    dados = {**features, "secoes": secoes}
    cache_faixas[track_id] = dados
    return dados


def thread_polling_spotify():
    """Consulta a API do Spotify a cada ~1.5s para manter o estado atualizado."""
    print("[Spotify] Thread de monitoramento iniciada.")
    while True:
        try:
            playback = sp.current_playback()
            agora = time.time()

            if playback and playback.get("is_playing") and playback.get("item"):
                item = playback["item"]
                track_id = item.get("id")
                track_name = item.get("name", "Desconhecido")
                artist_name = item["artists"][0]["name"] if item.get("artists") else "Desconhecido"
                progress_ms = playback.get("progress_ms", 0)
                duration_ms = item.get("duration_ms", 0)

                contexto = buscar_contexto_musica(track_id)

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
                    estado["valencia"] = contexto["valencia"]
                    estado["secoes"] = contexto["secoes"]
            else:
                with estado["lock"]:
                    estado["tocando"] = False

        except Exception as e:
            print(f"[Spotify] Erro ao consultar playback: {e}")

        time.sleep(1.5)


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

            secao_loudness = secao_atual["loudness"] if secao_atual else -8.0
            secao_tempo = secao_atual["tempo"] if secao_atual else tempo_bpm

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
