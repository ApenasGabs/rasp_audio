# ⚡ ESP32 Nó 2: Projetor Laser Profissional DMX512 (RS-485)

Firmware independente dedicado exclusivamente ao **Projetor Laser DMX512 (16 Canais)**:
1. **🎛️ Mesa DMX Virtual (16 Canais):** Controle total de modo, cor, padrão, zoom, rotação e posição X/Y.
2. **🔬 Bancada de Testes / Scanner (0 a 255):** Teste de padrões individuais sem interferência de música, com auto-scan e simuladores de kick e voz.
3. **🎵 Sincronismo Musical Avançado:** Rotação de padrões no bumbo + Gatilho Silábico Fonema por Fonema na voz.

### 🔌 Pinagem (ESP32-C3 Super Mini):
* `GPIO 21`: DI (Data In) do Módulo MAX485
* `GPIO 10`: DE + RE do Módulo MAX485
* `Pino 3 XLR`: Borne A do MAX485
* `Pino 2 XLR`: Borne B do MAX485
* `Pino 1 XLR`: GND do MAX485
