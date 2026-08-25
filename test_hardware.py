# -*- coding: utf-8 -*-
"""
Script de Teste de Bancada / Hardware
Permite acionar e validar individualmente cada laser, LED do globo e motor da Ponte H.
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

from drivers_hardware import LuzDigital, LuzPWM, GloboRGB, MotorPonteH

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
p_mot_laser = pinos.get("motor_laser_filtro", {}).get("bcm", 18)
p_mot_globo = pinos.get("motor_globo", {}).get("bcm", 26)

strobe_branco = LuzPWM(p_strobe, freq_hz=15)
laser_verde = LuzDigital(p_laser_g)
laser_vermelho = LuzDigital(p_laser_r)
globo_rgb = GloboRGB(p_globo_r, p_globo_g, p_globo_b)
motor_laser = MotorPonteH(p_mot_laser, freq_hz=1000)
motor_globo = MotorPonteH(p_mot_globo, freq_hz=1000)

print("=" * 60)
print(" 🛠️  TESTE AUTOMÁTICO DE HARDWARE (BANCADA)")
print("=" * 60)

try:
    print("1. Testando Strobe Branco (GPIO 17) por 2 segundos...")
    strobe_branco.definir_brilho(50)
    time.sleep(2.0)
    strobe_branco.definir_brilho(0)

    print("2. Testando Laser Verde (GPIO 27) por 2 segundos...")
    laser_verde.ligar()
    time.sleep(2.0)
    laser_verde.desligar()

    print("3. Testando Laser Vermelho (GPIO 22) por 2 segundos...")
    laser_vermelho.ligar()
    time.sleep(2.0)
    laser_vermelho.desligar()

    print("4. Testando Globo LED Vermelho (GPIO 23)...")
    globo_rgb.definir_rgb(100, 0, 0)
    time.sleep(1.5)

    print("5. Testando Globo LED Verde (GPIO 24)...")
    globo_rgb.definir_rgb(0, 100, 0)
    time.sleep(1.5)

    print("6. Testando Globo LED Azul (GPIO 25)...")
    globo_rgb.definir_rgb(0, 0, 100)
    time.sleep(1.5)

    print("7. Testando Globo Branco (R+G+B)...")
    globo_rgb.definir_rgb(100, 100, 100)
    time.sleep(1.5)
    globo_rgb.definir_rgb(0, 0, 0)

    print("8. Testando Motor do Filtro do Laser (GPIO 18) a 60%...")
    motor_laser.definir_velocidade(60)
    for _ in range(40):
        motor_laser.atualizar(0.05)
        time.sleep(0.05)
    motor_laser.definir_velocidade(0)
    for _ in range(20):
        motor_laser.atualizar(0.05)
        time.sleep(0.05)

    print("9. Testando Motor do Globo (GPIO 26) a 70%...")
    motor_globo.definir_velocidade(70)
    for _ in range(40):
        motor_globo.atualizar(0.05)
        time.sleep(0.05)
    motor_globo.definir_velocidade(0)
    for _ in range(20):
        motor_globo.atualizar(0.05)
        time.sleep(0.05)

    print("=" * 60)
    print(" ✅ TODOS OS TESTES FORAM CONCLUÍDOS COM SUCESSO!")
    print("=" * 60)

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
