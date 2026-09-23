# Arquitetura do Firmware — Pocket Pan / Metal Modal Synth

O **Pocket Pan** é um sintetizador físico modal projetado para a placa **TENSTAR TS-ESP32-S3** (clone da Adafruit Feather ESP32-S3 TFT), controlado via **BLE MIDI** (focado no controlador M-VAVE SMC-PAD Pocket) e com saída de áudio digital **I2S para DAC PCM5102** em 48 kHz / 32 bits stereo.

---

## 1. Distribuição de Núcleos (Dual-Core)

O ESP32-S3 possui dois núcleos Xtensa LX7 a 240 MHz. A arquitetura isola completamente o processamento de áudio das tarefas de rede e interface:

```text
               ┌────────────────────────────────────────────────────────┐
               │                      ESP32-S3                          │
               │                                                        │
               │   CORE 0: ÁUDIO & DSP (Tempo Real Estrito)             │
               │   ├── Prioridade: configMAX_PRIORITIES - 2 (23)        │
               │   ├── I2S DMA TX Driver (driver/i2s_std.h)             │
               │   ├── Consumo da fila SPSC de eventos MIDI             │
               │   ├── Voice Allocator (8 vozes modais polifônicas)     │
               │   ├── Modal Resonator Bank (2ª ordem IIR)              │
               │   ├── Exciter (Impacto de maceta + ruído filtrado)     │
               │   └── Master Mixdown, Soft-Clipper & Saturação 32-bit  │
               │                                                        │
               │                    ▲                                   │
               │                    │ Fila Lock-Free SPSC (MidiEvent)   │
               │                    ▼                                   │
               │                                                        │
               │   CORE 1: UI & COMUNICAÇÃO (Não Bloqueante)            │
               │   ├── Prioridade: 3 (UI) / 5 (NimBLE Host)             │
               │   ├── NimBLE Bluetooth Central (BLE MIDI Client)       │
               │   ├── Leitura de GATT Notifications (UUIDs MIDI)       │
               │   ├── Decodificação de timestamps de 13 bits           │
               │   ├── ST7789 TFT Driver (240x135 @ 30 Hz via SPI2)     │
               │   ├── Modo Diagnóstico de MIDI (MIDI MONITOR)          │
               │   └── Telemetria de sistema (CPU %, underruns)         │
               └────────────────────────────────────────────────────────┘
```

---

## 2. Invariantes de Tempo Real no Core 0

Dentro da task e do callback de áudio é **terminantemente proibido**:
1. Chamadas a `malloc`, `free`, `new` ou `delete`.
2. Logging e prints de depuração (`printf`, `ESP_LOG*`).
3. Operações com o display SPI ou o controlador Bluetooth NimBLE.
4. Mutexes bloqueantes (toda a passagem Core 1 $\rightarrow$ Core 0 é realizada via fila circular SPSC lock-free atômica).
5. Acessos a memória PSRAM por sample individual.

Todos os buffers de áudio, coeficientes de filtro e estados de voz são pré-alocados na inicialização em **SRAM interna**.

---

## 3. Gestão de Memória: SRAM Interna vs. PSRAM

| Estrutura | Localização de Memória | Tamanho | Motivo Técnico |
|---|---|---|---|
| Buffer DMA I2S (TX) | SRAM Interna (`MALLOC_CAP_DMA`) | 1.024 bytes (128 frames stereo 32-bit) | Requisito de barramento DMA |
| 6 Descritores DMA | SRAM Interna | ~144 bytes | Requisito de hardware GDMA |
| Estados das 8 Vozes Modais (`z1`, `z2`, coeficientes `a1`, `a2`) | SRAM Interna (BSS estático) | ~2.5 KB | Acesso ultra-rápido a cada sample sem latência de cache PSRAM |
| Fila SPSC de Eventos MIDI | SRAM Interna (BSS estático) | ~1.5 KB | Acesso atômico sem contenção entre núcleos |
| Framebuffer ST7789 (240x135 RGB565) | SRAM Interna (`MALLOC_CAP_DMA`) | 64.800 bytes | Transferência DMA de bitmap SPI2 a 40 MHz |
| Presets e Tabelas Não Críticas | Flash / PSRAM | Sob demanda | Não interferem na latência determinística do Core 0 |

---

## 4. Caminho de Áudio I2S & DAC PCM5102

- **Taxa de amostragem:** 48.000 Hz
- **Slot I2S:** 32 bits
- **Canais:** 2 (Stereo)
- **Tamanho de bloco:** 128 frames ($\sim 2,667\text{ ms}$ por bloco)
- **Descritores DMA:** 6 buffers de 1.024 bytes ($\sim 16\text{ ms}$ de buffer total no driver GDMA)
- **Pinagem Validada:**
  - BCLK: **GPIO11**
  - LRCK / WS: **GPIO12**
  - DOUT / TX: **GPIO6**
  - DIN / RX: **GPIO13** (desabilitado no MVP)
- **Requisito Crítico do PCM5102:**
  - O pino MCLK/SCK do PCM5102 não pode ficar fisicamente flutuando.
  - O pino **GPIO10** do ESP32-S3 é configurado com saída forçada em **nível lógico LOW (com pull-down interno)** para acionar o gerador de clock / PLL interno do PCM5102.

---

## 5. DSP Modal & Prevenção de Acúmulo em Nyquist

Cada voz modal contém um banco de até 10 modos de 2ª ordem implementados como ressonadores IIR de forma direta:
$$y[n] = \text{gain} \cdot x[n] + a_1 \cdot y[n-1] + a_2 \cdot y[n-2]$$
onde:
$$w = \frac{2\pi f}{f_s}, \quad r = \exp\left(\frac{-6.907755}{T_{60} \cdot f_s}\right)$$
$$a_1 = 2r \cos(w), \quad a_2 = -r^2$$

### Tratamento Acima de Nyquist
Ao sintetizar notas agudas ou modos parciais elevados, **modos acima do limite de Nyquist não são clampeados para $0,48 f_s$** (o que geraria acúmulo artificial de energia metálica em uma única banda de alta frequência).
Em vez disso:
- Modos entre $0,40 f_s$ (19,2 kHz) e $0,48 f_s$ (23,04 kHz) sofrem atenuação linear progressiva (*fade-out suave*).
- Modos com frequência $\ge 0,48 f_s$ são explicitamente desativados (`active = false`, ganho nulo).

### Modelo Acústico PAN (Handpan / Pantam)
- **Modo 0 ($f$):** Fundamental (Ding/Tonefield)
- **Modo 1 ($1.0032f$):** Split doublet do fundamental para batimento (*beating*) físico natural
- **Modo 2 ($2.0f$):** Oitava longitudinal
- **Modo 3 ($3.0f$):** Quinta composta transversal
- **Modos 4..7:** Parciais metálicos não-harmônicos de contorno com decaimentos rápidos

---

## 6. Alocador de Vozes & Aftertouch

- **Polifonia:** 8 vozes simultâneas.
- **Note Off:** Libera apenas o gesto, mantendo o ressonador vibrando até o fim natural do decaimento físico metálico.
- **Voice Stealing:** Caso todas as 8 vozes estejam ativas ao receber um Note On:
  1. Identifica a voz liberada com menor energia acumulada.
  2. Aplica micro-fade de ~2.5 ms para evitar transientes / cliques audíveis.
  3. Reatribui a voz à nova nota.
- **Aftertouch (Choke):**
  - Mapeado para amortecimento dinâmico (*damping*) com filtro passa-baixas de 1 polo ($\tau \approx 20\text{ ms}$) para evitar ruído de zíper (*zipper noise*).

---

## 7. BLE MIDI & Modo Diagnóstico

O ESP32-S3 atua como **BLE Central** conectando-se automaticamente ao **M-VAVE SMC-PAD Pocket**:
- **UUID de Serviço:** `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
- **UUID de Característica:** `7772E5DB-3868-4112-A1A9-F2669D106BF3`
- **Timestamps:** Decodifica e preserva o timestamp nativo de 13 bits do padrão BLE-MIDI.
- **Tela MIDI MONITOR:** Permite inspecionar em tempo real se o controlador transmite *Polyphonic Key Pressure* ou *Channel Pressure*, além de registrar bytes hexadecimais brutos.
