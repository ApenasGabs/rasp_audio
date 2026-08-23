import socket
import json

PORTA_UDP = 5005

# Configura o Socket UDP para ouvir
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(socket, "SO_REUSEPORT"):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sock.bind(("", PORTA_UDP))

print("\033[2J\033[H", end="")  # Limpa a tela
print("Aguardando dados do Servidor UDP...")

try:
    while True:
        data, addr = sock.recvfrom(2048)
        payload = json.loads(data.decode("utf-8"))

        print("\033[H", end="")  # Volta ao topo do terminal
        print("===================== MONITOR DE ESPECTRO & PICOS =====================")
        print("Faixa            | Nível e Espectro                             | Status   | Valor / Limiar")
        print("---------------------------------------------------------------------------------------")

        if "faixas" in payload:
            faixas = payload["faixas"]
            for nome, info in faixas.items():
                nivel = float(info.get("nivel", 0.0))
                pico = bool(info.get("pico", False))
                ativo = bool(info.get("ativo", False))
                valor = int(info.get("valor", 0))
                limiar = int(info.get("limiar", 0))

                tamanho_barra = int(nivel * 35)
                barra = "█" * tamanho_barra
                if pico:
                    status = "💥 [PICO!]"
                elif ativo:
                    status = "⚡ [ATIVO]"
                else:
                    status = "   [     ]"

                nome_formatado = f"{nome.replace('_', ' ').title():<16}"
                print(
                    f"{nome_formatado} | {barra:<35} ({int(nivel * 100):3d}%) | {status} | {valor:7d} / {limiar:7d}"
                )

        else:
            # Modo de compatibilidade com servidor legado
            for nome, valor in payload.items():
                if isinstance(valor, (int, float)):
                    tamanho = min(35, int(valor / 20000))
                    barra = "█" * tamanho
                    nome_formatado = f"{nome.title():<16}"
                    print(f"{nome_formatado} | {barra:<35}        | [LEGADO] | {int(valor):7d}")

        print("---------------------------------------------------------------------------------------")
        print("(Pressione Ctrl+C para sair)")

except KeyboardInterrupt:
    print("\nSaindo do monitor de espectro...")


