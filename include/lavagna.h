#ifndef LAVAGNA_H
#define LAVAGNA_H

#include <arpa/inet.h>
#include <sys/time.h>

#define PING_TIMER 30 // quanti secondi una card deve rimanere in DOING prima di mandare un PING all'utente
#define PONG_TIMER 30 // quanti secondi aspettare dopo il PING prima di chiudere la connessione

// STRUTTURA DELL'UTENTE
struct Utente {
  int socket;         // decrittore del socket
  int porta;          // porta da usare per le revisioni
  int attivo;         // 1: utente connesso, 0: slot libero
  int occupato;       // 1: ha già un'attivita da svolgere, 0: è disponibile
  int card_proposta;  // id di una card proposta all'utente, serve per evitare lo
                      // spam di proposte
  int card_assegnata; // id della card assegnata all'utente
  int avvertito;      // mi dice se ho avvisato l'utente che le card sono finite...
                      // 1: avvisato, 0: non avvisato
};

// DICHIARAZIONE DI FUNZIONI DELLA LAVAGNA
int find_free_slot();
void free_slot(int);
void inizializza_lavagna();
void restore_card(int);
int get_socket_from_card(int);
int get_card_from_socket(int);

void handler_show_lavagna();
int handler_hello(int, uint16_t);
int handler_ack_card(int, uint16_t);
void handler_handle_card();
void handler_move_card(uint32_t, uint8_t, uint16_t);
int handler_request_user_list(int);
void handler_card_done(int);
int handler_create_card(int, uint16_t);
void handler_error(int, const char *);
void handler_ping_user(fd_set *);

#endif