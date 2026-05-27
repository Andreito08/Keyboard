// ARDUINO 2: LCD 16x2 + BUZZER (SLAVE I2C)

#include <Wire.h>
#include <LiquidCrystal.h>

// ===== PIN LCD e BUZZER =====
const byte RS = 7, EN = 6, D4 = 5, D5 = 4, D6 = 3, D7 = 2; // pin LCD
const byte BZ = 8;   // pin del buzzer

// Crea l'oggetto LCD (collegamento a 4 fili)
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

// ===== INDIRIZZO I2C (stesso del master) =====
const byte SLAVE = 0x12;

// ===== BUFFER DI TESTO (2 righe x 16 colonne) =====
char buf[2][16];
byte row = 0;    // riga virtuale del cursore (0 o 1)
byte col = 0;    // colonna virtuale (0..15)
char modeLabel[4] = "abc";   // etichetta del modo corrente (max 3 caratteri)
bool caps = false;           // stato maiuscola (non usato direttamente, ma per coerenza)

// ===== VARIABILI PER LA RICEZIONE I2C (volatile perché usate in ISR) =====
volatile char rxLine[32];   // buffer di linea ricevuta
volatile byte rxPos = 0;    // posizione di scrittura nel buffer
volatile bool rxReady = false; // flag: true quando un comando completo è arrivato

// =============================================================
// FUNZIONI UTILITIES
// =============================================================

// Emette un suono con il buzzer (frequenza in Hz, durata in ms)
void beep(int freq, int dur) {
  tone(BZ, freq, dur);
}

// Pulisce tutto il buffer di testo (lo riempie di spazi)
void clearBuf() {
  for (byte r = 0; r < 2; r++) {
    for (byte c = 0; c < 16; c++) {
      buf[r][c] = ' ';
    }
  }
}

// Avanza il cursore virtuale di una posizione, gestendo a capo e scroll
void advance() {
  col++;
  if (col == 16) {               // fine della riga?
    col = 0;                     // torna all'inizio
    if (row == 0) {              // se era sulla prima riga
      row = 1;                   // passa alla seconda
    } else {                     // se era sulla seconda riga
      // scrolla verso l'alto: copia la seconda riga sulla prima
      for (byte c = 0; c < 16; c++) {
        buf[0][c] = buf[1][c];
        buf[1][c] = ' ';        // pulisce la seconda
      }
      row = 1;                   // cursore rimane sulla seconda riga
      col = 0;                   // all'inizio della riga
    }
  }
}

// Inserisce un carattere nel buffer e avanza il cursore
void putChar(char c) {
  // Se siamo sulla seconda riga e colonna ≥13, blocca a 12
  // (per non sovrascrivere le etichette del modo)
  if (row == 1 && col >= 13) {
    col = 12;
  }

  buf[row][col] = c;   // scrive il carattere
  advance();           // sposta il cursore

  // Ricontrolla dopo l'avanzamento (per sicurezza)
  if (row == 1 && col >= 13) {
    col = 12;
  }
}

// Aggiorna fisicamente il display LCD con il contenuto del buffer
void render() {
  char temp[2][16];
  // Copia il buffer in un'area temporanea
  memcpy(temp, buf, 32);

  // Sovrascrive le ultime 3 colonne della seconda riga con l'etichetta del modo
  temp[1][13] = modeLabel[0];
  temp[1][14] = modeLabel[1];
  temp[1][15] = modeLabel[2];

  // Le colonne 10..12 sono lasciate libere per l'anteprima (non gestita qui)

  // Scrive la prima riga sul display
  lcd.setCursor(0, 0);
  for (byte c = 0; c < 16; c++) {
    lcd.write(temp[0][c]);
  }

  // Scrive la seconda riga sul display
  lcd.setCursor(0, 1);
  for (byte c = 0; c < 16; c++) {
    lcd.write(temp[1][c]);
  }

  // Posiziona il cursore virtuale e lo rende lampeggiante
  lcd.setCursor(col, row);
  lcd.cursor();
  lcd.blink();
}

// =============================================================
// ESECUZIONE DEI COMANDI RICEVUTI VIA I2C
// =============================================================

void executeCmd(const char* s) {
  // Comando: MODE|xxx (imposta l'etichetta del modo)
  if (strncmp(s, "MODE|", 5) == 0) {
    strncpy(modeLabel, s + 5, 3);   // copia i 3 caratteri dopo "MODE|"
    modeLabel[3] = 0;               // terminatore nullo
    beep(750, 80);                  // suono di conferma (freq media)
  }

  // Comando: CAPS|x (attiva/disattiva maiuscola)
  else if (strncmp(s, "CAPS|", 5) == 0) {
    caps = (s[5] == '1');           // true se il carattere è '1'
    beep(750, 80);
  }

  // Comando: PREV|NONE (nessuna anteprima – non fa nulla, il render si occuperà)
  else if (strcmp(s, "PREV|NONE") == 0) {
    // Non serve eseguire azioni particolari
  }

  // Comando: PREV|CHAR|c (anteprima di un singolo carattere)
  else if (strncmp(s, "PREV|CHAR|", 10) == 0) {
    // In questa versione semplificata non gestiamo visivamente l'anteprima
    // perché la posizione del cursore potrebbe essere alterata.
    // L'anteprima è comunque visibile nel buffer se il carattere viene inserito.
    // (Nell'originale veniva sovrascritto temporaneamente il buffer)
  }

  // Comando: PREV|TEXT|xxx (anteprima di un breve testo)
  else if (strncmp(s, "PREV|TEXT|", 10) == 0) {
    // Stessa osservazione: gestione semplificata, non implementata.
  }

  // Comando: OUT|CHAR|c (scrittura definitiva di un carattere)
  else if (strncmp(s, "OUT|CHAR|", 9) == 0) {
    char c = s[9];           // carattere da scrivere
    putChar(c);              // lo inserisce nel buffer

    // Suono differenziato in base al tipo di carattere
    if (isLetter(c)) {
      beep(1600, 60);       // lettera: suono acuto e breve
    } else if (isdigit(c)) {
      beep(1100, 70);       // numero: suono medio
    } else {
      beep(750, 80);        // altro: suono grave
    }
  }

  // Comando: OUT|CMD|xxx (comando speciale: ENTER, BACK, CAPS)
  else if (strncmp(s, "OUT|CMD|", 8) == 0) {
    const char* cmd = s + 8;    // stringa del comando (es. "ENTER")

    // Comando ENTER (vai a capo o scroll)
    if (strcmp(cmd, "ENTER") == 0) {
      if (row == 0) {
        row = 1;          // passa alla seconda riga
        col = 0;
      } else {
        // Se eravamo già sulla seconda riga, esegue lo scroll
        for (byte c = 0; c < 16; c++) {
          buf[0][c] = buf[1][c];
          buf[1][c] = ' ';
        }
        row = 1;
        col = 0;
      }
      beep(750, 80);
    }

    // Comando BACK (cancella il carattere precedente)
    else if (strcmp(cmd, "BACK") == 0) {
      // Controlla che non sia all'inizio del buffer
      if (row > 0 || col > 0) {
        // Torna indietro di una posizione
        if (col > 0) {
          col--;
        } else {
          row = 0;
          col = 15;
        }

        // Se siamo nella zona delle etichette, riporta a 12
        if (row == 1 && col >= 13) {
          col = 12;
        }

        // Cancella il carattere nella posizione corrente
        buf[row][col] = ' ';
      }
      beep(320, 120);   // suono grave e lungo per cancellazione
    }

    // Comando CAPS (aggiornamento stato maiuscola – nessuna azione sull'LCD)
    else if (strcmp(cmd, "CAPS") == 0) {
      beep(750, 80);
    }
  }

  // Dopo ogni comando, aggiorna il display
  render();
}

// =============================================================
// GESTIONE DELLA RICEZIONE I2C (ISR)
// =============================================================

// Funzione chiamata automaticamente quando il master invia dati
void receiveEvent(int howMany) {
  // Legge tutti i byte disponibili
  while (Wire.available()) {
    char ch = Wire.read();    // legge un carattere

    // Ignora i carriage return (se presenti)
    if (ch == '\r') continue;

    // Se incontra il terminatore '\n', il messaggio è completo
    if (ch == '\n') {
      rxLine[rxPos] = 0;     // termina la stringa
      rxReady = true;        // segnala al loop che c'è un comando da eseguire
      rxPos = 0;             // resetta la posizione per il prossimo messaggio
    } else {
      // Accumula il carattere nel buffer, se c'è spazio
      if (rxPos < 31) {
        rxLine[rxPos++] = ch;
      }
    }
  }
}

// =============================================================
// SETUP
// =============================================================

void setup() {
  pinMode(BZ, OUTPUT);       // imposta il buzzer come output
  lcd.begin(16, 2);          // inizializza LCD 16×2
  clearBuf();                // pulisce il buffer interno
  render();                  // mostra il buffer vuoto sul display

  // Inizializza I2C come slave con l'indirizzo definito
  Wire.begin(SLAVE);
  // Registra la funzione che gestirà i dati in arrivo (ISR)
  Wire.onReceive(receiveEvent);
}

// =============================================================
// LOOP PRINCIPALE
// =============================================================

void loop() {
  // Se c'è un comando pronto (ricevuto dall'ISR)
  if (rxReady) {
    char line[32];                // buffer locale per elaborazione

    // Disabilita gli interrupt per copiare il buffer volatile in modo sicuro
    noInterrupts();
    strcpy(line, (const char*)rxLine);   // copia il comando
    rxReady = false;                     // resetta il flag
    interrupts();                        // riabilita gli interrupt

    // Esegue il comando ricevuto
    executeCmd(line);
  }
}
