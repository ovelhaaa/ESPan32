# Referências Técnicas & Atribuições — Pocket Pan

Este documento registra explicitamente as origens, derivações matemáticas e referências arquiteturais empregadas no desenvolvimento do firmware **Pocket Pan / Metal Modal Synth** para a placa **TENSTAR TS-ESP32-S3**.

---

## 1. Síntese Modal & Ressonadores Físicos

### DaisySP (`electro-smith/DaisySP`)
- **Arquivos consultados:**
  - `Source/PhysicalModeling/modalvoice.h` / `modalvoice.cpp`
  - `Source/PhysicalModeling/resonator.h` / `resonator.cpp`
- **Autoria:** Ben Sergentanis, Emilie Gillet (Electrosmith Corp).
- **Licença:** MIT License.
- **Conceitos aproveitados:**
  - Modelo de excitação para síntese modal por percussão (pulso de impacto + burst curto de ruído filtrado).
  - Mapeamento não-linear perceptivo de velocidade MIDI para brilho (*brightness*), atenuação dinâmica e dureza do impacto transitório (*hardness*).
  - Estrutura de voz isolando o gerador de excitação (*Exciter*) do banco ressonador (*Resonator*).

### Mutable Instruments (`pichenettes/eurorack`)
- **Módulos consultados:**
  - `elements/dsp/resonator.h` / `resonator.cc`
  - `plaits/dsp/physical_modelling/modal_voice.h` / `modal_voice.cc`
  - `rings/dsp/`
- **Autoria:** Emilie Gillet.
- **Licença:** MIT License.
- **Conceitos aproveitados:**
  - Fórmulas de atenuação de modos superiores para controle de brilho e geometria.
  - Princípio de divisão de frequências e atenuação acima do limite de Nyquist.
  - Preservação da ressonância de instrumentos metálicos após o Note Off (Note Off representa apenas o fim do gesto, não silenciamento imediato).

### FAUST Physical Modeling Libraries
- **Bibliotecas consultadas:**
  - `physmodels.lib`
  - Modelos modais de sinos e pratos metálicos (`standardBell`, `churchBell`, `englishBell`).
- **Conceitos aproveitados:**
  - Formulação canônica do filtro IIR de 2ª ordem para ressonadores modais impulsivos:
    $$y[n] = \text{gain} \cdot x[n] + a_1 \cdot y[n-1] + a_2 \cdot y[n-2]$$
    onde:
    $$w = \frac{2\pi f}{f_s}$$
    $$r = \exp\left(\frac{\ln(0.001)}{T_{60} \cdot f_s}\right)$$
    $$a_1 = 2r \cos(w), \quad a_2 = -r^2$$
  - Proporções harmônicas essenciais do Handpan (Pantam):
    - Modo 0: Fundamental ($f$)
    - Modo 1: Split do fundamental (batimento acústico / shimmer metálico natural, $\sim 1.003f$)
    - Modo 2: Oitava ($2f$)
    - Modo 3: Quinta composta ($3f$)
    - Modos superiores: parciais não-harmônicos de contorno metálico.

---

## 2. Hardware TENSTAR TS-ESP32-S3 & Drivers Validados

### Orbit-Echo (`github.com/ovelhaaa/orbit-echo`)
- **Arquivos consultados:**
  - `targets/embedded_esp32s3/main/board_config.h`
  - `targets/embedded_esp32s3/main/audio_engine_esp32.cpp` / `.h`
  - `targets/embedded_esp32s3/main/hw/display.h`
  - `targets/embedded_esp32s3/main/app_main.cpp`
- **Conceitos e pinagens herdados:**
  - **Pinagem I2S validada:**
    - BCLK: **GPIO11**
    - LRCK/WS: **GPIO12**
    - DOUT/TX: **GPIO6**
    - MCLK: **GPIO10** (mantido ativamente em nível LOW para acionar o PLL interno do PCM5102 sem flutuação)
    - DIN/RX: **GPIO13** (desabilitado no MVP TX-only)
  - **Pinagem e sequência ST7789 TFT (240x135):**
    - MOSI: **GPIO35**, SCLK: **GPIO36**, CS: **GPIO7**, DC: **GPIO39**, RESET: **GPIO40**, BACKLIGHT: **GPIO45**, POWER: **GPIO21**.
    - Sequência crítica: Gate de alimentação GPIO21 em HIGH com delay de estabilização de 100 ms antes do reset do painel; color inversion ativada; swap XY; espelhamento vertical; offset/gap `(40, 53)`; barramento SPI2 a 40 MHz; framebuffer RGB565 em SRAM interna DMA-capable.

---

## 3. BLE MIDI & Transporte

### ESP32_Host_MIDI (`sauloverissimo/ESP32_Host_MIDI`) & ESP-IDF NimBLE
- **Conceitos aproveitados:**
  - Arquitetura de Central/Client BLE para recepção de controladores MIDI sem fio.
  - Abstração `MidiTransport` permitindo desacoplamento do motor de áudio.
  - Identificação de periféricos pelos UUIDs padrão da MIDI Association:
    - Service UUID: `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
    - Characteristic UUID: `7772E5DB-3868-4112-A1A9-F2669D106BF3`
  - Decodificação de timestamps BLE-MIDI de 13 bits (6 bits altos no header + 7 bits baixos no byte de timestamp).
  - Isolamento estrito de processamento: NimBLE Host alocado no **Core 1**, sem interferir na estabilidade de tempo real do áudio no **Core 0**.
