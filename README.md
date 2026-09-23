# Pocket Pan — Metal Modal Synthesizer para TENSTAR TS-ESP32-S3

Firmware de sintetizador físico/modal para a placa **TENSTAR TS-ESP32-S3** (clone da Adafruit Feather ESP32-S3 TFT), controlado via **BLE MIDI** (otimizado para o controlador **M-VAVE SMC-PAD Pocket**) com saída de áudio digital por **I2S para DAC PCM5102** e display embutido **ST7789 240×135**.

---

## 1. Conexões de Hardware & Pinout Validado

A pinagem foi rigorosamente herdada da configuração validada do projeto `orbit-echo`:

### DAC I2S (PCM5102)
| Sinal PCM5102 | Pino ESP32-S3 | Função / Descrição |
|---|---|---|
| **BCK / BCLK** | **GPIO11** | Bit Clock I2S (48 kHz × 32 bits × 2 canais = 3,072 MHz) |
| **LRCK / WS**  | **GPIO12** | Word Select / Frame Clock (48 kHz) |
| **DIN**        | **GPIO6**  | Data Output do ESP32-S3 (I2S TX) |
| **SCK / MCLK** | **GPIO10** | **Obrigatório:** Nível LOW fixo (com pull-down) para ativar o PLL interno do PCM5102 |
| **VCC**        | 3.3V       | Alimentação |
| **GND**        | GND        | Terra comum |

> [!NOTE]
> O módulo PCM5102 não necessita de MCLK externo se o pino SCK estiver conectado ao terra ou a um pino em nível LOW. O firmware configura ativamente o **GPIO10** em nível LOW com pull-down para garantir que o PLL interno gere o clock sem ruídos ou flutuação.

### Display TFT On-Board (ST7789 240×135)
| Sinal Display | Pino ESP32-S3 | Descrição |
|---|---|---|
| **MOSI** | **GPIO35** | SPI Data |
| **SCLK** | **GPIO36** | SPI Clock (40 MHz) |
| **CS**   | **GPIO7**  | Chip Select |
| **DC**   | **GPIO39** | Data / Command |
| **RESET**| **GPIO40** | Reset |
| **BL**   | **GPIO45** | Backlight Enable |
| **PWR**  | **GPIO21** | Gate de alimentação do domínio TFT/STEMMA (acionado em HIGH com delay de 100 ms) |

---

## 2. Compilação e Testes

### Testes DSP no Desktop (Sem necessidade de ESP32)
O motor DSP é escrito em C++17 puro, desacoplado do ESP-IDF, e pode ser testado instantaneamente na máquina host:

```powershell
# Executa a suíte de testes unitários e gera o áudio output_handpan.wav:
.\build.ps1 -Action test
```

Os testes automatizados validam:
1. **Frequência modal:** Precisão de afinação da frequência de ressonância ($\le 1\text{ Hz}$ de erro).
2. **Decaimento $T_{60}$:** Verificação exata da atenuação exponencial para $-60\text{ dB}$ ($0,001$).
3. **Estabilidade e Nyquist:** Notas de 20 Hz a 8.000 Hz, amortecimento extremo, e garantia de desativação/fade dos modos superiores em vez de acúmulo artificial acima de Nyquist.
4. **Polifonia de 8 vozes & Voice Stealing:** Disparo rápido de notas, roubo de voz baseado na menor energia e exportação do arquivo `output_handpan.wav`.

### Compilar e Gravar no ESP32-S3

```powershell
# 1. Compilar o firmware completo:
.\build.ps1 -Action build

# 2. Gravar na placa (substitua pela sua porta COM):
.\build.ps1 -Action flash -Port COM3

# 3. Abrir o monitor serial:
.\build.ps1 -Action monitor -Port COM3
```

---

## 3. Modelo Acústico PAN (Handpan / Pantam)

O preset padrão `PAN` modela a assinatura física de um instrumento metálico do tipo Handpan:
- **Modo 0 ($f$):** Fundamental da nota.
- **Modo 1 ($1.0032f$):** Par desafinado do fundamental (*mode splitting*), gerando o característico batimento acústico (*beating* / *shimmer*).
- **Modo 2 ($2f$):** Oitava harmônica (vibração no eixo longitudinal do domo).
- **Modo 3 ($3f$):** Quinta composta (vibração no eixo transversal).
- **Modos 4..7:** Parciais superiores metálicos não-harmônicos de contorno.

### Dinâmica e Aftertouch (Choke):
- **Velocity:** Não controla apenas o volume! Modula o ataque da maceta, o brilho (*brightness*) do ruído de impacto e a dureza transiente (*hardness*).
- **Aftertouch:** Pressionar o pad após o ataque abafa fisicamente a nota (*palm mute* / *choke*), filtrado com suavização de 1 polo para eliminar ruído de zíper (*zipper noise*).
- **Note Off:** Não silencia abruptamente o instrumento; o metal continua ressoando naturalmente após a liberação do pad.

---

## 4. Telas do Display

1. **Status Geral:**
   - Nome do preset (`PAN`), nota raiz atual (`D3`), vozes ativas (`VOICES 3/8`).
   - Status da conexão BLE (`BLE SCAN` ou `BLE OK`), taxa de áudio (`AUDIO 48K`), carga de CPU em Core 0 (`CPU 18%`) e contagem de underruns (`UNDERRUN 0`).
2. **Monitor MIDI Diagnóstico (`MIDI MONITOR`):**
   - Exibe em tempo real: número da nota, velocidade, pressão de aftertouch, tipo da mensagem (`POLY AT` vs `CH AT`), timestamp BLE-MIDI de 13 bits e bytes hexadecimais brutos para análise do comportamento do controlador.
