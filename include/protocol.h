#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "kanban.h"
#include <stdint.h>

// IMPOSTAZIONI DEL SERVER
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5678
#define MAX_UTENTI 100

// STRUTTURE DEI PACCHETTI

// header che ogni messaggio deve avere
struct Header {
  uint8_t comando;    // comando del messaggio
  uint16_t lunghezza; // numero di byte del payload
} __attribute__((
    packed)); // rimuove gli spazi vuoti, applicato ad ogni struct del messaggio

// messaggio di default per i comandi che non necessitano un payload
struct MsgBase {
  struct Header hdr;
} __attribute__((packed));

// messaggio per CMD_ACK_CARD
struct MsgAck {
  struct Header hdr;
  uint32_t id_card;
} __attribute__((packed));

// messaggio per CMD_HELLO
struct MsgHello {
  struct Header hdr;
  uint16_t porta; // porta per la revisione
} __attribute__((packed));

// messaggio per CMD_CREATE_CARD e CMD_ERROR
struct MsgCard {
  struct Header hdr;
  char testo[MAX_TESTO];
} __attribute__((packed));

// messaggio per CMD_HANDLE_CARD
struct MsgHandleCard {
  struct Header hdr;
  struct Card card; // card assegnata
  uint32_t numero_utenti;
  uint16_t porte[MAX_UTENTI];
} __attribute__((packed));

// messaggio per CMD_SEND_USER_LIST
struct MsgList {
  struct Header hdr;
  uint32_t numero_utenti;
  uint16_t porte[MAX_UTENTI];
} __attribute__((packed));

// messaggio per CMD_REVIEW_CARD
struct MsgReview {
  struct Header hdr;
  uint32_t id_card;      // id della card da revisionare
  char testo[MAX_TESTO]; // descrizione della card

} __attribute__((packed));

// messaggio per errore
struct MsgError {
  struct Header hdr;
  char testo[MAX_TESTO];
} __attribute__((packed));

#endif