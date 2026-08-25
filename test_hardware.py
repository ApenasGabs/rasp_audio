# -*- coding: utf-8 -*-
"""
Script de Teste de Bancada / Hardware Multi-Canais & Bidirecional
"""
import time
import os
import json

try:
    import RPi.GPIO as GPIO
    GPIO.setmode(GPIO.BCM)
    GPIO.setwarnings(False)
except (ImportError, RuntimeError):
    print("ERRO: RPi.GPIO não disponível.")
    exit(1)

from drivers_hardware import LuzDigital, LuzPWM, GloboRGB, MotorBidirecional

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(BASE_DIR, "config_hardware.json")

config = {}
if os.path.exists(CONFIG_PATH):
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        config = json.load(f)

pinos = config.get("pinos", {})
p_strobe = pinos.get("strobe_branco", {}).get("bcm", 17)
p_laser_g = pinos.get("laser_verde", {}).get("bcm", 27)
p_laser_r = pinos.get("laser_vermelho", {}).get("bcm", 22)
p_globo_r = pinos.get("globo_r", {}).get("bcm", 23)
p_globo_g = pinos.get("globo_g", {}).get("bcm", 24)
p_globo_b = pinos.get("globo_b", {}).get("bcm", 25)

p_mot_laser_in1 = pinos.get("motor_laser_filtro", {}).get("bcm_in1", 18)
p_mot_laser_in2 = pinos.get("motor_laser_filtro", {}).get("bcm_in2", 13)

p_mot_globo_in1 = pinos.get("motor_globo", {}).get("bcm_in1", 26)
p_mot_globo_in2 = pinos.get("motor_globo", {}).get("bcm_in2", 19)

strobe_branco = LuzPWM(p_strobe, freq_hz=15)
laser_verde = LuzDigital(p_laser_g)
laser_vermelho = LuzDigital(p_laser_r)
globo_rgb = GloboRGB(p_globo_r, p_globo_g, p_globo_b)
motor_laser = MotorBidirecional(p_mot_laser_in1, p_mot_laser_in2, freq_hz=1000)
motor_globo = MotorBidirecional(p_mot_globo_in1, p_mot_globo_in2, freq_hz=1000)

print("=" * 65)
print(" 🛠️  TESTE DE BANCADA - HARDWARE MULTI-CANAIS & BIDIRECIONAL")
print("=" * 65)

try:
    print("1. Testando Strobe Branco (GPIO 17)...")
    strobe_branco.definir_brilho(50)
    time.sleep(1.5)
    strobe_branco.definir_brilho(0)

    print("2. Testando Laser Verde (GPIO 27)...")
    laser_verde.ligar()
    time.sleep(1.5)
    laser_verde.desligar()

    print("3. Testando Laser Vermelho (GPIO 22)...")
    laser_vermelho.ligar()
    time.sleep(1.5)
    laser_vermelho.desligar()

    print("4. Testando Cores do Globo RGB (Vermelho -> Verde -> Azul -> Branco)...")
    globo_rgb.definir_rgb(100, 0, 0)
    time.sleep(1.0)
    globo_rgb.definir_rgb(0, 100, 0)
    time.sleep(1.0)
    globo_rgb.definir_rgb(0, 0, 100)
    time.sleep(1.0)
    globo_rgb.definir_rgb(100, 100, 100)
    time.sleep(1.0)
    globo_rgb.definir_rgb(0, 0, 0)

    print("5. Testando Motor do Globo -> Sentido HORÁRIO (Avanço a 70%)...")
    motor_globo.definir_movimento(70, direcao=1)
    for _ in range(30):
        motor_globo.atualizar(0.05)
        time.sleep(0.05)

    print("6. Testando Motor do Globo -> Invertendo para Sentido ANTI-HORÁRIO (Recuo a 70%)...")
    motor_globo.inverter_direcao()
    for _ in range(40):
        motor_globo.atualizar(0.05)
        time.sleep(0.05)
    motor_globo.definir_movimento(0, 0)
    for _ in range(15):
        motor_globo.atualizar(0.05)
        time.sleep(0.05)

    print("7. Testando Motor do Filtro do Laser -> Oscilação Rápida (Vai e Volta a 2.5Hz)...")
    inicio_osc = time.time()
    while time.time() - inicio_osc < 3.0:
        agora = time.time()
        motor_laser.oscilar_senoidal(freq_hz=2.5, velocidade_max_pct=90.0, tempo_atual=agora)
        motor_laser.atualizar(0.025)
        time.sleep(0.025)

    motor_laser.definir_movimento(0, 0)
    for _ in range(10):
        motor_laser.atualizar(0.05)
        time.sleep(0.05)

    print("=" * 65)
    print(" ✅ TODOS OS TESTES BIDIRECIONAIS CONCLUÍDOS COM SUCESSO!")
    print("=" * 65)

except KeyboardInterrupt:
    print("\nInterrompendo teste...")
finally:
    strobe_branco.parar()
    laser_verde.desligar()
    laser_vermelho.desligar()
    globo_rgb.parar()
    motor_laser.parar()
    motor_globo.parar()
    GPIO.cleanup()
