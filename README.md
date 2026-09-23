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
| **BOOT** | **GPIO0**  | Botão de boot físico usado para alternar entre Status e Monitor MIDI |

---

## 2. Compilação e Testes

### Testes Host no Desktop (Sem necessidade de ESP32)
O motor DSP e o parser BLE MIDI são escritos em C++17 puro, desacoplados do ESP-IDF, e podem ser testados instantaneamente na máquina host:

```powershell
# Executa a suíte de testes unitários (DSP + BLE MIDI Parser) e gera os arquivos WAV:
.\build.ps1 -Action test
```

Os testes automatizados validam:
1. **Frequência modal:** Precisão de afinação da frequência de ressonância ($\le 1\text{ Hz}$ de erro).
2. **Decaimento $T_{60}$:** Verificação exata da atenuação exponencial para $-60\text{ dB}$ ($0,001$).
3. **Mode Splitting (Par Desafinado):** Confirmação de que o detune ($+0.0032$) é aplicado estritamente uma vez, gerando o batimento acústico natural de $\sim 0,94\text{ Hz}$.
4. **Physical Restrike (Retrigger na Mesma Nota):** A nova excitação acumula energia no corpo que já vibra, preservando a cauda acústica sem reinicialização forçada.
5. **Declicked Voice Stealing:** Transição de roubo de voz suavizada com cauda de micro-fade linear de 32 amostras ($\sim 0,67\text{ ms}$), eliminando estalos de descontinuidade em rajadas de 9+ notas.
6. **Amortecimento Multivoz Independente:** Suporte a Poly Pressure independente por voz sem contadores globais compartilhados, e Channel Pressure atuando globalmente.
7. **Normalização Modal:** Normalização dos filtros de ressonância ($b_0 = \sin(w) \cdot \text{gain} \cdot \text{bankNorm}$), garantindo que o pico de ataque não dependa arbitrariamente de $T_{60}$ ou da frequência.
8. **Calibração de Velocidade & Headroom:** Faixa dinâmica expressiva de velocity 20 a 127 com limiter atuando estritamente como margem de segurança analógica (100% linear para notas individuais até vel 110).
9. **Parser BLE MIDI:** Decodificação de pacotes BLE GATT com timestamps de 13 bits, Running Status, mensagens System Real-Time intercaladas (`0xF8`) e recuperação robusta de pacotes truncados.
10. **Exportação de WAVs Comparativos:** Gera `pan_D3_vel40.wav`, `pan_D3_vel90.wav`, `pan_D3_vel127.wav`, `pan_D4_double_strike.wav` e `pan_chord.wav`.

### Compilar e Gravar no ESP32-S3

```powershell
# 1. Compilar o firmware completo com idf.py:
.\build.ps1 -Action build

# 2. Gravar na placa (substitua pela sua porta COM):
.\build.ps1 -Action flash -Port COM3

# 3. Abrir o monitor serial:
.\build.ps1 -Action monitor -Port COM3
```

---

## 3. Modelo Acústico PAN (Handpan / Pantam)

O preset padrão `PAN` modela a assinatura física de um instrumento metálico do tipo Handpan:
- **Modo 0 ($f$):** Fundamental da nota (`ratio = 1.0, detune = 0.0`).
- **Modo 1 ($1.0032f$):** Par desafinado do fundamental (`ratio = 1.0, detune = 0.0032`), gerando o característico batimento acústico (*beating* / *shimmer*).
- **Modo 2 ($2f$):** Oitava harmônica (vibração no eixo longitudinal do domo).
- **Modo 3 ($3f$):** Quinta composta (vibração no eixo transversal).
- **Modos 4..7:** Parciais superiores metálicos não-harmônicos de contorno.

### Dinâmica e Aftertouch (Choke):
- **Velocity:** Modula a amplitude do impacto, a dureza transiente da maceta (de 14 amostras em toques suaves a 3 amostras em golpes firmes) e a abertura do filtro de brilho (700 Hz a 12 kHz).
- **Aftertouch:** Pressionar o pad após o ataque abafa fisicamente a nota (*palm mute* / *choke*), filtrado com suavização de 1 polo independente por voz para eliminar ruído de zíper (*zipper noise*).
- **Same-Note Restrike:** Golpear novamente a mesma nota adiciona energia acústica à ressonância prévia em vez de resetar o banco modal.
- **Note Off:** Não silencia abruptamente o instrumento; o metal continua ressoando naturalmente após a liberação do pad.

---

## 4. Telas do Display

O botão físico **BOOT (GPIO0)** permite alternar a qualquer momento entre as duas telas:

1. **Status Geral:**
   - Nome do preset (`PAN`), nota raiz atual (`D3`), vozes ativas (`VOICES 3/8`).
   - Status da conexão BLE (`BLE OK` ou `BLE SCAN`), taxa de áudio (`AUDIO 48K`), carga de CPU em Core 0 (`CPU %`) e estouros de deadline (`DLINE`).
2. **Monitor MIDI Diagnóstico (`MIDI DIAGNOSTIC MONITOR`):**
   - Exibe em tempo real: número da nota, velocidade, pressão de aftertouch, tipo da mensagem (`NOTE ON`, `NOTE OFF`, `POLY AT`, `CH AT`), timestamp BLE-MIDI de 13 bits, bytes hexadecimais brutos e telemetria da fila MIDI (`HWM` e `DROP`).
   - Ideal para bring-up e conferência rápida com o controlador M-VAVE SMC-PAD Pocket.
