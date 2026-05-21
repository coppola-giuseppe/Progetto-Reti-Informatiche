#ifndef KANBAN_H
#define KANBAN_H

#include <stdint.h>
#include <time.h>

// COSTANTI
#define MAX_TESTO 256 // lunghezza massima della descrizione di una attività
#define MAX_CARDS 100 // numero di cards massimo gestibile
#define MIN_CARDS_INIZIALI 10 // numero di cards minime all'inizializzazione
#define DEBUG_LOG 0 // se settata a 1 stampa tutti i messaggi, sennò stampa solo quelli principali


// POSSIBILI STATI DELLE CARDS
#define CARD_TODO 0
#define CARD_DOING 1
#define CARD_DONE 2

// STRUTTURA DATI DI UNA CARD
struct Card {
  uint32_t id;           // id univoco
  uint8_t colonna;       // stato della card
  char testo[MAX_TESTO]; // descrizione dell'attività

  uint16_t porta_utente; // porta dell'utente che l'ha presa

  time_t timestamp;       // timestamp di quando è entrata nello stato DOING
  uint8_t in_attesa_pong; // 1:PING mandato, attesa di PONG, 0:altrimenti
  time_t timestamp_ping;  // timestamp di quando è stato mandato il PING
};

#endif