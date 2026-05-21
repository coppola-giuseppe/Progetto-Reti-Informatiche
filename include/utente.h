#ifndef UTENTE_H
#define UTENTE_H

#include "protocol.h"
#include <sys/time.h>

#define DURATA_LAVORO 5 // durata della mia_sleep
#define RETRY_REVIEW 30   // dopo quanto riprovare la review automatica

// enum per la gestione della mia_sleep
// se sono in STATO_LAVORANDO disabilito la tastiera, altrimenti è abilitata
typedef enum {
  STATO_LIBERO,          // l'utente è libero
  STATO_LAVORANDO,       // l'utente sta lavorando alla card assegnata
  STATO_REVISIONE,       // l'utente deve chiedere la revisione
  STATO_ATTESA_REVISIONE // se non ci sono altri utenti connessi, si mette in
                         // attesa
} StatoLavoro;

// enum per la conversione di un comando da stringa a numero
typedef enum {
  CMD_INPUT_QUIT,
  CMD_INPUT_CREATE_CARD,
  CMD_INPUT_CARD_DONE,
  CMD_INPUT_HELLO,
  CMD_INPUT_ACK_CARD,
  CMD_INPUT_REQUEST_USER_LIST,
  CMD_INPUT_REVIEW_CARD,
  CMD_INPUT_UNKNOWN
} Comandi;

// DICHIARAZIONE DI FUNZIONI DELL'UTENTE
Comandi convertitore_comandi(const char *);
void mia_sleep(int, StatoLavoro *, time_t, int, fd_set *);
int handler_review_card(int, struct MsgList *, int, char[MAX_TESTO], int);
void handler_card_done(int);

#endif