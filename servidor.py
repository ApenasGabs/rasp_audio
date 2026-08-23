import time
import socket
import json
import numpy as np
import pyaudio

# Configuração da Rede UDP (Broadcast)
PORTA_UDP = 5005
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

# Definição das faixas de frequência (em Hz) e parâmetros de detecção de pico
# Cada faixa possui:
# - freq_min / freq_max: limites em Hz reais
# - sensibilidade: multiplicador sobre o volume médio recente para disparar pico
# - piso_minimo: nível mínimo de energia para ignorar silêncio/ruído de fundo
# - cooldown_ms: intervalo mínimo (em ms) entre dois picos seguidos
FAIXAS_CONFIG = {
    "sub_graves": {
        "freq_min": 20,
        "freq_max": 80,
        "sensibilidade": 1.4,
        "piso_minimo": 100000,
        "cooldown_ms": 120,
    },
    "graves": {
        "freq_min": 80,
        "freq_max": 250,
        "sensibilidade": 1.35,
        "piso_minimo": 80000,
        "cooldown_ms": 100,
    },
    "medios_graves": {
        "freq_min": 250,
        "freq_max": 600,
        "sensibilidade": 1.3,
        "piso_minimo": 40000,
        "cooldown_ms": 80,
    },
    "medios": {
        "freq_min": 600,
        "freq_max": 2500,
        "sensibilidade": 1.3,
        "piso_minimo": 20000,
        "cooldown_ms": 80,
    },
    "agudos": {
        "freq_min": 2500,
        "freq_max": 8000,
        "sensibilidade": 1.3,
        "piso_minimo": 8000,
        "cooldown_ms": 70,
    },
    "super_agudos": {
        "freq_min": 8000,
        "freq_max": 18000,
        "sensibilidade": 1.35,
        "piso_minimo": 4000,
        "cooldown_ms": 70,
    },
}


class DetectorPicoFaixa:
    """Rastreia envelope rítmico com baseline assimétrica (evita sufocar sequências de batidas)."""

    def __init__(self, nome, config, rate, chunk):
        self.nome = nome
        self.config = config
        self.rate = rate
        self.chunk = chunk

        # Calcula os bins correspondentes no FFT (rfft)
        self.bin_inicio = max(0, int(config["freq_min"] * (chunk / rate)))
        self.bin_fim = min(
            (chunk // 2) + 1,
            max(self.bin_inicio + 1, int(config["freq_max"] * (chunk / rate))),
        )

        self.baseline = float(config["piso_minimo"])
        self.envelope = float(config["piso_minimo"])
        self.sensibilidade = config["sensibilidade"]
        self.piso_minimo = float(config["piso_minimo"])
        self.cooldown_ms = config["cooldown_ms"]
        self.ultimo_pico_tempo = 0.0
        self.ultimo_valor = 0.0

    def processar(self, fft_data, tempo_atual):
        # Média da magnitude das frequências nesta faixa
        fatia = fft_data[self.bin_inicio : self.bin_fim]
        valor = float(np.mean(fatia)) if len(fatia) > 0 else 0.0

        # Atualização da baseline (piso de ruído) de forma ASSIMÉTRICA:
        # Quando há som forte, a baseline sobe muito devagar para NÃO engolir batidas seguidas.
        # Quando há silêncio, a baseline desce mais rápido para restaurar sensibilidade.
        if valor > self.baseline:
            self.baseline = (0.008 * valor) + (0.992 * self.baseline)
        else:
            self.baseline = (0.08 * valor) + (0.92 * self.baseline)

        # Garante que a baseline nunca fique abaixo do piso mínimo configurado
        self.baseline = max(self.piso_minimo, self.baseline)

        # Limiar dinâmico que define a presença da batida
        limiar = self.baseline * self.sensibilidade

        # Envelope dinâmico (ataque instantâneo, decaimento suave)
        if valor > self.envelope:
            self.envelope = valor
        else:
            self.envelope = max(self.baseline, (self.envelope * 0.82) + (valor * 0.18))

        # Status de presença rítmica (ativo durante toda a duração da batida)
        esta_ativo = valor >= limiar

        # Detecção de transiente/pico de início de batida
        tempo_desde_ultimo = (tempo_atual - self.ultimo_pico_tempo) * 1000.0
        eh_pico = False

        if esta_ativo and valor > (self.ultimo_valor * 1.03) and tempo_desde_ultimo >= self.cooldown_ms:
            eh_pico = True
            self.ultimo_pico_tempo = tempo_atual

        # Nível relativo normalizado (0.0 a 1.0)
        faixa_dinamica = max(1.0, self.envelope - self.baseline)
        if esta_ativo:
            nivel_norm = min(1.0, max(0.2, (valor - self.baseline) / faixa_dinamica))
        else:
            nivel_norm = max(0.0, min(0.3, (valor - self.baseline) / faixa_dinamica))

        self.ultimo_valor = valor

        return {
            "valor": int(valor),
            "nivel": round(float(nivel_norm), 3),
            "pico": bool(eh_pico),
            "ativo": bool(esta_ativo),
            "limiar": int(limiar),
        }



def main():
    p = pyaudio.PyAudio()

    # Busca automática do microfone USB
    INDEX_MICROFONE = None
    CHANNELS = 1
    RATE = 44100

    for i in range(p.get_device_count()):
        info = p.get_device_info_by_index(i)
        if info.get("maxInputChannels") > 0 and "USB" in info.get("name", ""):
            INDEX_MICROFONE = i
            CHANNELS = int(info.get("maxInputChannels"))
            RATE = int(info.get("defaultSampleRate"))
            break

    if INDEX_MICROFONE is None:
        print("ERRO: Microfone USB não encontrado.")
        p.terminate()
        return

    CHUNK = 1024
    print(
        f"Dispositivo: ID {INDEX_MICROFONE} | Canais: {CHANNELS} | Rate: {RATE}Hz | Chunk: {CHUNK}"
    )

    # Inicializa os detectores para cada faixa configurada
    detectores = {
        nome: DetectorPicoFaixa(nome, cfg, RATE, CHUNK)
        for nome, cfg in FAIXAS_CONFIG.items()
    }

    stream = p.open(
        format=pyaudio.paInt16,
        channels=CHANNELS,
        rate=RATE,
        input=True,
        input_device_index=INDEX_MICROFONE,
        frames_per_buffer=CHUNK,
    )

    print(f"Servidor Iniciado! Processando picos dinâmicos via UDP na porta {PORTA_UDP}...")

    try:
        while True:
            data = stream.read(CHUNK, exception_on_overflow=False)
            audio_data = np.frombuffer(data, dtype=np.int16)

            if CHANNELS == 2:
                audio_data = audio_data[0::2]

            fft_data = np.abs(np.fft.rfft(audio_data))
            agora = time.time()

            faixas_resultado = {}
            for nome, detector in detectores.items():
                faixas_resultado[nome] = detector.processar(fft_data, agora)

            # Payload rico contendo dados detalhados por faixa + chaves legadas para compatibilidade
            payload = {
                "tipo": "audio",
                "faixas": faixas_resultado,
                # Atalhos diretos para compatibilidade com versões anteriores
                "graves": faixas_resultado["graves"]["valor"],
                "medios_graves": faixas_resultado["medios_graves"]["valor"],
                "medios": faixas_resultado["medios"]["valor"],
                "agudos": faixas_resultado["agudos"]["valor"],
                "super_agudos": faixas_resultado["super_agudos"]["valor"],
            }


            mensagem = json.dumps(payload).encode("utf-8")
            sock.sendto(mensagem, ("<broadcast>", PORTA_UDP))

    except KeyboardInterrupt:
        print("\nParando Servidor...")
    finally:
        stream.stop_stream()
        stream.close()
        p.terminate()


if __name__ == "__main__":
    main()


