# -*- coding: utf-8 -*-
"""
Script de Teste de Comunicação UDP Sem Fio para o ESP32-C3 Super Mini
Envia pacotes de teste via Wi-Fi para validar o recebimento no ESP32.
"""
import socket
import json
import time

PORTA_UDP = 5005
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

print("=" * 60)
print(" 📡 ENVIANDO TESTE SEM FIO (BROADCAST UDP) PARA O ESP32-C3")
print("=" * 60)

# Pacote 1: Alta Energia (Servo oscilando e Strobe piscando)
print("1. Enviando modo ALTA ENERGIA (Servo deve varrer e Strobe piscar)...")
for i in range(20):
    payload = {
        "tipo": "audio",
        "faixas": {
            "graves": {"nivel": 0.85, "pico": (i % 4 == 0), "ativo": True},
            "medios": {"nivel": 0.70, "pico": False, "ativo": True},
            "agudos": {"nivel": 0.60, "pico": False, "ativo": False}
        }
    }
    msg = json.dumps(payload).encode("utf-8")
    sock.sendto(msg, ("<broadcast>", PORTA_UDP))
    time.sleep(0.1)

# Pacote 2: Modo Suave (Cores frias Azul/Ciano)
print("2. Enviando modo SUAVE (Globo RGB deve ficar Azul/Ciano)...")
payload_sp = {
    "tipo": "spotify",
    "tocando": True,
    "energia": 0.30,
    "danceabilidade": 0.40,
    "modo_sugerido": "suave"
}
sock.sendto(json.dumps(payload_sp).encode("utf-8"), ("<broadcast>", PORTA_UDP))

for i in range(15):
    payload = {
        "tipo": "audio",
        "faixas": {
            "graves": {"nivel": 0.20, "pico": False, "ativo": False},
            "medios": {"nivel": 0.50, "pico": False, "ativo": True},
            "agudos": {"nivel": 0.10, "pico": False, "ativo": False}
        }
    }
    sock.sendto(json.dumps(payload).encode("utf-8"), ("<broadcast>", PORTA_UDP))
    time.sleep(0.1)

print("=" * 60)
print(" ✅ TESTE CONCLUÍDO! Se o ESP32 respondeu, a rede Wi-Fi está 100%!")
print("=" * 60)
