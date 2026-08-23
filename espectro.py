import pyaudio
import numpy as np

p = pyaudio.PyAudio()

# --- BUSCA AUTOMÁTICA DO MICROFONE USB ---
INDEX_MICROFONE = None
CHANNELS = 1
RATE = 44100

for i in range(p.get_device_count()):
    info = p.get_device_info_by_index(i)
    # Se tem canal de entrada e tem "USB" no nome
    if info.get('maxInputChannels') > 0 and 'USB' in info.get('name', ''):
        INDEX_MICROFONE = i
        CHANNELS = int(info.get('maxInputChannels'))
        RATE = int(info.get('defaultSampleRate'))
        break

if INDEX_MICROFONE is None:
    print("ERRO: Nenhum microfone USB encontrado! Verifique a conexão.")
    p.terminate()
    exit()

print(f"Dispositivo encontrado! ID: {INDEX_MICROFONE} | Canais: {CHANNELS} | Rate: {RATE}Hz")

# --- CONFIGURAÇÕES DE ÁUDIO ---
CHUNK = 1024
FORMAT = pyaudio.paInt16
ESCALA_VISUAL = 20000 

stream = p.open(format=FORMAT, channels=CHANNELS, rate=RATE,
                input=True, input_device_index=INDEX_MICROFONE, frames_per_buffer=CHUNK)

try:
    while True:
        data = stream.read(CHUNK, exception_on_overflow=False)
        audio_data = np.frombuffer(data, dtype=np.int16)
        
        # Se o dispositivo for estéreo (2 canais), transforma em mono para a matemática funcionar
        if CHANNELS == 2:
            audio_data = audio_data[0::2] 
            
        fft_data = np.abs(np.fft.rfft(audio_data))
        
        faixas = {
            "1. Graves (Batida)    ": np.mean(fft_data[0:6]),
            "2. Medios-Graves      ": np.mean(fft_data[6:15]),
            "3. Medios (Vozes)     ": np.mean(fft_data[15:46]),
            "4. Agudos (Pratos)    ": np.mean(fft_data[46:140]),
            "5. Super Agudos       ": np.mean(fft_data[140:500])
        }
        
        print("\033[H", end="") # Volta ao topo da tela
        print("=== ANALISADOR DE ESPECTRO (Pressione Ctrl+C para sair) ===\n")
        
        for nome, valor in faixas.items():
            tamanho = int(valor / ESCALA_VISUAL)
            tamanho = min(tamanho, 50)
            barra = "█" * tamanho
            print(f"{nome} | {barra:<50} | Valor: {int(valor):8d}")
            
except KeyboardInterrupt:
    print("\n\nSaindo...")
finally:
    stream.stop_stream()
    stream.close()
    p.terminate()
