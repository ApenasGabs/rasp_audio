# -*- coding: utf-8 -*-
import time
import math

try:
    import RPi.GPIO as GPIO
    GPIO_DISPONIVEL = True
except (ImportError, RuntimeError):
    GPIO_DISPONIVEL = False
    print("[Aviso] RPi.GPIO não disponível neste ambiente. Operando em modo Mock/Simulação.")


class LuzPWM:
    """Controla LEDs com PWM por hardware (Strobe de Graves ou Brilho Variável)."""

    def __init__(self, pino_bcm, freq_hz=15, nome="LuzPWM"):
        self.pino = pino_bcm
        self.freq_hz = freq_hz
        self.nome = nome
        self.duty_cycle = 0.0
        self.fim_pulso = 0.0
        self.pwm = None

        if GPIO_DISPONIVEL:
            GPIO.setup(self.pino, GPIO.OUT)
            self.pwm = GPIO.PWM(self.pino, self.freq_hz)
            self.pwm.start(0)

    def definir_brilho(self, duty_cycle_pct):
        duty = max(0.0, min(100.0, float(duty_cycle_pct)))
        self.duty_cycle = duty
        if GPIO_DISPONIVEL and self.pwm:
            self.pwm.ChangeDutyCycle(duty)

    def pulsar(self, duty_cycle_pct=50.0, duracao_s=0.07):
        self.fim_pulso = max(self.fim_pulso, time.time() + duracao_s)
        self.definir_brilho(duty_cycle_pct)

    def atualizar(self, agora):
        if self.duty_cycle > 0.0 and agora >= self.fim_pulso:
            self.definir_brilho(0.0)

    def parar(self):
        if GPIO_DISPONIVEL and self.pwm:
            self.pwm.ChangeDutyCycle(0)
            self.pwm.stop()


class GloboRGB:
    """Gerencia as 3 cores do Globo Giratório (Vermelho, Verde, Azul) via ULN2003."""

    def __init__(self, pino_r, pino_g, pino_b, freq_hz=100):
        self.luz_r = LuzPWM(pino_r, freq_hz, "Globo_R")
        self.luz_g = LuzPWM(pino_g, freq_hz, "Globo_G")
        self.luz_b = LuzPWM(pino_b, freq_hz, "Globo_B")
        self.cor_atual = (0, 0, 0)

    def definir_rgb(self, r_pct, g_pct, b_pct):
        r = max(0.0, min(100.0, float(r_pct)))
        g = max(0.0, min(100.0, float(g_pct)))
        b = max(0.0, min(100.0, float(b_pct)))
        self.cor_atual = (r, g, b)

        self.luz_r.definir_brilho(r)
        self.luz_g.definir_brilho(g)
        self.luz_b.definir_brilho(b)

    def definir_paleta_contextual(self, modo, nivel_medios, tempo_s):
        """Modula as cores do globo suavemente baseado no modo musical e no ritmo dos médios."""
        brilho_base = max(20.0, nivel_medios * 100.0)

        if modo == "suave":
            # Tons frios relaxantes: Ciano, Azul e Roxo suave
            onda = (math.sin(tempo_s * 0.8) + 1.0) / 2.0
            r = 0.0
            g = onda * 40.0 * (brilho_base / 100.0)
            b = (1.0 - onda * 0.5) * 80.0 * (brilho_base / 100.0)
            self.definir_rgb(r, g, b)

        elif modo == "alta_energia":
            # Cores quentes e intensas com rotação eufórica: Magenta, Vermelho, Amarelo e Ciano
            onda_r = (math.sin(tempo_s * 3.0) + 1.0) / 2.0
            onda_g = (math.sin(tempo_s * 3.0 + 2.09) + 1.0) / 2.0
            onda_b = (math.sin(tempo_s * 3.0 + 4.18) + 1.0) / 2.0
            self.definir_rgb(
                onda_r * 100.0 * max(0.6, nivel_medios),
                onda_g * 80.0 * max(0.4, nivel_medios),
                onda_b * 100.0 * max(0.6, nivel_medios)
            )

        elif modo == "standby":
            self.definir_rgb(0, 0, 0)

        else:  # media_energia / fallback
            # Balanço dinâmico: transição suave entre Azul, Violeta e Dourado
            onda = (math.sin(tempo_s * 1.5) + 1.0) / 2.0
            r = onda * 70.0 * (brilho_base / 100.0)
            g = (1.0 - onda) * 40.0 * (brilho_base / 100.0)
            b = 85.0 * (brilho_base / 100.0)
            self.definir_rgb(r, g, b)

    def parar(self):
        self.luz_r.parar()
        self.luz_g.parar()
        self.luz_b.parar()


class ServoSG90:
    """Controla o Servo Motor SG90 com sinal PWM a 50Hz (Ângulos de 0° a 180°)."""

    def __init__(self, pino_bcm, angulo_min=15, angulo_max=165, freq_hz=50, nome="ServoSG90"):
        self.pino = pino_bcm
        self.angulo_min = angulo_min
        self.angulo_max = angulo_max
        self.freq_hz = freq_hz
        self.nome = nome

        self.angulo_atual = 90.0
        self.angulo_alvo = 90.0
        self.velocidade_graus_s = 120.0
        self.pwm = None

        if GPIO_DISPONIVEL:
            GPIO.setup(self.pino, GPIO.OUT)
            self.pwm = GPIO.PWM(self.pino, self.freq_hz)
            self.pwm.start(self._graus_para_duty(90.0))

    def _graus_para_duty(self, graus):
        """Converte ângulo de 0° a 180° para Duty Cycle percentual (2.5% a 12.5%)."""
        graus_clamp = max(0.0, min(180.0, float(graus)))
        return 2.5 + (graus_clamp / 180.0) * 10.0

    def definir_angulo(self, graus):
        """Move diretamente para o ângulo especificado."""
        self.angulo_alvo = max(self.angulo_min, min(self.angulo_max, float(graus)))
        self.angulo_atual = self.angulo_alvo
        if GPIO_DISPONIVEL and self.pwm:
            duty = self._graus_para_duty(self.angulo_atual)
            self.pwm.ChangeDutyCycle(duty)

    def definir_alvo_suave(self, graus, velocidade_graus_s=120.0):
        """Define o ângulo de destino com velocidade controlada."""
        self.angulo_alvo = max(self.angulo_min, min(self.angulo_max, float(graus)))
        self.velocidade_graus_s = max(10.0, float(velocidade_graus_s))

    def varrer_senoidal(self, freq_hz, angulo_min, angulo_max, tempo_atual):
        """Cria movimento pendular / de varredura contínuo suave."""
        seno = (math.sin(2.0 * math.pi * freq_hz * tempo_atual) + 1.0) / 2.0
        angulo = angulo_min + (seno * (angulo_max - angulo_min))
        self.definir_angulo(angulo)

    def atualizar(self, delta_tempo=0.025):
        """Interpolação suave não-bloqueante entre o ângulo atual e o alvo."""
        if abs(self.angulo_atual - self.angulo_alvo) > 0.5:
            passo = self.velocidade_graus_s * delta_tempo
            if self.angulo_atual < self.angulo_alvo:
                self.angulo_atual = min(self.angulo_alvo, self.angulo_atual + passo)
            else:
                self.angulo_atual = max(self.angulo_alvo, self.angulo_atual - passo)

            if GPIO_DISPONIVEL and self.pwm:
                duty = self._graus_para_duty(self.angulo_atual)
                self.pwm.ChangeDutyCycle(duty)

    def desativar_sinal(self):
        """Zera o duty cycle para que o servo pare de vibrar / emitir zumbido em repouso."""
        if GPIO_DISPONIVEL and self.pwm:
            self.pwm.ChangeDutyCycle(0)

    def parar(self):
        if GPIO_DISPONIVEL and self.pwm:
            self.pwm.ChangeDutyCycle(0)
            self.pwm.stop()
