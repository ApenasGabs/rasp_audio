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

# Configuração do PWM para o efeito Strobe no Grave (15 piscadas por segundo)
strobe_grave = GPIO.PWM(BASS_PIN, 15)
strobe_grave.start(0)  # Começa desligado (0% Duty Cycle)

# Configuração da Rede UDP
PORTA_UDP = 5005
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(socket, "SO_REUSEPORT"):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sock.bind(("", PORTA_UDP))

print("==================================================")
print(" Cliente LED Iniciado (Modo Dinâmico de Picos)")
print(" - Graves (GPIO 17): Efeito Strobe via PWM")
print(" - Agudos (GPIO 27): Flash On/Off")
print(" Aguardando dados do servidor...")
print("==================================================")

# Controle de acionamento rítmico
# Duração mínima de sustentação para pulsos muito rápidos (em segundos)
SUSTENTACAO_MINIMA_GRAVE = 0.06  # 60ms mínimo para a batida ser bem sentida no strobe
SUSTENTACAO_MINIMA_AGUDO = 0.05  # 50ms mínimo para o prato

fim_strobe_grave = 0.0
fim_flash_agudo = 0.0

try:
    while True:
        data, addr = sock.recvfrom(2048)
        payload = json.loads(data.decode("utf-8"))
        agora = time.time()

        # Extrai os dados das faixas
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

        # --- Controle do Grave (STROBE via PWM no Ritmo da Batida) ---
        # Ativa quando há presença real do grave ou início de batida
        if ativo_grave or pico_grave or nivel_grave >= 0.45:
            fim_strobe_grave = max(fim_strobe_grave, agora + SUSTENTACAO_MINIMA_GRAVE)
            strobe_grave.ChangeDutyCycle(50)  # Pulsa em 15Hz durante a presença do grave
        elif agora >= fim_strobe_grave:
            strobe_grave.ChangeDutyCycle(0)   # Desliga no intervalo entre batidas

        # --- Controle do Agudo (Flash no GPIO 27) ---
        if pico_agudo or (ativo_agudo and nivel_agudo >= 0.5):
            fim_flash_agudo = max(fim_flash_agudo, agora + SUSTENTACAO_MINIMA_AGUDO)
            GPIO.output(TREBLE_PIN, GPIO.HIGH)
        elif agora >= fim_flash_agudo:
            GPIO.output(TREBLE_PIN, GPIO.LOW)

except KeyboardInterrupt:
    print("\nParando LEDs...")
finally:
    strobe_grave.stop()
    GPIO.cleanup()


