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

from drivers_hardware import LuzDigital, LuzPWM, GloboRGB, MotorBidirecional

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
p_laser_g = pinos.get("laser_verde", {}).get("bcm", 27)
p_laser_r = pinos.get("laser_vermelho", {}).get("bcm", 22)
p_globo_r = pinos.get("globo_r", {}).get("bcm", 23)
p_globo_g = pinos.get("globo_g", {}).get("bcm", 24)
p_globo_b = pinos.get("globo_b", {}).get("bcm", 25)

# Pinos Bidirecionais da Ponte H (IN1 / IN2)
p_mot_laser_in1 = pinos.get("motor_laser_filtro", {}).get("bcm_in1", 18)
p_mot_laser_in2 = pinos.get("motor_laser_filtro", {}).get("bcm_in2", 13)

p_mot_globo_in1 = pinos.get("motor_globo", {}).get("bcm_in1", 26)
p_mot_globo_in2 = pinos.get("motor_globo", {}).get("bcm_in2", 19)

# Inicializa os Dispositivos de Hardware
strobe_branco = LuzPWM(p_strobe, freq_hz=15, nome="Strobe Branco")
laser_verde = LuzDigital(p_laser_g, nome="Laser Verde")
laser_vermelho = LuzDigital(p_laser_r, nome="Laser Vermelho")
globo_rgb = GloboRGB(p_globo_r, p_globo_g, p_globo_b, freq_hz=100)

motor_laser = MotorBidirecional(p_mot_laser_in1, p_mot_laser_in2, freq_hz=1000, nome="Filtro Laser (Ponte H)")
motor_globo = MotorBidirecional(p_mot_globo_in1, p_mot_globo_in2, freq_hz=1000, nome="Globo Giratorio (Ponte H)")

# Configuração da Rede UDP
PORTA_UDP = 5005
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(socket, "SO_REUSEPORT"):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sock.bind(("", PORTA_UDP))

print("==========================================================")
print(" Orquestrador de Iluminação Multi-Canais & Bidirecional")
print(f" - Strobe Branco: GPIO {p_strobe} (PWM 15Hz)")
print(f" - Lasers Verde / Vermelho: GPIO {p_laser_g} / GPIO {p_laser_r}")
print(f" - Globo RGB: GPIO R:{p_globo_r} G:{p_globo_g} B:{p_globo_b}")
print(f" - Motor Filtro Laser: IN1:{p_mot_laser_in1} / IN2:{p_mot_laser_in2} (Bidirecional)")
print(f" - Motor Globo Giratório: IN1:{p_mot_globo_in1} / IN2:{p_mot_globo_in2} (Bidirecional)")
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
ultimo_inversao_globo = time.time()
direcao_globo = 1
contador_kicks = 0

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
        dados_super_agudos = faixas.get("super_agudos", {})

        ativo_grave = dados_graves.get("ativo", False)
        pico_grave = dados_graves.get("pico", False)
        nivel_grave = dados_graves.get("nivel", 0.0)

        nivel_medios = dados_medios.get("nivel", 0.3)
        ativo_medios = dados_medios.get("ativo", False)

        ativo_agudo = dados_agudos.get("ativo", False)
        pico_agudo = dados_agudos.get("pico", False)
        nivel_agudo = dados_agudos.get("nivel", 0.0)

        pico_super = dados_super_agudos.get("pico", False)

        # 4. COREOGRAFIA DOS ATUADORES

        # --- A. CONTROLE DO GLOBO RGB ---
        globo_rgb.definir_paleta_contextual(modo_atual, nivel_medios, agora)

        # --- B. CONTROLE BIDIRECIONAL DOS MOTORES (PONTE H) ---
        
        # Gerenciamento de inversão do Globo (Sweep / Vai e Volta periódico ou por batida)
        tempo_varredura = 4.0 if modo_atual == "alta_energia" else 6.5
        if agora - ultimo_inversao_globo >= tempo_varredura:
            direcao_globo = -1 if direcao_globo == 1 else 1
            ultimo_inversao_globo = agora

        # Inversão de impacto no Drop / Kicks pesados
        if pico_grave and nivel_grave >= 0.85:
            contador_kicks += 1
            if contador_kicks >= 4:  # A cada 4 batidas fortes inverte sentido
                direcao_globo = -1 if direcao_globo == 1 else 1
                contador_kicks = 0
                ultimo_inversao_globo = agora

        if modo_atual == "alta_energia":
            # Globo: velocidade total alternando direção
            motor_globo.definir_movimento(100.0, direcao_globo)
            # Filtro do Laser: oscilação rápida rítmica (vai e volta a 2.5Hz)
            motor_laser.oscilar_senoidal(freq_hz=2.5, velocidade_max_pct=100.0, tempo_atual=agora)

        elif modo_atual == "suave":
            # Movimento calmo e suave (vai e volta lento)
            motor_globo.definir_movimento(25.0, direcao_globo)
            motor_laser.definir_movimento(0.0, 0)

        elif modo_atual == "standby":
            if ativo_grave or pico_grave or ativo_agudo:
                motor_globo.definir_movimento(45.0, direcao_globo)
                motor_laser.definir_movimento(30.0, 1)
            else:
                motor_globo.definir_movimento(0.0, 0)
                motor_laser.definir_movimento(0.0, 0)

        else:  # media_energia / fallback
            motor_globo.definir_movimento(60.0, direcao_globo)
            # Filtro do laser acompanha pratos ou oscila suavemente a 1.2Hz
            if ativo_agudo or pico_agudo:
                motor_laser.oscilar_senoidal(freq_hz=1.8, velocidade_max_pct=75.0, tempo_atual=agora)
            else:
                motor_laser.definir_movimento(35.0, 1)

        # --- C. CONTROLE DO STROBE BRANCO (GRAVES / KICK) ---
        if modo_atual == "suave":
            if ativo_grave or nivel_grave > 0.3:
                strobe_branco.pulsar(min(35.0, nivel_grave * 35.0), duracao_s=0.07)
        else:
            if ativo_grave or pico_grave or nivel_grave >= 0.40:
                duty_strobe = 50.0 if modo_atual == "alta_energia" else 45.0
                strobe_branco.pulsar(duty_strobe, duracao_s=0.07)

        # --- D. CONTROLE DOS LASERS (AGUDOS E ATAQUES) ---
        if modo_atual != "suave":
            if pico_agudo or (ativo_agudo and nivel_agudo >= 0.40):
                laser_verde.pulsar(duracao_s=0.05)

            if (pico_grave and nivel_grave >= 0.70) or pico_super:
                laser_vermelho.pulsar(duracao_s=0.06)

        # 5. ATUALIZAÇÃO TEMPORAL DOS ATUADORES
        strobe_branco.atualizar(agora)
        laser_verde.atualizar(agora)
        laser_vermelho.atualizar(agora)
        motor_globo.atualizar(delta_tempo)
        motor_laser.atualizar(delta_tempo)

except KeyboardInterrupt:
    print("\nParando todos os dispositivos...")
finally:
    strobe_branco.parar()
    laser_verde.desligar()
    laser_vermelho.desligar()
    globo_rgb.parar()
    motor_globo.parar()
    motor_laser.parar()
    try:
        GPIO.cleanup()
    except Exception:
        pass
