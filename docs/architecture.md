# Arquitetura do Firmware — Pocket Pan / Metal Modal Synth

O **Pocket Pan** é um sintetizador físico modal projetado para a placa **TENSTAR TS-ESP32-S3** (clone da Adafruit Feather ESP32-S3 TFT), controlado via **BLE MIDI** (focado no controlador M-VAVE SMC-PAD Pocket) e com saída de áudio digital **I2S para DAC PCM5102** em 48 kHz / 32 bits stereo.

---

## 1. Distribuição de Núcleos (Dual-Core) & Concorrência

O ESP32-S3 possui dois núcleos Xtensa LX7 a 240 MHz. A arquitetura isola completamente o processamento de áudio das tarefas de rede e interface:

```text
               ┌────────────────────────────────────────────────────────┐
               │                      ESP32-S3                          │
               │                                                        │
               │   CORE 0: ÁUDIO & DSP (Tempo Real Estrito)             │
               │   ├── Prioridade: configMAX_PRIORITIES - 2 (23)        │
               │   ├── I2S DMA TX Driver (driver/i2s_std.h)             │
               │   ├── Consumo da fila SPSC lock-free de eventos MIDI   │
               │   ├── Voice Allocator (8 vozes com restrike e steal)   │
               │   ├── Modal Resonator Bank (2ª ordem IIR normalizado)  │
               │   ├── Exciter (Impacto de maceta + ruído filtrado)     │
               │   ├── Limiter analógico de segurança (linear até 0.85) │
               │   └── Publicação atômica de AudioTelemetrySnapshot     │
               │                                                        │
               │                    ▲                     ▲             │
               │       SpscMidiQueue│                     │Snapshot     │
               │       (Core 1 → 0) │                     │(Core 0 → 1) │
               │                    ▼                     ▼             │
               │                                                        │
               │   CORE 1: UI & COMUNICAÇÃO (Não Bloqueante)            │
               │   ├── Prioridade: 3 (UI) / 5 (NimBLE Host)             │
               │   ├── NimBLE Bluetooth Central (BLE MIDI Client)       │
               │   ├── Descoberta explícita de serviço, char e CCCD     │
               │   ├── Parser BLE-MIDI com timestamps de 13 bits        │
               │   ├── ST7789 TFT Driver (240x135 @ 30 Hz via SPI2)     │
               │   ├── Formatação de texto e nomes de nota (fora Core 0)│
               │   ├── Monitor Diagnóstico de MIDI (via botão BOOT)     │
               │   └── Telemetria (CPU %, deadline misses, métricas)    │
               └────────────────────────────────────────────────────────┘
```

---

## 2. Invariantes de Tempo Real no Core 0

Dentro da task e do callback de áudio é **terminantemente proibido**:
1. Chamadas a `malloc`, `free`, `new` ou `delete`.
2. Logging e prints de depuração (`printf`, `ESP_LOG*`).
3. Formatação de strings (`snprintf`, lookup de nomes de nota).
4. Operações com o display SPI ou o stack Bluetooth NimBLE.
5. Mutexes bloqueantes (a comunicação Core 1 $\rightarrow$ Core 0 é realizada via fila circular SPSC lock-free atômica; e Core 0 $\rightarrow$ Core 1 via `AudioTelemetryPublisher` com double-buffer atômico).
6. Acessos a memória PSRAM por sample individual.

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
- **Descritores DMA:** 6 buffers de 1.024 bytes ($\sim 16\text{ ms}$ de capacidade total no driver GDMA)
- **Pinagem Validada:**
  - BCLK: **GPIO11**
  - LRCK / WS: **GPIO12**
  - DOUT / TX: **GPIO6**
  - DIN / RX: **GPIO13** (desabilitado no MVP)
- **Requisito do PCM5102:**
  - O pino MCLK/SCK do PCM5102 não pode ficar fisicamente flutuando.
  - O pino **GPIO10** do ESP32-S3 é configurado com saída forçada em **nível lógico LOW (com pull-down interno)** para acionar o gerador de clock / PLL interno do PCM5102.

---

## 5. DSP Modal, Normalização e Vozes

Cada voz modal contém um banco de até 10 modos de 2ª ordem implementados como ressonadores IIR de forma direta:
$$y[n] = b_0 \cdot x[n] + a_1 \cdot y[n-1] + a_2 \cdot y[n-2]$$
onde:
$$w = \frac{2\pi f}{f_s}, \quad r = \exp\left(\frac{-6.907755}{T_{60} \cdot f_s}\right)$$
$$a_1 = 2r \cos(w), \quad a_2 = -r^2$$

### Normalização do Ganho Modal
A resposta ao impulso de um ressonador de 2 polos possui envelope de pico:
$$\hat{h} \approx \frac{b_0}{\sin(w)}$$
Ao definir $b_0 = \sin(w) \cdot \text{modeGain} \cdot \text{bankNorm}$, onde $\text{bankNorm} = 1 / \sqrt{\sum \text{gain}_k^2}$:
- O pico de ataque do impulso independe da frequência $w$ e independe do tempo de decaimento $T_{60}$.
- A soma de múltiplos modos não satura descontroladamente a faixa dinâmica digital.
- O limiter mestre permanece 100% linear ($0,000\%$ THD) até o limiar de 0.85, atuando puramente como proteção de segurança analógica.

### Physical Same-Note Restrike
Quando uma nova mensagem `NoteOn` atinge uma nota que já está vibrando:
- O banco de ressonadores **não é zerado**.
- O exciter é redisparado com a nova velocidade.
- A energia cinética é injetada diretamente no corpo em vibração, acumulando energia física de forma natural.

### Declicked Voice Stealing
Quando todas as 8 vozes estão ativas e uma 9ª nota requer alocação:
1. O alocador seleciona a voz de menor energia.
2. Uma cauda de micro-fade linear de 32 amostras ($\sim 0,67\text{ ms}$) é gerada a partir do sinal residual da voz roubada.
3. A voz é reinicializada e dispara a nova nota imediatamente.
4. A cauda de fade é mixada suavemente no bloco de áudio, eliminando estalos audíveis de descontinuidade no domínio do tempo.

---

## 6. BLE-MIDI e Parser com Timestamps de 13 bits

O subsistema NimBLE Central implementa a especificação MIDI over Bluetooth Low Energy:
- **Service UUID:** `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
- **Characteristic UUID:** `7772E5DB-3868-4112-A1A9-F2669D106BF3`
- **Descoberta Dinâmica de CCCD:** Não assume handles fixos; realiza descoberta explícita do descritor `0x2902` e escreve `0x0001` para ativação de notificações.
- **Parser de Pacotes:**
  - Decodifica os 6 bits mais significativos no header e 7 bits no byte de timestamp para formar o timestamp de 13 bits.
  - Suporta Running Status mantido dentro do pacote.
  - Intercala mensagens System Real-Time (`0xF8` Clock) sem interromper a decodificação de mensagens de canal.
  - Recupera o sincronismo de pacotes truncados sem corromper o estado das mensagens seguintes.
