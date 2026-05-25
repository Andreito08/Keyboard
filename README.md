# Tastiera Analogica Arduino

> Sistema di input a 10 tasti costruito con un partitore resistivo, logica multi-tap stile T9 e comunicazione I2C tra due schede Arduino.

<p align="center">
  Un sistema completo di tastiera fisica: dalla lettura analogica al testo su LCD, passando per un protocollo di comunicazione custom.
</p>

***

## Panoramica

Il progetto è composto da **due Arduino** che lavorano in coppia:

- **Arduino 1 (Master)** — legge una tastiera a 10 pulsanti tramite un partitore resistivo su `A0`, decodifica i tasti, gestisce la logica multi-tap e invia comandi via I2C. Tre LED (rosso, verde, blu) danno feedback visivo immediato su ogni azione.
- **Arduino 2 (Slave)** — riceve i messaggi I2C, aggiorna il display LCD 16×2, mostra la modalità attiva e l'anteprima del carattere in composizione, e produce feedback sonoro tramite buzzer.

La comunicazione tra le due schede avviene con un **protocollo testuale strutturato** — stringhe ASCII come `OUT|CHAR|A` o `MODE|123` — trasmesse su bus I2C all'indirizzo slave `0x12`.

***

## Funzionalità

| Funzione | Descrizione |
|---|---|
| **Multi-tap T9** | Ogni tasto 1–9 cicla tra le lettere del gruppo con tap ripetuti entro 1 secondo |
| **3 modalità** | `abc` (lettere), `123` (numeri), `+*-` (punteggiatura e comandi) |
| **Maiuscole** | Un tap dedicato rende maiuscolo il prossimo carattere |
| **Backspace** | Cancella l'ultimo carattere inserito |
| **Invio** | Vai a capo con scroll automatico del display |
| **Preview live** | Il display mostra in anteprima il carattere in composizione prima del commit |
| **Feedback LED** | Verde = lettera, Blu = numero/cambio modalità, Rosso = cancellazione |
| **Feedback sonoro** | Toni diversi per lettere, numeri, speciali e backspace |
| **Scroll automatico** | Il testo scorre sulle due righe LCD quando si supera il limite di colonna |

***

## Componenti utilizzati

### Arduino 1 — Master (Tastiera + LED)

| Componente | Dettaglio |
|---|---|
| Arduino UNO | o compatibile |
| 10 pulsanti | collegati a partitore resistivo su `A0` |
| LED rosso | pin `4` |
| LED blu | pin `5` |
| LED verde | pin `6` |

### Arduino 2 — Slave (Display + Buzzer)

| Componente | Dettaglio |
|---|---|
| Arduino UNO | o compatibile |
| LCD 16×2 | RS→`7`, EN→`6`, D4–D7→`5`,`4`,`3`,`2` |
| Buzzer passivo | pin `8` |

### Collegamento I2C

- `SDA` (A4) e `SCL` (A5) collegati tra le due schede
- Resistenze di pull-up da 4.7 kΩ su SDA e SCL
- Massa comune tra i due Arduino

***

## Struttura della repository

```bash
Keyboard/
└── code
     ├──  arduino_1.ino   # Master: tastiera analogica, LED, I2C sender
     └──  arduino_2.ino   # Slave: LCD, buzzer, I2C receiver
├── LICENSE
└── README.md
```

***

## Schema di funzionamento

```
  [ Tasto premuto ]
        │
        ▼
  Lettura ADC (media 8 campioni)
        │
        ▼
  Decodifica per corrispondenza con valori attesi (±12 ADC)
        │
        ▼
  Logica multi-tap / timeout / cambio modalità
        │
        ▼
  Messaggio I2C  ──────────────────►  Arduino Slave
  es. OUT|CHAR|A                            │
      MODE|123                              ▼
      PREV|CHAR|b                   Aggiorna LCD + Buzzer
      OUT|CMD|BACK
```

***

## Protocollo I2C

Ogni messaggio è una stringa ASCII terminata da `\n`, inviata all'indirizzo `0x12`:

| Messaggio | Significato |
|---|---|
| `MODE\|abc` / `MODE\|123` / `MODE\|+*-` | Cambia la modalità attiva |
| `CAPS\|1` / `CAPS\|0` | Attiva/disattiva la maiuscola sul prossimo carattere |
| `PREV\|CHAR\|x` | Mostra anteprima del carattere `x` |
| `PREV\|TEXT\|xxx` | Mostra anteprima testuale (es. `MAI`, `SPC`, `DEL`) |
| `PREV\|NONE` | Cancella l'anteprima |
| `OUT\|CHAR\|x` | Commette il carattere `x` nel buffer testo |
| `OUT\|CMD\|ENTER` | Vai a capo |
| `OUT\|CMD\|BACK` | Backspace |
| `OUT\|CMD\|CAPS` | Notifica toggle maiuscole |

***

## Mapping tasti

### Modalità `abc` — Lettere

| Tasto | Caratteri |
|---|---|
| 1 | a b c |
| 2 | d e f |
| 3 | g h i |
| 4 | j k l |
| 5 | m n o |
| 6 | p q r |
| 7 | s t u |
| 8 | v w x |
| 9 | y z |
| 10 | → passa a `123` |

### Modalità `123` — Numeri

| Tasto | Output |
|---|---|
| 1–9 | cifra corrispondente |
| 10 (×1) | `0` |
| 10 (×2) | → passa a `+*-` |

### Modalità `+*-` — Speciali e comandi

| Tasto | Funzione |
|---|---|
| 1 | Spazio |
| 2 | `.` |
| 3 | `,` |
| 4 | ENTER |
| 5 | Toggle maiuscola |
| 6 | Backspace |
| 7 | `?` |
| 8 | `!` |
| 9 | `-` |
| 10 | → torna a `abc` |

***

## Configurazione ADC

I valori attesi per la decodifica dei tasti si trovano in `arduino_1.ino`:

```cpp
const int expectedValues[10] = {
  990, 985, 969, 958, 945, 930, 913, 890, 867, 839
};
```

Se la lettura risulta instabile o imprecisa, misura i valori reali del tuo partitore aprendo il Monitor Seriale con `Serial.print(analogRead(A0))` e aggiorna l'array. La tolleranza accettata è **±12 unità ADC**.

***

## Come caricare

1. Carica **prima** `arduino_2.ino` sullo slave — così il bus I2C ha già un destinatario quando il master parte.
2. Carica `arduino_1.ino` sul master.
3. Collega le due schede via I2C: `SDA↔SDA`, `SCL↔SCL`, `GND↔GND`.
4. Alimenta entrambi e inizia a digitare.

***

## Librerie richieste

Entrambe incluse nell'Arduino IDE:

- `Wire.h` — comunicazione I2C
- `LiquidCrystal.h` — controllo display LCD parallelo

***

## Anteprima

*Coming soon*

***

## Autore

Progetto realizzato da **Andreito08**
🔗 [GitHub](https://github.com/Andreito08)

***

## Licenza

Distribuito sotto licenza **MIT**, realizzato a scopo didattico.
