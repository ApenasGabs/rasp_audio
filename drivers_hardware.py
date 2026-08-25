# -*- coding: utf-8 -*-
import time
import math

try:
    import RPi.GPIO as GPIO
    GPIO_DISPONIVEL = True
except (ImportError, RuntimeError):
    GPIO_DISPONIVEL = False
    print("[Aviso] RPi.GPIO não disponível neste ambiente. Operando em modo Mock/Simulação.")


class LuzDigital:
    """Controla diodos laser ou LEDs digitais via ULN2003 (Liga/Desliga com sustentação)."""

    def __init__(self, pino_bcm, nome="LuzDigital"):
        self.pino = pino_bcm
        self.nome = nome
        self.estado = False
        self.fim_pulso = 0.0

        if GPIO_DISPONIVEL:
            GPIO.setup(self.pino, GPIO.OUT)
            GPIO.output(self.pino, GPIO.LOW)

    def ligar(self):
        self.estado = True
        if GPIO_DISPONIVEL:
            GPIO.output(self.pino, GPIO.HIGH)

    def desligar(self):
        self.estado = False
        if GPIO_DISPONIVEL:
            GPIO.output(self.pino, GPIO.LOW)

    def pulsar(self, duracao_s=0.06):
        """Liga e agenda o desligamento automático após duracao_s."""
        self.fim_pulso = max(self.fim_pulso, time.time() + duracao_s)
        self.ligar()

    def atualizar(self, agora):
        """Desliga se o tempo de sustentação expirou."""
        if self.estado and agora >= self.fim_pulso:
            self.desligar()


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


class MotorBidirecional:
    """Controla motor DC bidirecional na Ponte H (Avanço, Recuo, Rampa de Aceleração e Inversão)."""

    def __init__(self, pino_in1, pino_in2, freq_hz=1000, nome="MotorBidirecional"):
        self.pino_in1 = pino_in1
        self.pino_in2 = pino_in2
        self.freq_hz = freq_hz
        self.nome = nome

        self.direcao_atual = 0    # 1 (frente), -1 (trás), 0 (parado)
        self.direcao_alvo = 1
        self.velocidade_atual = 0.0
        self.velocidade_alvo = 0.0

        self.pwm_in1 = None
        self.pwm_in2 = None

        if GPIO_DISPONIVEL:
            GPIO.setup(self.pino_in1, GPIO.OUT)
            GPIO.setup(self.pino_in2, GPIO.OUT)
            self.pwm_in1 = GPIO.PWM(self.pino_in1, self.freq_hz)
            self.pwm_in2 = GPIO.PWM(self.pino_in2, self.freq_hz)
            self.pwm_in1.start(0)
            self.pwm_in2.start(0)

    def definir_movimento(self, velocidade_pct, direcao=1):
        """Define a velocidade desejada (0-100%) e direção (1=frente, -1=trás)."""
        self.velocidade_alvo = max(0.0, min(100.0, float(velocidade_pct)))
        if direcao in [1, -1]:
            self.direcao_alvo = direcao

    def inverter_direcao(self):
        """Inverte o sentido de rotação atual (efeito vai e volta / rebate na batida)."""
        self.direcao_alvo = -1 if self.direcao_alvo == 1 else 1

    def oscilar_senoidal(self, freq_hz, velocidade_max_pct, tempo_atual):
        """Cria movimento contínuo e suave de vai-e-vem (sweep / oscilação rítmica)."""
        seno = math.sin(2.0 * math.pi * freq_hz * tempo_atual)
        direcao = 1 if seno >= 0 else -1
        velocidade = abs(seno) * velocidade_max_pct
        self.definir_movimento(velocidade, direcao)

    def atualizar(self, delta_tempo=0.025):
        """Aplica rampa suave de aceleração e proteção ao inverter o sentido da Ponte H."""
        # Se precisa mudar de direção, primeiro desacelera até 0 antes de inverter
        mudando_direcao = (self.direcao_atual != self.direcao_alvo and self.velocidade_atual > 5.0)

        taxa_rampa = 120.0 * delta_tempo  # 120% por segundo

        if mudando_direcao:
            self.velocidade_atual = max(0.0, self.velocidade_atual - (taxa_rampa * 1.5))
        else:
            self.direcao_atual = self.direcao_alvo
            if self.velocidade_atual < self.velocidade_alvo:
                self.velocidade_atual = min(self.velocidade_alvo, self.velocidade_atual + taxa_rampa)
            elif self.velocidade_atual > self.velocidade_alvo:
                self.velocidade_atual = max(self.velocidade_alvo, self.velocidade_atual - taxa_rampa)

        # Envia os sinais PWM para a Ponte H
        if GPIO_DISPONIVEL and self.pwm_in1 and self.pwm_in2:
            if self.direcao_atual == 1:
                self.pwm_in2.ChangeDutyCycle(0)
                self.pwm_in1.ChangeDutyCycle(self.velocidade_atual)
            elif self.direcao_atual == -1:
                self.pwm_in1.ChangeDutyCycle(0)
                self.pwm_in2.ChangeDutyCycle(self.velocidade_atual)
            else:
                self.pwm_in1.ChangeDutyCycle(0)
                self.pwm_in2.ChangeDutyCycle(0)

    def parar(self):
        if GPIO_DISPONIVEL and self.pwm_in1 and self.pwm_in2:
            self.pwm_in1.ChangeDutyCycle(0)
            self.pwm_in2.ChangeDutyCycle(0)
            self.pwm_in1.stop()
            self.pwm_in2.stop()
