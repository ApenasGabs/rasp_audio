# -*- coding: utf-8 -*-
"""
Script de Teste de Bancada / Hardware (Strobe + Globo RGB + Servo SG90)
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

from drivers_hardware import LuzPWM, GloboRGB, ServoSG90

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(BASE_DIR, "config_hardware.json")

config = {}
if os.path.exists(CONFIG_PATH):
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        config = json.load(f)

pinos = config.get("pinos", {})
p_strobe = pinos.get("strobe_branco", {}).get("bcm", 17)
p_globo_r = pinos.get("globo_r", {}).get("bcm", 23)
p_globo_g = pinos.get("globo_g", {}).get("bcm", 24)
p_globo_b = pinos.get("globo_b", {}).get("bcm", 25)
p_servo = pinos.get("servo_globo", {}).get("bcm", 18)

strobe_branco = LuzPWM(p_strobe, freq_hz=15)
globo_rgb = GloboRGB(p_globo_r, p_globo_g, p_globo_b)
servo_globo = ServoSG90(p_servo, angulo_min=0, angulo_max=180, freq_hz=50)

print("=" * 65)
print(" 🛠️  TESTE DE BANCADA - STROBE + GLOBO RGB + SERVO SG90")
print("=" * 65)

try:
    print("1. Testando Strobe Branco (GPIO 17)...")
    strobe_branco.definir_brilho(50)
    time.sleep(1.5)
    strobe_branco.definir_brilho(0)

    print("2. Testando Cores do Globo RGB (Vermelho -> Verde -> Azul -> Branco)...")
    globo_rgb.definir_rgb(100, 0, 0)
    time.sleep(1.0)
    globo_rgb.definir_rgb(0, 100, 0)
    time.sleep(1.0)
    globo_rgb.definir_rgb(0, 0, 100)
    time.sleep(1.0)
    globo_rgb.definir_rgb(100, 100, 100)
    time.sleep(1.0)
    globo_rgb.definir_rgb(0, 0, 0)

    print("3. Testando Servo SG90 (GPIO 18):")
    print("   -> Movendo para 0°...")
    servo_globo.definir_angulo(0)
    time.sleep(1.0)

    print("   -> Movendo para 90° (Centro)...")
    servo_globo.definir_angulo(90)
    time.sleep(1.0)

    print("   -> Movendo para 180°...")
    servo_globo.definir_angulo(180)
    time.sleep(1.0)

    print("   -> Movendo para 90° (Centro)...")
    servo_globo.definir_angulo(90)
    time.sleep(1.0)

    print("4. Testando Varredura Suave Contínua (Sweep)...")
    inicio_sweep = time.time()
    while time.time() - inicio_sweep < 4.0:
        agora = time.time()
        servo_globo.varrer_senoidal(freq_hz=0.5, angulo_min=20, angulo_max=160, tempo_atual=agora)
        time.sleep(0.02)

    servo_globo.definir_angulo(90)
    time.sleep(0.5)

    print("=" * 65)
    print(" ✅ TODOS OS TESTES CONCLUÍDOS COM SUCESSO!")
    print("=" * 65)

except KeyboardInterrupt:
    print("\nInterrompendo teste...")
finally:
    strobe_branco.parar()
    globo_rgb.parar()
    servo_globo.desativar_sinal()
    servo_globo.parar()
    GPIO.cleanup()
