import pyaudio
import numpy as np
import RPi.GPIO as GPIO

# --- CONFIGURAÇÕES DE GPIO ---
BASS_PIN = 17   # Pino para luz dos graves
TREBLE_PIN = 27 # Pino para luz dos agudos

GPIO.setmode(GPIO.BCM)
GPIO.setwarnings(False)
GPIO.setup(BASS_PIN, GPIO.OUT)
GPIO.setup(TREBLE_PIN, GPIO.OUT)

# --- CONFIGURAÇÕES DE ÁUDIO ---
CHUNK = 1024       # Quantidade de amostras de áudio lidas por vez
FORMAT = pyaudio.paInt16
CHANNELS = 1
RATE = 44100       # Taxa de amostragem (Hz)

# Inicializa o PyAudio
p = pyaudio.PyAudio()

# Abre o fluxo de entrada de áudio (Microfone/Placa USB)
stream = p.open(format=FORMAT,
                channels=CHANNELS,
                rate=RATE,
                input=True,
                input_device_index=1,
		frames_per_buffer=CHUNK)

# --- LIMIARES DE SENSIBILIDADE ---
# Você precisará ajustar esses valores dependendo da altura da música 
# e da sensibilidade do seu microfone.
LIMIT_BASS = 1500000 
LIMIT_TREBLE = 10000 

print("Escutando... Pressione Ctrl+C para parar.")

try:
    while True:
        # 1. Lê os dados crus do áudio
        data = stream.read(CHUNK, exception_on_overflow=False)
        
        # 2. Converte os dados em um array do NumPy (números que podemos calcular)
        audio_data = np.frombuffer(data, dtype=np.int16)
        
        # 3. Aplica a Transformada de Fourier (FFT) para separar as frequências
        fft_data = np.abs(np.fft.rfft(audio_data))
        
        # O resultado do FFT divide as frequências em "fatias" (bins).
        # Cada fatia tem aprox 43 Hz (RATE / CHUNK).
        
        # 4. Separação: Graves (aprox 20Hz a 250Hz) -> Bins de 0 a 6
        bass_intensity = np.mean(fft_data[0:6])
        
        # 5. Separação: Agudos (aprox 4000Hz a 8000Hz) -> Bins de 93 a 186
        treble_intensity = np.mean(fft_data[93:186])
        
        # 6. Lógica para acender/apagar os LEDs
        if bass_intensity > LIMIT_BASS:
            GPIO.output(BASS_PIN, GPIO.HIGH)
        else:
            GPIO.output(BASS_PIN, GPIO.LOW)
            
        if treble_intensity > LIMIT_TREBLE:
            GPIO.output(TREBLE_PIN, GPIO.HIGH)
        else:
            GPIO.output(TREBLE_PIN, GPIO.LOW)

except KeyboardInterrupt:
    print("Parando...")

finally:
    # Limpa as configurações de GPIO e desliga os componentes de áudio
    GPIO.cleanup()
    stream.stop_stream()
    stream.close()
    p.terminate()
