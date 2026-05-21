#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "commands.h"
#include "kanban.h"
#include "lavagna.h"
#include "protocol.h"

struct Utente lista_utenti[MAX_UTENTI];
int utenti_connessi = 0;
// Variabile globale che conta le card totali, usata per validare una
// CREATE_CARD
int numero_cards = 0;
struct Card lista_cards[MAX_CARDS];

int main() {
  int listener, ret;
  struct sockaddr_in my_addr;

  fd_set master;
  fd_set read_fds;
  int fdmax;

  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  // creazione del socket di ascolto per la lavagna
  listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  int set = 1;
  // setto l'opzione SO_REUSEADDR così se riavvio il server posso subito usare
  // lo stesso (IP + porta)
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set));
  if (listener == -1) {
    perror("Errore nella socket "); // INFO LOG
    exit(1);
  }

  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &my_addr.sin_addr);
  // bind del socket
  ret = bind(listener, (struct sockaddr *)&my_addr, sizeof(my_addr));
  if (ret == -1) {
    perror("Errore nella bind "); // INFO LOG
    exit(1);
  }

  // listen del socket
  ret = listen(listener, 10);
  if (ret == -1) {
    perror("Errore nella listen "); // INFO LOG
    exit(1);
  }

  // inserisco il socket principale e la tastiera nel set master
  FD_SET(listener, &master);
  FD_SET(0, &master);
  fdmax = listener;

  struct timeval timeout;

  // metto nella lavagna 10 card di default
  inizializza_lavagna();
  // mostro la lavagna
  handler_show_lavagna();

  // cuore della lavagna
  while (1) {
    // ripristino del timeout
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    read_fds = master;

    ret = select(fdmax + 1, &read_fds, NULL, NULL, &timeout);

    // controllo se devo mandare PING oppure ci sono timeout per le PONG
    handler_ping_user(&master);

    // GESTIONE DELLA RETE
    //
    switch (ret) {
    case -1:                          // errore
      perror("Errore nella select "); // INFO LOG
      exit(1);

    case 0: // timeout scaduto, nessun descrittore pronto
      break;

    default: // descrittori pronti

      for (int i = 0; i <= fdmax; i++) {

        if (FD_ISSET(i, &read_fds)) {
          if (i == 0) { // è la tastiera: mi metto in attesa di comandi
            char comando_tastiera[256];
            fgets(comando_tastiera, sizeof(comando_tastiera), stdin);
            comando_tastiera[strcspn(comando_tastiera, "\n")] = 0;

            // in base al comando, chiamo lo specifico handler
            if (strcmp(comando_tastiera, "SHOW_LAVAGNA") == 0) {
              handler_show_lavagna();
              printf("> ");
              fflush(stdout);
            } else if (strcmp(comando_tastiera, "HANDLE_CARD") ==
                       0) { // non serve più poiché la funzione
                            // handler_handle_card viene chiamata
                            // automaticamente, lascio in caso di future
                            // modifiche
              handler_handle_card();
            } else {
              printf("Comando sconosciuto.\n> "); // INFO LOG
              fflush(stdout); // la printf si aspetta di terminare con \n, però
                              // voglio che un comando sia scritto nella stessa
                              // riga di ">", quindi forzo la stampa
            }
          } else if (i == listener) { // un nuovo utente si prova a connettere
            int new_user = accept(listener, NULL, NULL);
            if (new_user == -1) {
              perror("Errore nella accept "); // INFO LOG
              continue;
            }

            // cerco uno slot utente, se finiti chiudo la connessione
            int index = find_free_slot();
            if (index == -1) {
              printf("\n[NOTIFICA] Max utenti connessi raggiunti"); // INFO LOG
              close(new_user);
              continue;
            }
            // registro l'utente e il suo socket
            lista_utenti[index].attivo = 1;
            lista_utenti[index].socket = new_user;
            lista_utenti[index].porta =
                0; // inizializzo la porta a zero, verrà settata alla CMD_HELLO

            FD_SET(new_user, &master);
            if (new_user > fdmax)
              fdmax = new_user;

            if (DEBUG_LOG) {
              printf("\n[NOTIFICA] Nuovo utente connesso sul socket %d\n",
                     new_user);
            } // DEBUG
            printf("> ");
            fflush(stdout);
          } else { // utente già connesso ha mandato un messaggio
            struct Header hdr;
            // leggo prima l'header per capire il tipo di comando ricevuto e la
            // lunghezza del payload
            int byte_letti = recv(i, &hdr, sizeof(struct Header), 0);
            if (byte_letti <= 0) { // chiusura della connessione o errore
              printf("\n[NOTIFICA] L'utente sul socket %d si è disconnesso.\n",
                     i); // INFO LOG
              if (utenti_connessi >
                  0) // se l'utente si disconnette prima di una hello, non devo
                     // decrementare utenti_connessi
                utenti_connessi--;
              restore_card(i);
              free_slot(i);
              close(i);
              FD_CLR(i, &master);
              handler_handle_card(); // se la connessione è chiusa, riprovo a
                                     // riassegnare la card
            } else {                 // comando valido
              uint16_t len_payload = ntohs(hdr.lunghezza);

              switch (hdr.comando) {
              // chiamo lo specifico handler per la HELLO, se ha successo invio
              // una card
              case CMD_HELLO:
                if (handler_hello(i, len_payload) ==
                    -1) { // errore: chiudo la connessione e aggiorno il set
                          // master
                  close(i);
                  FD_CLR(i, &master);
                  free_slot(i);
                } else
                  handler_handle_card(); // invio della card
                break;

              // chiusura della connessione: metto le card possedute da DOING a
              // TODO chiudo la connessione e aggiorno il set master
              case CMD_QUIT:
                printf(
                    "\n[NOTIFICA] L'utente sul socket %d si è disconnesso.\n",
                    i); // INFO LOG
                utenti_connessi--;
                restore_card(i);
                free_slot(i);
                close(i);
                FD_CLR(i, &master);
                handler_handle_card();
                break;

              // ricevendo un ACK valido, vado effettivamente ad assegnare una
              // card ad un utente ed aggiorno la lavagna
              case CMD_ACK_CARD:
                if (handler_ack_card(i, len_payload) ==
                    -1) { // errore: chiudo la connessione e aggiorno il set
                          // master
                  utenti_connessi--;
                  close(i);
                  FD_CLR(i, &master);
                  free_slot(i);
                }
                break;

              case CMD_REQUEST_USER_LIST:
                if (handler_request_user_list(i) ==
                    -1) { // errore: chiudo la connessione e aggiorno il set
                          // master
                  // TODO: invece di cacciare l'utente, mando messaggio di
                  // errore
                  utenti_connessi--;
                  close(i);
                  FD_CLR(i, &master);
                  free_slot(i);
                }
                break;

              case CMD_CREATE_CARD: {

                int ret = handler_create_card(i, len_payload);

                if (ret == 0) {
                  handler_error(i, "numero massimo di card raggiunte");
                } else if (ret == -1) {
                  handler_error(i, "errore durante la ricezione del messaggio");
                }

                break;
              }
              case CMD_PONG_LAVAGNA: {
                // ho ricevuto una PONG, aggiorno il timer della PING
                int card_id = get_card_from_socket(i);
                if (DEBUG_LOG) {
                  printf("\n[NOTIFICA] Ho ricevuto il PONG dall'utente %d\n",
                         i);
                } // DEBUG

                for (int j = 0; j < MAX_CARDS; j++) {
                  if (lista_cards[j].id == card_id) {
                    lista_cards[j].in_attesa_pong = 0;
                    lista_cards[j].timestamp_ping = time(NULL);
                    break;
                  }
                }

                break;
              }
              case CMD_CARD_DONE:
                handler_card_done(i);
                break;

              default:
              }
            }
          }
        }
      }

      break;
    }
  }

  return 0;
}

// handler che manda PING all'utente se sono passati PING_TIMER secondi e se non
// riceve PONG dopo PONG_TIMER secondi
void handler_ping_user(fd_set *master) {
  time_t now = time(NULL); // ora attuale

  // scorro le carte e guardo solo quelle in DOING
  for (int i = 0; i < MAX_CARDS; i++) {
    if (lista_cards[i].colonna == CARD_DOING) {

      // se avevo già mandato il PING, guardo se sono passati PONG_TIMER secondi
      if (lista_cards[i].in_attesa_pong == 1) {
        if (now - lista_cards[i].timestamp_ping >= PONG_TIMER) {
          // tempo scaduto, chiudo la connessione
          int socket = get_socket_from_card(lista_cards[i].id);
          printf(
              "\n[NOTIFICA] L'utente %d non risponde, chiudo la connessione.\n",
              socket); // INFO LOG
          utenti_connessi--;
          restore_card(socket);
          free_slot(socket);
          close(socket);
          FD_CLR(socket, master);
        }
      }
      // altrimenti controllo se sono passati PING_TIMER secondi da quando la
      // card è in DOING e in caso mando il PING
      else {
        if (now - lista_cards[i].timestamp_ping >= PING_TIMER) {
          // passati PING_TIMER secondi, mando il PING
          lista_cards[i].in_attesa_pong = 1;
          lista_cards[i].timestamp_ping = now;

          struct MsgBase msg_ping;
          msg_ping.hdr.comando = CMD_PING_USER;
          msg_ping.hdr.lunghezza = 0;

          int socket = get_socket_from_card(lista_cards[i].id);
          if (socket != -1) {
            send(socket, &msg_ping, sizeof(struct MsgBase), 0);
            if (DEBUG_LOG) {
              printf("\n[NOTIFICA] Ho mandato il PING all'utente %d\n", socket);
            } // DEBUG
          }
        }
      }
    }
  }
}

// dato l'id di una card, ritorna il socket che la sta gestendo, -1 altrimenti
int get_socket_from_card(int card_id) {
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (lista_utenti[i].card_assegnata == card_id) {
      return lista_utenti[i].socket;
    }
  }
  return -1;
}

// dato un socket, ritorna la card che sta gestendo, -1 altrimenti
int get_card_from_socket(int socket) {
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (lista_utenti[i].socket == socket &&
        lista_utenti[i].card_assegnata != 0) {
      return lista_utenti[i].card_assegnata;
    }
  }
  return -1;
}

// handler che dato un socket ed un testo, invia il messaggio d'errore
void handler_error(int socket, const char *testo) {

  struct MsgError msg;
  msg.hdr.comando = CMD_ERROR;
  msg.hdr.lunghezza = htons(MAX_TESTO);
  strncpy(msg.testo, testo, MAX_TESTO - 1);
  msg.testo[MAX_TESTO - 1] = '\0';
  send(socket, &msg, sizeof(struct MsgError), 0);
}

// handler che gestisce l'aggiunta di una nuova card
// riceve la descrizione della card dall'utente e ritorna
// 1: ok, 0: max cards raggiunte, -1:errore
int handler_create_card(int socket, uint16_t len_payload) {
  char testo[MAX_TESTO];

  // leggo comunque il messaggio ricevuto, sia che possa creare la nuova card
  // sia che non possa sennò romperei il TCP
  int byte_ricevuti = recv(socket, &testo, sizeof(char) * MAX_TESTO, 0);
  if (byte_ricevuti <= 0) {
    perror("Errore recv payload HANDLE_CARD "); // INFO LOG
    return -1;
  }

  int utente = -1;
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (lista_utenti[i].socket == socket) {
      utente = i;
      break;
    }
  }

  // se l'utente non è registrato non faccio nulla
  if (utente == -1)
    return -1;

  // se non posso aggiungere altre cards, non faccio nulla
  if (numero_cards == MAX_CARDS)
    return 0;

  struct Card new_card;
  new_card.id = numero_cards + 1;
  new_card.colonna = CARD_TODO;
  strcpy(new_card.testo, testo);
  new_card.porta_utente = 0;
  new_card.timestamp = 0;
  new_card.in_attesa_pong = 0;
  new_card.timestamp_ping = 0;

  lista_cards[numero_cards++] = new_card;
  if (DEBUG_LOG) {
    printf("\n[NOTIFICA] Nuova card aggiunta dall'utente %d, card totali: %d",
           socket, numero_cards);
  } // DEBUG

  // mostro la lavagna aggiornata
  handler_show_lavagna();

  // in caso ci fossero utenti liberi e non c'erano card disponbibili, li servo
  handler_handle_card();

  return 1;
}

// handler che gestisce la ricezione di CARD_DONE
// guarda se chi manda il messaggio aveva una card in DOING e se si la mette in
// DONE manda una nuova card all'utente
void handler_card_done(int socket) {

  // dato il socket, vado a prendere la porta dell'utente
  uint16_t porta = 0;
  int trovato = 0;
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (lista_utenti[i].socket == socket && lista_utenti[i].attivo == 1) {
      porta = lista_utenti[i].porta;
      trovato = 1;
      lista_utenti[i].avvertito =
          0; // l'utente ha finito una card, devo avvisarlo se non ci sono card
             // disponibili
      lista_utenti[i].card_assegnata = 0; // resetto la card assegnata
      break;
    }
  }

  // se non ho trovato un utente, non faccio nulla
  if (trovato == 0) {
    if (DEBUG_LOG) {
      printf("\n[NOTIFICA] CARD_DONE ricevuto da un utente non registrato\n");
    } // DEBUG
    return;
  }

  uint32_t id_card = 0;
  trovato = 0;
  // Usando la porta vado a cercare quale card è gestita da quella porta
  for (int i = 0; i < MAX_CARDS; i++) {
    if (lista_cards[i].id != 0 && lista_cards[i].colonna == CARD_DOING &&
        lista_cards[i].porta_utente == porta) {
      id_card = lista_cards[i].id;
      trovato = 1;
      break;
    }
  }

  // non aveva una card assegnata
  if (trovato == 0)
    return;

  // metto la card in DONE
  handler_move_card(id_card, CARD_DONE, porta);

  // riassegno una card
  handler_handle_card();
}

// dato un socket utente, guarda se era registrato e in caso mette eventuali
// carte DOING in TO DO
void restore_card(int socket) {
  uint16_t porta = 0;
  for (int j = 0; j < MAX_UTENTI; j++) {
    if (lista_utenti[j].attivo && lista_utenti[j].socket == socket) {
      porta = lista_utenti[j].porta;
      break;
    }
  }

  // se era registrato rimetto in to do la card che aveva
  if (porta > 0) {
    for (int i = 0; i < MAX_CARDS; i++) {
      if (lista_cards[i].id != 0 && lista_cards[i].colonna == CARD_DOING &&
          lista_cards[i].porta_utente == porta) {
        printf(
            "\n[NOTIFICA] Card %u rimessa in TO DO (utente %u disconnesso).\n",
            lista_cards[i].id, porta); // INFO LOG
        handler_move_card(lista_cards[i].id, CARD_TODO, 0);
      }
    }
  }
}

// handler di CMD_HELLO, legge la porta e la salva
// Ritorna -1 in caso di errori, 0 altrimenti
int handler_hello(int socket, uint16_t len_payload) {
  uint16_t porta;

  int byte_ricevuti = recv(socket, &porta, len_payload, 0);

  if (byte_ricevuti <= 0) {
    perror("Errore nella recv in handler_hello "); // INFO LOG
    return -1;
  } else if (byte_ricevuti != len_payload) {
    printf(
        "Errore: non sono arrivati tutti i byte nella handler_hello.\n"); // INFO
                                                                          // LOG
    return -1;
  }

  porta = ntohs(porta);

  for (int j = 0; j < MAX_UTENTI; j++) {
    if (lista_utenti[j].attivo && lista_utenti[j].socket == socket) {
      lista_utenti[j].porta = porta;
      lista_utenti[j].card_proposta = 0;
      lista_utenti[j].avvertito = 0;
      if (DEBUG_LOG) {
        printf("\n[NOTIFICA] Utente registrato. Socket:%d  Porta:%d\n", socket,
               porta);
      } // DEBUG
      break;
    }
  }
  utenti_connessi++;
  return 0;
}

// gestisce l'assegnazione di attività agli utenti che non hanno una card
// viene chiamata in automatico dopo una registrazione tramite HELLO, quindi
// gestirà un utente alla volta tuttavia è stata pensata per inviare una card a
// tutti gli utenti attualmente connessi ma che non hanno un lavoro da svolgere
void handler_handle_card() {

  int printed = 0; // flag per sapere se ho stampato notifiche

  // RACCOLTA E ORDINAMENTO UTENTI
  int socket_attivi[MAX_UTENTI];
  uint16_t porte_attive[MAX_UTENTI];
  int index = 0;

  // salvo solo gli utenti che si sono registrati ed hanno fatto HELLO
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (lista_utenti[i].attivo == 1 && lista_utenti[i].porta > 0) {
      socket_attivi[index] = lista_utenti[i].socket;
      porte_attive[index] = lista_utenti[i].porta;
      index++;
    }
  }

  // ordino gli utenti per porta perché la specifica del progetto
  // richiede che le card vengano assegnate partendo dalla porta più piccola
  // uso bubble sort perché il numero di utenti è molto piccolo
  for (int i = 0; i < utenti_connessi - 1; i++) {
    for (int j = 0; j < utenti_connessi - i - 1; j++) {
      if (porte_attive[j] > porte_attive[j + 1]) {
        // scambio porte
        uint16_t temp_porta = porte_attive[j];
        porte_attive[j] = porte_attive[j + 1];
        porte_attive[j + 1] = temp_porta;

        // scambio socket
        int temp_socket = socket_attivi[j];
        socket_attivi[j] = socket_attivi[j + 1];
        socket_attivi[j + 1] = temp_socket;
      }
    }
  }

  // ASSEGNAZIONE DELLE CARD
  int indice_prossima_card =
      0; // le card vengono assegnate dopo un ACK, se non tengo conto di dove
         // sono arrivato e rifacessi il ciclo, manderei a più utenti la stessa
         // card -> per il caso di più utenti in attesa di una card alla
         // chiamata
  for (int i = 0; i < utenti_connessi; i++) {

    int socket_destinatario = socket_attivi[i];
    int porta_destinatario = porte_attive[i];

    // se ad un utente ho già proposto una card, non gli mando nulla

    // cerco l'indice nella lista utenti
    int indice_utente = -1;
    for (int u = 0; u < MAX_UTENTI; u++) {
      if (lista_utenti[u].socket == socket_destinatario) {
        indice_utente = u;
        break;
      }
    }

    if (indice_utente != -1 && lista_utenti[indice_utente].card_proposta != 0) {
      if (DEBUG_LOG) {
        printf("\n[NOTIFICA] Utente sulla porta %d ha già la card %d.\n",
               porta_destinatario, lista_utenti[indice_utente].card_proposta);
      } // DEBUG
      continue; // vado al prossimo utente
    }

    // ogni utente può avere una sola card alla volta, se ha già una card passo
    // al prossimo utente
    int working = 0;
    for (int c = 0; c < MAX_CARDS; c++) {
      if (lista_cards[c].id != 0 && lista_cards[c].colonna == CARD_DOING &&
          lista_cards[c].porta_utente == porta_destinatario) {
        working = 1;
        break;
      }
    }
    if (working) {
      if (DEBUG_LOG) {
        printf(
            "\n[NOTIFICA] Utente sulla porta %u ha già una card in carico.\n",
            porta_destinatario);
      } // DEBUG
      printed = 1;
      continue; // vado al prossimo utente
    }

    // utente libero, cerco la card da dargli

    int indice_card = -1;
    for (int c = indice_prossima_card; c < MAX_CARDS; c++) {
      // cerco una card nello stato TODO
      if (lista_cards[c].id != 0 && lista_cards[c].colonna == CARD_TODO) {
        // verifico che questa card non l'abbia già proposta ad un utente
        int proposta = 0;
        for (int u = 0; u < MAX_UTENTI; u++) {
          if (lista_utenti[u].attivo &&
              lista_utenti[u].card_proposta == lista_cards[c].id) {
            proposta = 1;
            break;
          }
        }

        if (proposta) { // card già proposta ad un altro utente, passo alla card
                        // successiva
          // per il caso in cui un utente riceve una card e prima che faccia
          // l'ACK un altro utente si registra e non deve ricevere una card già
          // proposta
          continue;
        }

        // card libera, la propongo
        indice_card = c;
        indice_prossima_card = c + 1;
        break;
      }
    }

    if (indice_card == -1) { // nessuna carta in to do, lo comunico all'utente

      if (indice_utente != -1 && lista_utenti[indice_utente].avvertito == 0) {
        printf(
            "\n[NOTIFICA] Nessuna card in TO DO disponibile per l'utente %u.\n",
            porta_destinatario); // INFO LOG
        printed = 1;
        handler_error(socket_destinatario,
                      "Non ci sono card in TO DO al momento. Attendi o crea "
                      "una nuova card.");
        lista_utenti[indice_utente].avvertito =
            1; // mi segno che lo ho già notificato dell'assenza di nuove card
      }
      continue; // se le carte in to do sono finite, lo comunicherò a tutti gli
                // utenti che si aspettano una card
    }

    // PREPARAZIONE DEL MESSAGGIO ED INVIO
    // Scelgo di mandare la card per intero invece che solo
    // id/descrizione/utenti connessi/porte perché così l'utente riceve già
    // tutto il necessario in caso di altre implementazioni future
    struct MsgHandleCard msg;
    msg.hdr.comando = CMD_HANDLE_CARD;
    msg.card = lista_cards[indice_card];
    msg.numero_utenti =
        0; // lo metto a zero per usarlo indice della lista delle porte

    // riempio la lista delle porte, destinatario escluso
    for (int j = 0; j < utenti_connessi; j++) {
      if (porte_attive[j] != porta_destinatario) {
        msg.porte[msg.numero_utenti] = htons(porte_attive[j]);
        msg.numero_utenti++;
      }
    }

    // mi calcolo la dimensione esatta del payload
    // creo dinamicamente il payload in base al numero di utenti per non mandare
    // byte inutili
    int payload = sizeof(struct Card) + sizeof(uint32_t) +
                  sizeof(uint16_t) * msg.numero_utenti;
    msg.hdr.lunghezza = htons(payload);

    msg.numero_utenti = htonl(msg.numero_utenti);
    msg.card.id = htonl(msg.card.id);
    msg.card.porta_utente = htons(msg.card.porta_utente);
    // uso la funzione htobe64 per convertire i timestamp in network order, in
    // quanto time_t è a 64 bit
    msg.card.timestamp = (time_t)htobe64((uint64_t)msg.card.timestamp);
    msg.card.timestamp_ping =
        (time_t)htobe64((uint64_t)msg.card.timestamp_ping);

    // invio del pacchetto formato da header + payload byte
    int byte_inviati =
        send(socket_destinatario, &msg, sizeof(struct Header) + payload, 0);
    if (byte_inviati > 0) {
      int id_originale = lista_cards[indice_card]
                             .id; // prendo l'id non convertito i network order
      printf("\n[NOTIFICA] Inviata card ID %u all'utente sulla porta %u.\n",
             id_originale, porta_destinatario); // INFO LOG
      printed = 1;

      if (indice_utente != -1) {
        lista_utenti[indice_utente].card_proposta = id_originale;
      }
    }
  }

  // stampo "> " solo se ho stampato notifiche, per evitare doppi "> "
  if (printed) {
    printf("> ");
    fflush(stdout);
  }
}

// handler di REQUEST_USER_LIST, invia all'utente la lista utenti
// -1: errore, 0: ok
int handler_request_user_list(int socket) {

  uint16_t porte_attive[MAX_UTENTI];
  int count = 0;

  uint16_t porta_destinatario = 0;
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (lista_utenti[i].socket == socket) {
      porta_destinatario = lista_utenti[i].porta;
      break;
    }
  }
  if (!porta_destinatario)
    return -1;

  // salvo solo gli utenti che si sono registrati ed hanno fatto HELLO
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (lista_utenti[i].attivo == 1 && lista_utenti[i].porta > 0) {
      if (lista_utenti[i].porta != porta_destinatario) {
        porte_attive[count] = lista_utenti[i].porta;
        count++;
      }
    }
  }

  // ordino le porte con bubble sort sempre per mandare all'utente una lista
  // ordinata delle porte
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      if (porte_attive[j] > porte_attive[j + 1]) {
        // scambio le porte
        uint16_t temp_porta = porte_attive[j];
        porte_attive[j] = porte_attive[j + 1];
        porte_attive[j + 1] = temp_porta;
      }
    }
  }

  // CREAZIONE MESSAGGIO

  struct MsgList msg_list;
  msg_list.hdr.comando = CMD_SEND_USER_LIST;
  // creo dinamicamente il payload in base al numero di utenti per non mandare
  // byte inutili
  int payload = sizeof(uint32_t) + sizeof(uint16_t) * count;
  msg_list.hdr.lunghezza = htons(payload);

  msg_list.numero_utenti = htonl((uint32_t)count); // utenti_connessi - 1
  for (int i = 0; i < count; i++) {
    msg_list.porte[i] = htons(porte_attive[i]);
  }

  int byte_inviati =
      send(socket, &msg_list, sizeof(struct Header) + payload, 0);
  if (byte_inviati > 0) {
    printf("\n[NOTIFICA] Inviata lista utenti all'utente sulla porta %u.\n",
           porta_destinatario); // INFO LOG
  }
  printf("> ");
  fflush(stdout);
  return 0;
}

// gestisce la ricezione dell'ACK dopo aver proposto un task ad un utente
// ritorna -1: in caso di errori, 0: tutto ok
int handler_ack_card(int socket, uint16_t len_payload) {
  uint32_t id;

  // leggo l'id della carta che ha accettato l'utente
  int byte_letti = recv(socket, &id, len_payload, 0);
  if (byte_letti <= 0) {
    perror("Errore nella recv in handler_ack_card "); // INFO LOG
    return -1;
  }

  id = ntohl(id);

  // cerco la porta dell'utente e resetto la variabile card_proposta per future
  // proposte
  int porta_utente = 0;
  for (int j = 0; j < MAX_UTENTI; j++) {
    if (lista_utenti[j].attivo && lista_utenti[j].socket == socket) {
      porta_utente = lista_utenti[j].porta;
      lista_utenti[j].card_proposta = 0;
      lista_utenti[j].card_assegnata = id;
      break;
    }
  }

  if (porta_utente == 0) {
    printf(
        "\n[NOTIFICA] ACK arrivato da un utente non registrato.\n"); // INFO LOG
    return -1;
  }

  printf("\n[NOTIFICA] L'utente sulla porta %d ha accettato la card ID %u!\n",
         porta_utente, id); // INFO LOG

  handler_move_card(id, CARD_DOING, porta_utente);

  return 0;
}

// gestisce lo spostamento delle cards nella lavagna
// la porta viene resettata se si torna in todo e settata se si va in doing
void handler_move_card(uint32_t id_card, uint8_t nuovo_stato, uint16_t porta) {
  int trovata = 0;

  // ricerca della card con id = id_card
  for (int i = 0; i < MAX_CARDS; i++) {
    if (lista_cards[i].id == id_card && lista_cards[i].id != 0) {
      trovata = 1;

      // aggiornamento dello stato
      lista_cards[i].colonna = nuovo_stato;

      // svolgo azioni specifiche in base al nuovo stato
      if (nuovo_stato ==
          CARD_DOING) { // un utente ha iniziato a gestire una card

        // salvo la porta dell'utente che gestisce la card
        lista_cards[i].porta_utente = porta;

        // salvo il timestamp attuale e inizializzo il PING-PONG
        lista_cards[i].timestamp = time(NULL);
        lista_cards[i].timestamp_ping = time(NULL);
        lista_cards[i].in_attesa_pong = 0;
      } else if (nuovo_stato == CARD_DONE) { // un utente ha finito la card

        // Manteniamo la sua porta per sapere chi l'ha completata
        lista_cards[i].porta_utente = porta;

        // salvo il timestamp e resetto il PING-PONG
        lista_cards[i].timestamp = time(NULL);
        lista_cards[i].timestamp_ping = time(NULL);
        lista_cards[i].in_attesa_pong = 0;
      } else if (nuovo_stato ==
                 CARD_TODO) { // card torna in to do a causa di errore o
                              // disconnessione di un utente

        // inizializzo la card per essere riassegnata
        lista_cards[i].porta_utente = 0;
        lista_cards[i].timestamp = 0;
        lista_cards[i].in_attesa_pong = 0;
      }

      break;
    }
  }

  if (!trovata) { // id_card non valido
    printf("\n[NOTIFICA] Impossibile muovere: Card ID %u non trovata.\n> ",
           id_card); // INFO LOG
    fflush(stdout);
    return;
  }

  // mostro il nuovo stato della lavagna
  handler_show_lavagna();
}

#define COL_WIDTH 65
#define MAX_PRINT_LINES 200

// funzione che dato un testo appartenente ad una card, lo formatta, cercando di
// andare capo senza spezzare le parole, in modo che venga stampato tutto il
// testo all'interno della colonna
void formatta_testo_colonna(char colonna[][COL_WIDTH + 1], int *righe_attuali,
                            const char *testo_completo) {
  int max_caratteri = COL_WIDTH - 2; // levo le due | laterali
  const char *testo_rimanente = testo_completo;

  // Ciclo finché c'è ancora qualcosa da stampare e le righe del monitor non
  // sono esaurite

  // finché devo stampare qualcosa e la riga a cui sono arrivato è minore di
  // MAX_PRINT_LINES, eseguo il ciclo
  while (strlen(testo_rimanente) > 0 && *righe_attuali < MAX_PRINT_LINES) {

    // caso in cui il testo rimanente sta tutto sulla riga corrente
    if (strlen(testo_rimanente) <= max_caratteri) {
      strcpy(colonna[*righe_attuali], testo_rimanente);
      (*righe_attuali)++;
      break;
    }

    // caso in cui il testo è troppo lungo e va troncato.
    int punto_di_taglio = max_caratteri;
    // cerco l'ultimo spazio partendo dalla fine del testo rimanente in modo da
    // non troncare parole a meta
    while (punto_di_taglio > 0 && testo_rimanente[punto_di_taglio] != ' ') {
      punto_di_taglio--;
    }

    // se non ho trovato uno spazio, allora taglio la parola
    if (punto_di_taglio == 0) {
      punto_di_taglio = max_caratteri;
    }

    // Salvo la mezza frase elaborata

    // salvo la parte tagliata nella riga corrente della colonna, aggiungendo il
    // terminatore di stringa
    strncpy(colonna[*righe_attuali], testo_rimanente, punto_di_taglio);
    colonna[*righe_attuali][punto_di_taglio] = '\0';
    // incremento il numero di righe usate
    (*righe_attuali)++;

    // avanzo il puntatore al testo rimanente dopo il punto di taglio
    testo_rimanente += punto_di_taglio;

    // ignoro eventuali spazi in modo che la riga successiva non inizi con degli
    // spazi
    while (*testo_rimanente == ' ') {
      testo_rimanente++;
    }
  }

  // dopo che ho finito di impaginare ogni card, se non ho finito le righe,
  // aggiungo una riga vuota per staccare la card dopo
  if (*righe_attuali < MAX_PRINT_LINES) {
    strcpy(colonna[*righe_attuali], "");
    (*righe_attuali)++;
  }
}

// handler di SHOW_LAVAGNA che si occupa di stampare la lavagna in modo
// leggibile
void handler_show_lavagna() {
  int total_width = COL_WIDTH * 3 + 6; // 3 colonne + 2 separatori + 4 extra

  // riga superiore di "=" per il titolo
  printf("\n");
  for (int i = 0; i < total_width; i++)
    printf("=");
  printf("\n");

  // titolo della lavagna
  char title[] = "STATO LAVAGNA KANBAN";
  int title_pad_l = (total_width - strlen(title)) / 2;
  int title_pad_r = total_width - strlen(title) - title_pad_l;
  // stampo il titolo centrato
  printf("%*s%s%*s\n", title_pad_l, "", title, title_pad_r, "");

  // riga inferiore di "=" per il titolo
  for (int i = 0; i < total_width; i++)
    printf("=");
  printf("\n");

  // nome delle colonne
  char h_todo[] = "[ TO DO ]";
  char h_doing[] = "[ DOING ]";
  char h_done[] = "[ DONE ]";

  // calcolo il padding p di ogni colonna per avere i nomi delle colonne
  // centrati
  int p_todo_l = (COL_WIDTH - strlen(h_todo)) / 2;
  int p_todo_r = COL_WIDTH - strlen(h_todo) - p_todo_l;
  int p_doing_l = (COL_WIDTH - strlen(h_doing)) / 2;
  int p_doing_r = COL_WIDTH - strlen(h_doing) - p_doing_l;
  int p_done_l = (COL_WIDTH - strlen(h_done)) / 2;
  int p_done_r = COL_WIDTH - strlen(h_done) - p_done_l;

  // stampo gli header delle colonne con relativi padding
  printf("%*s%s%*s | %*s%s%*s | %*s%s%*s\n", p_todo_l, "", h_todo, p_todo_r, "",
         p_doing_l, "", h_doing, p_doing_r, "", p_done_l, "", h_done, p_done_r,
         "");

  // linea "-" per separare gli header dalle righe delle card, con i "+" al
  // centro
  for (int i = 0; i < COL_WIDTH + 1; i++)
    printf("-");
  printf("+");
  for (int i = 0; i < COL_WIDTH + 2; i++)
    printf("-");
  printf("+");
  for (int i = 0; i < COL_WIDTH + 1; i++)
    printf("-");
  printf("\n");

  // i tre array che corrisono alle tre colonne, ogni riga è una riga della
  // colonna
  char txt_todo[MAX_PRINT_LINES][COL_WIDTH + 1];
  char txt_doing[MAX_PRINT_LINES][COL_WIDTH + 1];
  char txt_done[MAX_PRINT_LINES][COL_WIDTH + 1];

  // indicatori di avanzamento riga delle relative 3 colonne
  int righe_todo = 0, righe_doing = 0, righe_done = 0;

  // buffer temporaneo per salvare il testo formattato con snprintf prima di
  // passarlo a formatta_testo_colonna
  char buffer_temporaneo[512];

  // scorro tutte le card
  for (int i = 0; i < MAX_CARDS; i++) {
    // ignoro lo slot card inutilizzato
    if (lista_cards[i].id == 0)
      continue;

    // per ogni card, controllo in che colonna si trova e poi eseguo gli stessi
    // step
    // 1. creo una stringa con l'id e il testo della card e la salvo nel buffer
    // temporaneo
    // 2. passo il buffer_temporaneo alla funzione formatta_testo_colonna,
    // insieme al puntatore righe_todo e all'array txt_todo

    // TODO
    if (lista_cards[i].colonna == CARD_TODO) {
      // 1. creo una stringa con l'id e il testo della card e la salvo nel
      // buffer temporaneo
      snprintf(buffer_temporaneo, sizeof(buffer_temporaneo), "#%u: %s",
               lista_cards[i].id, lista_cards[i].testo);
      // 2. passo il buffer_temporaneo alla funzione formatta_testo_colonna,
      // insieme al puntatore righe_todo e all'array txt_todo
      formatta_testo_colonna(txt_todo, &righe_todo, buffer_temporaneo);
    }
    // DOING
    else if (lista_cards[i].colonna == CARD_DOING) {
      snprintf(buffer_temporaneo, sizeof(buffer_temporaneo), "#%u [P:%u]: %s",
               lista_cards[i].id, lista_cards[i].porta_utente,
               lista_cards[i].testo);
      formatta_testo_colonna(txt_doing, &righe_doing, buffer_temporaneo);
    }
    // DONE
    else if (lista_cards[i].colonna == CARD_DONE) {
      struct tm *tm_info = localtime(&lista_cards[i].timestamp);
      char dt_str[10];
      strftime(dt_str, sizeof(dt_str), "%H:%M:%S", tm_info);

      snprintf(buffer_temporaneo, sizeof(buffer_temporaneo),
               "#%u [P:%u] [%s]: %s", lista_cards[i].id,
               lista_cards[i].porta_utente, dt_str, lista_cards[i].testo);
      formatta_testo_colonna(txt_done, &righe_done, buffer_temporaneo);
    }
  }

  // cerco quale colonna ha più righe, in modo da sapere quante righe devo
  // stampare
  int colonne_massime = righe_todo;
  if (righe_doing > colonne_massime)
    colonne_massime = righe_doing;
  if (righe_done > colonne_massime)
    colonne_massime = righe_done;

  // se non ci sono card, stampo (Vuoto) al centro di ogni colonna
  if (colonne_massime == 0) {
    int pad_v = (COL_WIDTH - 8) / 2;
    int pad_r = COL_WIDTH - 8 - pad_v;
    printf("%*s(Vuoto)%*s | %*s(Vuoto)%*s | %*s(Vuoto)%*s\n", pad_v, "", pad_r,
           "", pad_v, "", pad_r, "", pad_v, "", pad_r, "");
  }

  // STAMPA
  // per ogni riga del terminale, stampo la riga corrispondente di ogni colonna
  // formattandola se una colonna non ha righe, stampo una stringa vuota
  for (int curr = 0; curr < colonne_massime; curr++) {
    printf("%-*s | %-*s | %-*s\n", COL_WIDTH,
           (curr < righe_todo) ? txt_todo[curr] : "", COL_WIDTH,
           (curr < righe_doing) ? txt_doing[curr] : "", COL_WIDTH,
           (curr < righe_done) ? txt_done[curr] : "");
  }

  for (int i = 0; i < total_width; i++)
    printf("=");
  printf("\n> ");
  fflush(stdout);
}

// resetta un dato slot utente per poter essere riutilizzato
void free_slot(int sd) {
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (lista_utenti[i].socket == sd) {
      lista_utenti[i].socket = 0;
      lista_utenti[i].porta = 0;
      lista_utenti[i].attivo = 0;
      lista_utenti[i].occupato = 0;
      lista_utenti[i].card_proposta = 0;
      lista_utenti[i].card_assegnata = 0;
      lista_utenti[i].avvertito = 0;
      return;
    }
  }
}

// restituisce l'indice del primo slot utente disponibile, -1 altrimenti
int find_free_slot() {
  for (int i = 0; i < MAX_UTENTI; i++) {
    if (!lista_utenti[i].attivo)
      return i;
  }
  return -1;
}

// inizializza la lavagna con 10 cards di default
void inizializza_lavagna() {
  // array con le descrizioni delle prime 10 card
  const char *defaul_cards[10] = {
      "Implementa back-end della banca",
      "Implementa sito web per i pagamenti",
      "Aggiusta la funzione ordina()",
      "Crea una documentazione per il sito della banca",
      "Creare un'inizializzazione del database della banca",
      "Testare la disconnessione improvvisa degli utenti",
      "Aggiorna tabelle del database",
      "Riduci il costo computazionale della funzione paga()",
      "Aggiorna le dipendenze",
      "Revisiona il codice tramite Valgrind"};

  // creo le 10 card e le assegno nella lista
  for (int i = 0; i < 10; i++) {
    lista_cards[i].id = i + 1;
    lista_cards[i].colonna = CARD_TODO;
    strcpy(lista_cards[i].testo, defaul_cards[i]);

    // inizializzo gli altri campi a zero
    lista_cards[i].porta_utente = 0;
    lista_cards[i].timestamp = 0;
    lista_cards[i].in_attesa_pong = 0;
    lista_cards[i].timestamp_ping = 0;
  }

  numero_cards = 10;
  printf(
      "\n[NOTIFICA] Lavagna inizializzata con 10 card in To Do.\n"); // INFO LOG
}