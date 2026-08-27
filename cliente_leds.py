# -*- coding: utf-8 -*-
import os
import time
import socket
import json

try:
    import RPi.GPIO as GPIO
    GPIO.setmode(GPIO.BCM)
    GPIO.setwarnings(False)
except (ImportError, RuntimeError):
    pass

from drivers_hardware import LuzPWM, GloboRGB, ServoSG90

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(BASE_DIR, "config_hardware.json")

config = {}
if os.path.exists(CONFIG_PATH):
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            config = json.load(f)
    except Exception as e:
        print(f"[Aviso] Erro ao carregar config_hardware.json: {e}")

pinos = config.get("pinos", {})
p_strobe = pinos.get("strobe_branco", {}).get("bcm", 17)
p_globo_r = pinos.get("globo_r", {}).get("bcm", 23)
p_globo_g = pinos.get("globo_g", {}).get("bcm", 24)
p_globo_b = pinos.get("globo_b", {}).get("bcm", 25)
p_servo = pinos.get("servo_globo", {}).get("bcm", 18)

# Inicializa os Dispositivos de Hardware
strobe_branco = LuzPWM(p_strobe, freq_hz=15, nome="Strobe Branco")
globo_rgb = GloboRGB(p_globo_r, p_globo_g, p_globo_b, freq_hz=100)
servo_globo = ServoSG90(p_servo, angulo_min=15, angulo_max=165, freq_hz=50, nome="Servo Globo")

# Configuração da Rede UDP
PORTA_UDP = 5005
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(socket, "SO_REUSEPORT"):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sock.bind(("", PORTA_UDP))

print("==========================================================")
print(" Orquestrador de Iluminação com Servo Motor SG90")
print(f" - Strobe Branco (Graves): GPIO {p_strobe} (PWM 15Hz)")
print(f" - Globo RGB: GPIO R:{p_globo_r} G:{p_globo_g} B:{p_globo_b}")
print(f" - Servo SG90 (Globo): GPIO {p_servo} (Sinal PWM 50Hz)")
print(" Aguardando pacotes UDP de Áudio e Spotify...")
print("==========================================================")

contexto_spotify = {
    "ativo": False,
    "tocando": False,
    "faixa": "",
    "artista": "",
    "energia": 0.6,
    "danceabilidade": 0.6,
    "modo_sugerido": "fallback",
    "ultimo_timestamp": 0.0
}

ultimo_ciclo = time.time()
lado_salto_servo = 30.0

try:
    while True:
        data, addr = sock.recvfrom(2048)
        payload = json.loads(data.decode("utf-8"))
        agora = time.time()
        delta_tempo = max(0.001, agora - ultimo_ciclo)
        ultimo_ciclo = agora

        tipo_pacote = payload.get("tipo", "audio")

        # 1. PROCESSAMENTO DE METADADOS DO SPOTIFY
        if tipo_pacote == "spotify":
            contexto_spotify["ativo"] = True
            contexto_spotify["tocando"] = payload.get("tocando", False)
            contexto_spotify["faixa"] = payload.get("faixa", "")
            contexto_spotify["artista"] = payload.get("artista", "")
            contexto_spotify["energia"] = payload.get("energia", 0.6)
            contexto_spotify["danceabilidade"] = payload.get("danceabilidade", 0.6)
            contexto_spotify["modo_sugerido"] = payload.get("modo_sugerido", "media_energia")
            contexto_spotify["ultimo_timestamp"] = agora
            continue

        # 2. DETERMINAÇÃO DO MODO ATUAL
        spotify_online = contexto_spotify["ativo"] and (agora - contexto_spotify["ultimo_timestamp"] < 4.0)

        if spotify_online:
            if not contexto_spotify["tocando"]:
                modo_atual = "standby"
            else:
                modo_atual = contexto_spotify["modo_sugerido"]
        else:
            modo_atual = "fallback"

        # 3. EXTRAÇÃO DAS FAIXAS DE ÁUDIO DO MICROFONE
        faixas = payload.get("faixas", {})
        dados_graves = faixas.get("graves", {})
        dados_medios = faixas.get("medios", {})
        dados_agudos = faixas.get("agudos", {})

        ativo_grave = dados_graves.get("ativo", False)
        pico_grave = dados_graves.get("pico", False)
        nivel_grave = dados_graves.get("nivel", 0.0)
        nivel_medios = dados_medios.get("nivel", 0.3)
        ativo_agudo = dados_agudos.get("ativo", False)

        # 4. COREOGRAFIA DOS ATUADORES

        # --- A. CONTROLE DO GLOBO RGB ---
        globo_rgb.definir_paleta_contextual(modo_atual, nivel_medios, agora)

        # --- B. CONTROLE DO SERVO MOTOR SG90 ---
        if modo_atual == "alta_energia":
            # Nos kicks fortes de grave dá um salto brusco para um dos lados
            if pico_grave and nivel_grave >= 0.70:
                lado_salto_servo = 150.0 if lado_salto_servo <= 90.0 else 30.0
                servo_globo.definir_angulo(lado_salto_servo)
            else:
                # Varredura rítmica rápida a 1.0Hz
                servo_globo.varrer_senoidal(freq_hz=1.0, angulo_min=20.0, angulo_max=160.0, tempo_atual=agora)

        elif modo_atual == "suave":
            # Movimento pendular calmo e relaxante a 0.2Hz (5s por ciclo)
            servo_globo.varrer_senoidal(freq_hz=0.2, angulo_min=50.0, angulo_max=130.0, tempo_atual=agora)

        elif modo_atual == "standby":
            if ativo_grave or pico_grave or ativo_agudo:
                servo_globo.varrer_senoidal(freq_hz=0.4, angulo_min=45.0, angulo_max=135.0, tempo_atual=agora)
            else:
                servo_globo.definir_angulo(90.0)

        else:  # media_energia / fallback
            # Varredura rítmica constante a 0.5Hz (2s por ciclo)
            servo_globo.varrer_senoidal(freq_hz=0.5, angulo_min=30.0, angulo_max=150.0, tempo_atual=agora)

        # --- C. CONTROLE DO STROBE BRANCO (GRAVES / KICK) ---
        if modo_atual == "suave":
            if ativo_grave or nivel_grave > 0.3:
                strobe_branco.pulsar(min(35.0, nivel_grave * 35.0), duracao_s=0.07)
        else:
            if ativo_grave or pico_grave or nivel_grave >= 0.40:
                duty_strobe = 50.0 if modo_atual == "alta_energia" else 45.0
                strobe_branco.pulsar(duty_strobe, duracao_s=0.07)

        # 5. ATUALIZAÇÃO TEMPORAL DOS ATUADORES
        strobe_branco.atualizar(agora)
        servo_globo.atualizar(delta_tempo)

except KeyboardInterrupt:
    print("\nParando todos os dispositivos...")
finally:
    strobe_branco.parar()
    globo_rgb.parar()
    servo_globo.desativar_sinal()
    servo_globo.parar()
    try:
        GPIO.cleanup()
    except Exception:
        pass
