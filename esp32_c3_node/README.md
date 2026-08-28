# 📡 Nó Receptor Sem Fio: ESP32-C3 Super Mini (DMX512 + Strobe + Globo RGB + Servo SG90)

Este firmware transforma o **ESP32-C3 Super Mini** em um receptor de iluminação inteligente sem fio via Wi-Fi UDP e controlador **DMX512 profissional** via módulo RS-485.

---

## 🔌 Pinagem Completa no ESP32-C3 Super Mini

| Dispositivo / Protocolo | Função | Pino ESP32-C3 | Driver | Tipo de Sinal |
| :--- | :--- | :--- | :--- | :--- |
| **⚪ Strobe Branco** | Graves / Kicks | **GPIO 0** | ULN2003 | PWM 15Hz (Strobe) |
| **🔴 Globo - R** | Cor Vermelha Globo | **GPIO 4** | ULN2003 | PWM (Cores) |
| **🟢 Globo - G** | Cor Verde Globo | **GPIO 5** | ULN2003 | PWM (Cores) |
| **🔵 Globo - B** | Cor Azul Globo | **GPIO 6** | ULN2003 | PWM (Cores) |
| **🤖 Servo SG90** | Movimento do Globo | **GPIO 7** | Direto (Sinal PWM) | PWM 50Hz (0° a 180°) |
| **🎛️ DMX512 TX** | Projetor Laser (Dados) | **GPIO 21** | Módulo RS-485 (DI) | UART 250 kbps (DMX512) |
| **🎛️ DMX512 Enable** | Habilita Transmissão | **GPIO 10** | Módulo RS-485 (DE+RE) | Digital HIGH |
| **💡 Status Wi-Fi** | LED Onboard | **GPIO 8** | Interno | Pisca (Tráfego de Rede) |

---

## ⚡ Esquema de Conexão do Módulo RS-485 (MAX485)

```
[ ESP32-C3 Super Mini ]                [ Módulo MAX485 ]           [ Conector XLR do Laser ]
      5V (ou 3.3V)        ──────────►        VCC
          GND             ──────────►        GND            ─────►  Pino 1 (GND / Malha)
     GPIO 21 (TX)         ──────────►      DI (Data In)
     GPIO 10 (Enable)     ──────────►      DE + RE
                                             A (DMX+)       ─────►  Pino 3 (DMX +)
                                             B (DMX-)       ─────►  Pino 2 (DMX -)
```

---

## 🎭 Canais DMX do Projetor Laser (16 Canais):

| Canal | Função | Comportamento na Música |
| :--- | :--- | :--- |
| **CH1** | Modo de Operação | `0` (Blackout em Standby) / `50` (Manual Console ao tocar) |
| **CH2** | Velocidade | `128` |
| **CH3** | Seleção de Cor | Ciano/Azul no calmo $ightarrow$ Multicolorido dinâmico no drop |
| **CH4** | Velocidade do Fluxo | Acompanha a energia musical |
| **CH5** | Padrão Gráfico (Gobo) | **Troca de figura geométrica a cada batida forte de grave (`pico_grave`)** |
| **CH6** | Tamanho do Desenho | **Zoom expansivo que explode nos drops** |
| **CH7** | Dimensionamento | Zoom dinâmico contínuo |
| **CH8** | Rotação Central | Rotação 3D acelerada em alta energia |
| **CH9/10**| Flip H / V | Inversão espacial nos ataques |
| **CH11/12**| Posição X / Y | Varredura de feixes no ambiente |
| **CH13** | Ondulação X | Onda senoidal acompanhando médios e sintetizadores |
| **CH14** | Traçado Gradual | Efeito de desenho de feixes no ar |
| **CH15** | Scan Speed | `255` (Máxima nitidez dos galvanômetros) |
| **CH16** | Modo de Exibição | Destaques luminosos no drop |

---

## 🛠️ Como Gravar o Firmware:
1. Abra `esp32_c3_node.ino` na Arduino IDE.
2. Altere `WIFI_SSID` e `WIFI_PASS` com os dados do seu Wi-Fi.
3. Selecione a placa: **ESP32C3 Dev Module** e faça o **Upload**.
