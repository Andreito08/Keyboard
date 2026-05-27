// ARDUINO 1: TASTIERA ANALOGICA + LED (MASTER I2C)

// Includo la libreria per la comunicazione I2C
#include <Wire.h>

// ===== PIN =====
const byte KEY_PIN = A0;   // pin analogico per la tastiera
const byte LED_R   = 4;    // LED rosso
const byte LED_B   = 5;    // LED blu
const byte LED_G   = 6;    // LED verde
// Il buzzer si trova sull'altro Arduino, non qui

// ===== VALORI ADC PER OGNI TASTO (1..10) =====
const int adcVal[10] = {
  990, 985, 969, 958, 945, 930, 913, 890, 867, 839
};

// ===== MAPPE DEI CARATTERI =====
// Gruppi di lettere per il modo ABC (tasti 1..9)
const char* abcGroup[9] = {
  "abc", "def", "ghi",
  "jkl", "mno", "pqr",
  "stu", "vwx", "yz"
};

// Cifre per il modo 123 (tasti 1..9)
const char num123[9] = {
  '1', '2', '3', '4', '5', '6', '7', '8', '9'
};

// Caratteri speciali per il modo SPC (tasti 1..9)
const char spcChar[9] = {
  ' ', '.', ',',   // tasto 1=spazio, 2=punto, 3=virgola
  '\n', '^', '\b', // tasto 4=invio, 5=maiuscolo, 6=backspace
  '?', '!', '-'    // tasto 7=?, 8=!, 9=-
};

// ===== VARIABILI DI STATO =====
byte mode = 0;          // 0 = ABC, 1 = 123, 2 = SPC
bool capsNext = false;  // flag per maiuscola sul prossimo carattere
byte curKey = 0;        // tasto attualmente premuto (1..10, 0 se nessuno)
byte taps = 0;          // quante volte è stato premuto lo stesso tasto
unsigned long tapTime = 0; // istante (millis) dell'ultima pressione

// ===== INDIRIZZO I2C DELLO SLAVE (Arduino 2) =====
const byte SLAVE = 0x12;

// =============================================================
// FUNZIONI DI UTILITÀ
// =============================================================

// Legge il pin analogico con media su 8 campioni (riduce il rumore)
int readADC() {
  long sum = 0;
  for (byte i = 0; i < 8; i++) {
    sum += analogRead(KEY_PIN);
    delayMicroseconds(250);   // pausa tra le letture
  }
  return sum / 8;             // media aritmetica
}

// Decodifica il valore ADC nel numero del tasto (1..10)
// Restituisce 0 se nessun tasto è premuto
byte getKey() {
  int v = readADC();          // valore analogico filtrato
  int best = 1000;            // differenza minima trovata
  int idx = -1;               // indice del tasto migliore

  // confronto con tutti i 10 valori attesi
  for (byte i = 0; i < 10; i++) {
    int d = abs(v - adcVal[i]);
    if (d < best) {           // trovata differenza più piccola
      best = d;
      idx = i;
    }
  }

  // se la differenza è ≤ 12 considero il tasto valido
  if (best <= 12) {
    return idx + 1;           // tasto 1..10
  } else {
    return 0;                 // nessun tasto
  }
}

// Accende un LED per 120 ms (gli altri vengono spenti)
void flash(byte pin) {
  // spegne tutti i LED
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_B, LOW);
  digitalWrite(LED_G, LOW);

  // accende il LED scelto
  digitalWrite(pin, HIGH);
  delay(120);                 // mantiene acceso per 120 ms
  digitalWrite(pin, LOW);     // spegne
}

// Invia una stringa di comando via I2C
// Aggiunge automaticamente il carattere newline '\n' alla fine
void sendCmd(const char* s) {
  Wire.beginTransmission(SLAVE);   // avvia trasmissione verso lo slave
  Wire.write(s);                   // scrive la stringa (senza terminatore)
  Wire.write('\n');                // delimitatore di comando
  Wire.endTransmission();          // invia effettivamente i dati
  delay(2);                        // pausa per stabilizzare il bus I2C
}

// Costruisce un comando del tipo "PREFO|CARATTERE" e lo invia
// Ad esempio: sendFmt("OUT|CHAR|", 'a') produce "OUT|CHAR|a\n"
void sendFmt(const char* prefix, char c) {
  char buf[20];              // buffer temporaneo (dimensione generosa)
  buf[0] = 0;                // inizializza a stringa vuota
  strcat(buf, prefix);       // copia il prefisso
  buf[strlen(prefix)] = c;   // aggiunge il carattere
  buf[strlen(prefix) + 1] = 0; // terminatore nullo
  sendCmd(buf);              // invia il comando
}

// =============================================================
// LOGICA DI GESTIONE DELLA TASTIERA (multi‑tap, anteprima, conferma)
// =============================================================

// Conferma la sequenza di tap corrente e invia il comando/Output
void commit() {
  // Se non ci sono tap in attesa, esce immediatamente
  if (taps == 0) {
    return;
  }

  byte k = curKey; // tasto che si sta premendo

  // ========== TASTO 10 (cambio modo / zero) ==========
  if (k == 10) {
    // Se siamo in modo ABC
    if (mode == 0) {
      mode = 1;                   // passa a modo 123
      sendCmd("MODE|123");        // aggiorna l'etichetta sul display
      flash(LED_B);               // segnale visivo blu
    }
    // Se siamo in modo 123
    else if (mode == 1) {
      // Due pressioni del tasto 10 -> passa a modo SPC
      if (taps >= 2) {
        mode = 2;                 // modo speciale
        sendCmd("MODE|+*-");      // etichetta "+*-"
        flash(LED_B);
      }
      // Una pressione -> scrive il carattere '0'
      else {
        sendFmt("OUT|CHAR|", '0');
        flash(LED_B);
      }
    }
    // Se siamo in modo SPC -> torna ad ABC
    else {
      mode = 0;
      sendCmd("MODE|abc");
      flash(LED_B);
    }

    // Azzera la sequenza e cancella l'anteprima
    taps = 0;
    sendCmd("PREV|NONE");
    return;                       // uscita anticipata
  }

  // ========== TASTI DA 1 A 9 ==========

  // ----- Modo ABC -----
  if (mode == 0) {
    const char* grp = abcGroup[k - 1];   // stringa delle lettere (es. "abc")
    byte len = strlen(grp);              // lunghezza del gruppo (di solito 3)
    byte idx = (taps - 1) % len;         // indice ciclico in base ai tap
    char c = grp[idx];                   // lettera scelta

    // Se è attiva la maiuscola, converte e resetta il flag
    if (capsNext) {
      c = c - 'a' + 'A';                // minuscola → maiuscola
      capsNext = false;
      sendCmd("CAPS|0");                // comunica che la maiuscola è stata usata
    }

    sendFmt("OUT|CHAR|", c);             // invia il carattere definitivo
    flash(LED_G);                        // feedback verde per le lettere
  }

  // ----- Modo 123 -----
  else if (mode == 1) {
    char c = num123[k - 1];              // cifra corrispondente
    sendFmt("OUT|CHAR|", c);
    flash(LED_B);                        // feedback blu per i numeri
  }

  // ----- Modo SPC (caratteri speciali) -----
  else {
    char c = spcChar[k - 1];             // carattere speciale

    // Se è il carattere di invio ('\n')
    if (c == '\n') {
      sendCmd("OUT|CMD|ENTER");
      flash(LED_B);
    }
    // Se è il carattere di cambio maiuscolo ('^')
    else if (c == '^') {
      capsNext = !capsNext;              // inverte il flag
      if (capsNext) {
        sendCmd("CAPS|1");               // attiva maiuscola
      } else {
        sendCmd("CAPS|0");               // disattiva
      }
      sendCmd("OUT|CMD|CAPS");           // comando di aggiornamento
      flash(LED_B);
    }
    // Se è backspace ('\b')
    else if (c == '\b') {
      sendCmd("OUT|CMD|BACK");
      flash(LED_R);                      // feedback rosso per cancellazione
    }
    // Tutti gli altri caratteri speciali (spazio, punto, virgola, ?, !, -)
    else {
      sendFmt("OUT|CHAR|", c);
      flash(LED_B);
    }
  }

  // Azzera la sequenza e cancella l'anteprima
  taps = 0;
  sendCmd("PREV|NONE");
}

// Mostra l'anteprima del carattere che verrebbe prodotto se si confermasse ora
void preview(byte key) {
  // Se il tasto è 10 (cambio modo), mostra un breve testo
  if (key == 10) {
    const char* txt = "123";    // default quando si parte da ABC
    if (mode == 1) {
      txt = "0";                // in modo 123, anteprima "0"
    } else if (mode == 2) {
      txt = "abc";              // in modo SPC, anteprima "abc"
    }
    char buf[16];
    snprintf(buf, 16, "PREV|TEXT|%s", txt);
    sendCmd(buf);
    return;
  }

  // Anteprima per tasti 1..9 in base al modo
  if (mode == 0) {
    const char* grp = abcGroup[key - 1];
    byte idx = (taps - 1) % strlen(grp);
    char c = grp[idx];
    if (capsNext) {
      c = c - 'a' + 'A';
    }
    sendFmt("PREV|CHAR|", c);
  }
  else if (mode == 1) {
    sendFmt("PREV|CHAR|", num123[key - 1]);
  }
  else {
    char c = spcChar[key - 1];
    if (c == '\n') {
      sendCmd("PREV|TEXT|INV");   // "INV" = invio
    } else if (c == '^') {
      sendCmd("PREV|TEXT|MAI");   // "MAI" = maiuscolo
    } else if (c == '\b') {
      sendCmd("PREV|TEXT|DEL");   // "DEL" = cancella
    } else if (c == ' ') {
      sendCmd("PREV|TEXT|SPC");   // "SPC" = spazio
    } else {
      sendFmt("PREV|CHAR|", c);   // anteprima carattere singolo
    }
  }
}

// Gestisce una nuova pressione del tasto 'k'
void processKey(byte k) {
  unsigned long now = millis(); // tempo corrente

  // Se è lo stesso tasto già premuto e siamo ancora entro 1 secondo
  if (
    (k == curKey) &&
    (taps > 0) &&
    (now - tapTime <= 1000)
  ) {
    taps++;                 // incrementa il contatore di tap
    preview(k);             // aggiorna l'anteprima
    tapTime = now;          // aggiorna il timestamp dell'ultimo tap
    return;                 // non serve confermare ancora
  }

  // Se c'era una sequenza in corso (tasto diverso o timeout), la conferma
  if (taps > 0) {
    commit();
  }

  // Inizia una nuova sequenza per il tasto appena premuto
  curKey = k;      // imposta il nuovo tasto
  taps = 1;        // primo tap
  tapTime = now;   // timestamp della prima pressione
  preview(k);      // mostra l'anteprima
}

// =============================================================
// SETUP e LOOP
// =============================================================

void setup() {
  // Configura i pin dei LED come output
  pinMode(LED_R, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(LED_G, OUTPUT);

  // Spegne tutti i LED all'avvio
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_B, LOW);
  digitalWrite(LED_G, LOW);

  // Inizializza I2C come master
  Wire.begin();
  delay(50);                // attende che lo slave sia pronto

  // Invia i comandi iniziali al display
  sendCmd("MODE|abc");      // modo iniziale: ABC
  sendCmd("CAPS|0");        // maiuscola inizialmente disattivata
  sendCmd("PREV|NONE");     // nessuna anteprima all'avvio
}

void loop() {
  // Variabili statiche per il debounce (mantengono il valore tra un ciclo e l'altro)
  static byte lastRaw = 0;
  static unsigned long lastChange = 0;

  // Legge il tasto corrente
  byte raw = getKey();

  // Se il valore letto è diverso dal precedente, aggiorna i riferimenti
  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = millis();   // registra quando è cambiato
  }

  // Se il tasto è stabile da almeno 60 ms ed è effettivamente premuto
  if ( (millis() - lastChange >= 60) && (raw != 0) ) {
    processKey(raw);          // elabora la pressione

    // Aspetta che il tasto venga rilasciato (per evitare ripetizioni)
    while (getKey() != 0) {
      delay(20);              // piccolo ritardo per non sovraccaricare la CPU
    }
  }

  // Controllo del timeout multi‑tap (1 secondo di inattività)
  if ( (taps > 0) && (millis() - tapTime > 1000) ) {
    commit();                 // conferma automaticamente la sequenza
  }
}
