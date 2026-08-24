import time
import socket
import json
import RPi.GPIO as GPIO

# Configuração GPIO
BASS_PIN = 17   # Pino 11 (Graves / Kick)
TREBLE_PIN = 27 # Pino 13 (Agudos / Pratos)

GPIO.setmode(GPIO.BCM)
GPIO.setwarnings(False)
GPIO.setup(BASS_PIN, GPIO.OUT)
GPIO.setup(TREBLE_PIN, GPIO.OUT)

# Configuração inicial do PWM no pino do grave
FREQ_PWM_PADRAO = 15
strobe_grave = GPIO.PWM(BASS_PIN, FREQ_PWM_PADRAO)
strobe_grave.start(0)  # Começa desligado (0% Duty Cycle)

# Configuração da Rede UDP
PORTA_UDP = 5005
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(socket, "SO_REUSEPORT"):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sock.bind(("", PORTA_UDP))

print("==========================================================")
print(" Cliente LED Híbrido Iniciado (Microfone + Spotify)")
print(" - Graves (GPIO 17): PWM Strobe/Fade Dinâmico")
print(" - Agudos (GPIO 27): Flash On/Off Contextual")
print(" Aguardando pacotes de áudio e contexto musical...")
print("==========================================================")

# Estado Contextual do Spotify
contexto_spotify = {
    "ativo": False,
    "tocando": False,
    "faixa": "",
    "artista": "",
    "energia": 0.5,
    "danceabilidade": 0.5,
    "modo_sugerido": "fallback",
    "ultimo_timestamp": 0.0
}

# Controle de sustentação temporal dos efeitos
SUSTENTACAO_GRAVE = 0.06  # 60ms
SUSTENTACAO_AGUDO = 0.05  # 50ms

fim_efeito_grave = 0.0
fim_flash_agudo = 0.0

try:
    while True:
        data, addr = sock.recvfrom(2048)
        payload = json.loads(data.decode("utf-8"))
        agora = time.time()

        tipo_pacote = payload.get("tipo", "audio")

        # ---------------------------------------------------------
        # 1. PACOTE SPOTIFY (O CÉREBRO: Metadados e Contexto)
        # ---------------------------------------------------------
        if tipo_pacote == "spotify":
            contexto_spotify["ativo"] = True
            contexto_spotify["tocando"] = payload.get("tocando", False)
            contexto_spotify["faixa"] = payload.get("faixa", "")
            contexto_spotify["artista"] = payload.get("artista", "")
            contexto_spotify["energia"] = payload.get("energia", 0.5)
            contexto_spotify["danceabilidade"] = payload.get("danceabilidade", 0.5)
            contexto_spotify["modo_sugerido"] = payload.get("modo_sugerido", "media_energia")
            contexto_spotify["ultimo_timestamp"] = agora
            continue

        # ---------------------------------------------------------
        # 2. PACOTE DE ÁUDIO (O REFLEXO: Detecção Rápida em ms)
        # ---------------------------------------------------------
        # Verifica se o Spotify está ativo (timeout de 4.0 segundos para fallback automático)
        spotify_online = contexto_spotify["ativo"] and (agora - contexto_spotify["ultimo_timestamp"] < 4.0)

        if spotify_online:
            if not contexto_spotify["tocando"]:
                modo_atual = "standby"
            else:
                modo_atual = contexto_spotify["modo_sugerido"]
        else:
            modo_atual = "fallback"

        # Extrai os dados do microfone/FFT
        if "faixas" in payload:
            dados_graves = payload["faixas"].get("graves", {})
            dados_agudos = payload["faixas"].get("agudos", {})

            ativo_grave = dados_graves.get("ativo", False)
            pico_grave = dados_graves.get("pico", False)
            nivel_grave = dados_graves.get("nivel", 0.0)

            ativo_agudo = dados_agudos.get("ativo", False)
            pico_agudo = dados_agudos.get("pico", False)
            nivel_agudo = dados_agudos.get("nivel", 0.0)
        else:
            pico_grave = payload.get("graves", 0) > 2500000
            ativo_grave = pico_grave
            nivel_grave = 1.0 if pico_grave else 0.0

            pico_agudo = payload.get("agudos", 0) > 10000
            ativo_agudo = pico_agudo
            nivel_agudo = 1.0 if pico_agudo else 0.0

        # ---------------------------------------------------------
        # 3. MATRIZ DE DECISÃO (AÇÃO NO HARDWARE)
        # ---------------------------------------------------------

        # MODO STANDBY: Spotify pausado ou sem música
        if modo_atual == "standby":
            # Se mesmo em standby o microfone captar batidas fortes no ambiente, responde pelo reflexo
            if ativo_grave or pico_grave or nivel_grave >= 0.45:
                fim_efeito_grave = max(fim_efeito_grave, agora + SUSTENTACAO_GRAVE)
                strobe_grave.ChangeDutyCycle(45)
            elif agora >= fim_efeito_grave:
                strobe_grave.ChangeDutyCycle(0)

            if pico_agudo or (ativo_agudo and nivel_agudo >= 0.50):
                fim_flash_agudo = max(fim_flash_agudo, agora + SUSTENTACAO_AGUDO)
                GPIO.output(TREBLE_PIN, GPIO.HIGH)
            elif agora >= fim_flash_agudo:
                GPIO.output(TREBLE_PIN, GPIO.LOW)
            continue

        # MODO SUAVE: Música acústica, lofi, calma (energia < 0.40)
        elif modo_atual == "suave":
            # Grave: Sem estrobo agressivo; brilho pulsante suave proporcional ao nível
            if ativo_grave or nivel_grave > 0.3:
                duty_suave = min(35, int(nivel_grave * 35))
                strobe_grave.ChangeDutyCycle(duty_suave)
                fim_efeito_grave = agora + SUSTENTACAO_GRAVE
            elif agora >= fim_efeito_grave:
                strobe_grave.ChangeDutyCycle(0)


            # Agudo: Ativa apenas em pratos muito nítidos e suaves
            if pico_agudo and nivel_agudo >= 0.70:
                fim_flash_agudo = agora + SUSTENTACAO_AGUDO
                GPIO.output(TREBLE_PIN, GPIO.HIGH)
            elif agora >= fim_flash_agudo:
                GPIO.output(TREBLE_PIN, GPIO.LOW)

        # MODO ALTA ENERGIA: Refrão, Drop, Rock/Eletrônica/Funk (energia >= 0.70)
        elif modo_atual == "alta_energia":
            # Grave: Strobe máximo a 15Hz (50% DutyCycle)
            if ativo_grave or pico_grave or nivel_grave >= 0.40:
                fim_efeito_grave = max(fim_efeito_grave, agora + SUSTENTACAO_GRAVE)
                strobe_grave.ChangeDutyCycle(50)
            elif agora >= fim_efeito_grave:
                strobe_grave.ChangeDutyCycle(0)

            # Agudo: Flash ágil e sensível nos pratos
            if pico_agudo or (ativo_agudo and nivel_agudo >= 0.40):
                fim_flash_agudo = max(fim_flash_agudo, agora + SUSTENTACAO_AGUDO)
                GPIO.output(TREBLE_PIN, GPIO.HIGH)
            elif agora >= fim_flash_agudo:
                GPIO.output(TREBLE_PIN, GPIO.LOW)

        # MODO MÉDIA ENERGIA / FALLBACK: Resposta rítmica equilibrada
        else:
            # Grave: Strobe rítmico padrão
            if ativo_grave or pico_grave or nivel_grave >= 0.45:
                fim_efeito_grave = max(fim_efeito_grave, agora + SUSTENTACAO_GRAVE)
                strobe_grave.ChangeDutyCycle(45)
            elif agora >= fim_efeito_grave:
                strobe_grave.ChangeDutyCycle(0)

            # Agudo: Flash nos pratos
            if pico_agudo or (ativo_agudo and nivel_agudo >= 0.50):
                fim_flash_agudo = max(fim_flash_agudo, agora + SUSTENTACAO_AGUDO)
                GPIO.output(TREBLE_PIN, GPIO.HIGH)
            elif agora >= fim_flash_agudo:
                GPIO.output(TREBLE_PIN, GPIO.LOW)

except KeyboardInterrupt:
    print("\nParando LEDs...")
finally:
    strobe_grave.stop()
    GPIO.cleanup()



