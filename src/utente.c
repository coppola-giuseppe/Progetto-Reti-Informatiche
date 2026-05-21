#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "commands.h"
#include "kanban.h"
#include "protocol.h"
#include "utente.h"

int main(int argc, char *argv[]) {

  if (argc != 2 || atoi(argv[1]) <= 5678) {
    printf("Uso corretto: ./utente <porta>  (porta >= 5679)\n"); // INFO LOG
    exit(1);
  }

  // SOCKET ASCOLTO PER IL P2P
  int p2p_review, ret;
  struct sockaddr_in p2p_addr;

  p2p_review = socket(AF_INET, SOCK_STREAM, 0);
  if (p2p_review == -1) {
    perror("Errore nella socket per il P2P "); // INFO LOG
    exit(1);
  }

  p2p_addr.sin_family = AF_INET;
  p2p_addr.sin_port = htons(atoi(argv[1]));
  p2p_addr.sin_addr.s_addr = INADDR_ANY; // mi metto in ascolto da qualsiasi ip

  ret = bind(p2p_review, (struct sockaddr *)&p2p_addr, sizeof(p2p_addr));
  if (ret == -1) {
    perror("Errore nella bind per il P2P "); // INFO LOG
    exit(1);
  }

  ret = listen(p2p_review, 10);
  if (ret == -1) {
    perror("Errore nella listen per il P2P "); // INFO LOG
    exit(1);
  }

  // SOCKET LAVAGNA
  int sd_lavagna;
  struct sockaddr_in sv_addr;

  sd_lavagna = socket(AF_INET, SOCK_STREAM, 0);
  if (sd_lavagna == -1) {
    perror("Errore nella socket per la lavagna "); // INFO LOG
    exit(1);
  }

  sv_addr.sin_family = AF_INET;
  sv_addr.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &sv_addr.sin_addr);

  printf("\n[NOTIFICA] Connessione alla lavagna in corso...\n"); // INFO LOG
  ret = connect(sd_lavagna, (struct sockaddr *)&sv_addr, sizeof(sv_addr));
  if (ret == -1) {
    perror("Errore nella connect "); // INFO LOG
    exit(1);
  }

  printf("\n[NOTIFICA] Connesso alla Lavagna! Digita HELLO per registrarti.\n"); // INFO LOG

  // SETUP DELLA SELECT
  fd_set master, read_fds;
  FD_ZERO(&master);
  FD_SET(0, &master);          // metto STDIN nel set master
  FD_SET(sd_lavagna, &master); // metto il socket della Lavagna nel set master
  FD_SET(p2p_review, &master); // metto il socketo per le review nel set master

  int fdmax = sd_lavagna;

  if (p2p_review > fdmax) {
    fdmax = p2p_review;
  }

  // VARIABILI PER LA GESTIONE DELLA SLEEP E DELLO STATO
  StatoLavoro stato_attuale = STATO_LIBERO;
  time_t inizio_lavoro = 0;
  int id_card_in_carico = -1;
  char descrizione_card[MAX_TESTO];
  int registrato = 0;
  int id_card_proposta = -1; // salva l'id della card proposta, serve per
                             // bloccare il comando di ACK senza avere proposte

  printf("Digita un comando:\n> "); // INFO LOG
  fflush(stdout); // forza la stampa del prompt, per avere > a capo

  // cuore dell'utente
  while (1) {

    struct timeval tv = {
        1, 0}; // timer di un secondo per implementare una sorta di sleep() ma
               // rimanendo sensibili ai PING della lavagna

    read_fds = master;

    if (stato_attuale == STATO_REVISIONE) {
      FD_CLR(0,
             &read_fds); // non rispondo più agli input da tastiera se sto
                         // aspettando l'estio della revisione
    }

    select(fdmax + 1, &read_fds, NULL, NULL, &tv);

    // simulazione di una sleep() se sto lavorando
    if (stato_attuale == STATO_LAVORANDO) {
      // chiamata alla mia sleep che simula un'attesa se sono nello stato
      // LAVORANDO
      mia_sleep(sd_lavagna, &stato_attuale, inizio_lavoro, DURATA_LAVORO,
                &master);
    } else if (stato_attuale ==
               STATO_ATTESA_REVISIONE) // se il precedente tentativo di
                                       // revisione è fallito, ne faccio
                                       // ripartire un'altro in automatico
    {                                  // dopo RETRY_REVIEW secondi
      if (time(NULL) - inizio_lavoro >= RETRY_REVIEW) {
        printf(
            "\n[SISTEMA] Tentativo automatico della revisione in corso...\n"); // INFO LOG
        stato_attuale = STATO_REVISIONE;

        struct MsgBase msg_request;
        msg_request.hdr.comando = CMD_REQUEST_USER_LIST;
        msg_request.hdr.lunghezza = 0;
        send(sd_lavagna, &msg_request, sizeof(struct MsgBase), 0);
      }
    }

    for (int i = 0; i <= fdmax; i++) {
      if (FD_ISSET(i, &read_fds)) {

        // tastiera
        if (i == 0) {
          char comando_input[256];
          fgets(comando_input, sizeof(comando_input), stdin);
          comando_input[strcspn(comando_input, "\n")] = 0;

          // se sto lavorando, posso usare solo il comando QUIT
          if (stato_attuale == STATO_LAVORANDO) {
            if (convertitore_comandi(comando_input) != CMD_INPUT_QUIT) {
              printf(
                  "\n[NOTIFICA] Mentre lavori puoi usare solo QUIT.\n>"); // DEBUG
              fflush(stdout);
              continue;
            }
          }

          switch (convertitore_comandi(comando_input)) {

          case CMD_INPUT_HELLO:
            if (registrato) {
              printf("\n[NOTIFICA] Sei già registrato!\n"); // INFO LOG
            } else {
              struct MsgHello msgHello;
              msgHello.hdr.comando = CMD_HELLO;
              msgHello.hdr.lunghezza = htons(sizeof(uint16_t));
              msgHello.porta = htons((uint16_t)atoi(argv[1]));

              send(sd_lavagna, &msgHello, sizeof(struct MsgHello), 0);
              printf("\n[NOTIFICA] Messaggio HELLO inviato alla Lavagna.\n"); // INFO LOG
              registrato = 1;
            }
            break;

          case CMD_INPUT_QUIT:
            printf("\n[NOTIFICA] Disconnessione in corso...\n"); // INFO LOG

            struct MsgBase msg;
            msg.hdr.comando = CMD_QUIT;
            msg.hdr.lunghezza = 0;

            send(sd_lavagna, &msg, sizeof(struct MsgBase), 0);

            close(sd_lavagna);
            exit(0);
            break;

          case CMD_INPUT_CREATE_CARD:
            if (!registrato) {
              printf("\n[NOTIFICA] Devi prima registrarti con HELLO!\n"); // INFO LOG
              break;
            }

            struct MsgCard msg_create;
            msg_create.hdr.comando = CMD_CREATE_CARD;
            int payload = sizeof(struct MsgCard) - sizeof(struct Header);
            msg_create.hdr.lunghezza = htons(payload);

            printf("\nInserisci la descrizione della nuova card: \n"); // INFO LOG
            fgets(msg_create.testo, sizeof(char) * MAX_TESTO, stdin);
            msg_create.testo[strcspn(msg_create.testo, "\n")] = 0;

            send(sd_lavagna, &msg_create, sizeof(struct MsgCard), 0);

            printf("\n[NOTIFICA] Richiesta CREATE_CARD inviata!\n"); // INFO LOG
            break;

          case CMD_INPUT_ACK_CARD:
            if (!registrato) {
              printf("\n[NOTIFICA] Devi prima registrarti con HELLO!\n"); // INFO LOG
            } else if (id_card_proposta == -1) {
              printf("\n[NOTIFICA] Nessuna card proposta al momento.\n"); // INFO LOG
            } else {
              struct MsgAck msg_ack;
              msg_ack.hdr.comando = CMD_ACK_CARD;
              msg_ack.hdr.lunghezza = htons(sizeof(uint32_t));
              msg_ack.id_card = htonl(id_card_proposta);

              send(sd_lavagna, &msg_ack, sizeof(struct MsgAck), 0);
              printf("\n[NOTIFICA] Hai accettato la card ID %d. Messaggio ACK "
                     "inviato!\n",
                     id_card_proposta); // INFO LOG

              id_card_in_carico = id_card_proposta;
              id_card_proposta = -1;

              stato_attuale = STATO_LAVORANDO;
              inizio_lavoro = time(NULL);
              printf("\n[NOTIFICA] Inizio simulazione lavoro... (Tastiera "
                     "bloccata per %d secondi)\n> ",
                     DURATA_LAVORO); // INFO LOG
              fflush(stdout);
            }
            break;

          case CMD_INPUT_REQUEST_USER_LIST:
            struct MsgBase msg_request;
            msg_request.hdr.comando = CMD_REQUEST_USER_LIST;
            msg_request.hdr.lunghezza = 0;
            send(sd_lavagna, &msg_request, sizeof(struct MsgBase), 0);
            printf("\n[NOTIFICA] Hai chiesto alla lavagna la lista degli "
                   "utenti connessi!\n"); // INFO LOG
            break;

          // se la prima revisione automatica è fallita e sono durante le
          // successive attese tra un retry automatico e l'altro posso forzare
          // una review con il comando REVIEW_CARD
          case CMD_INPUT_REVIEW_CARD:
            if (stato_attuale == STATO_ATTESA_REVISIONE) {
              printf(
                  "\n[NOTIFICA] Avvio manuale della revisione...\n"); // INFO LOG
              stato_attuale = STATO_REVISIONE;

              struct MsgBase msg_request;
              msg_request.hdr.comando = CMD_REQUEST_USER_LIST;
              msg_request.hdr.lunghezza = 0;
              send(sd_lavagna, &msg_request, sizeof(struct MsgBase), 0);
            } else
              printf("\n[NOTIFICA] Puoi fare una review manuale solo se sei in "
                     "attesa di altri utenti!\n"); // INFO LOG

            break;

          case CMD_INPUT_UNKNOWN:
          default:
            printf("\n[NOTIFICA] Comando sconosciuto, riprova.\n"); // INFO LOG
            break;
          }
          // ripristino del prompt nello stato libero e in attesa revisione
          if (stato_attuale == STATO_LIBERO ||
              stato_attuale == STATO_ATTESA_REVISIONE) {
            printf("> ");
            fflush(stdout);
          }
        }

        // lavagna
        else if (i == sd_lavagna) {
          struct Header hdr;
          int byte_letti =
              recv(sd_lavagna, &hdr, sizeof(struct Header), MSG_WAITALL);

          if (byte_letti <= 0) {
            printf("\n[NOTIFICA] La Lavagna ha chiuso la connessione. Termino il "
                   "programma.\n"); // INFO LOG
            close(sd_lavagna);
            exit(1);
          } else {
            // converto da network ad host order
            uint16_t len_payload = ntohs(hdr.lunghezza);

            switch (hdr.comando) {
            case CMD_HANDLE_CARD:
              struct MsgHandleCard msg_handle;
              msg_handle.hdr = hdr;

              int byte_ricevuti =
                  recv(sd_lavagna, &msg_handle.card, len_payload, MSG_WAITALL);
              if (byte_ricevuti <= 0) {
                perror("Errore recv payload HANDLE_CARD "); // INFO LOG
                break;
              }

              // salvo l'id in vista dell'ACK
              id_card_proposta = (int)ntohl(
                  msg_handle.card
                      .id); // ho castato a int perché uso id_card_proposta come
                            // variabile locale int
              uint32_t n_utenti = ntohl(msg_handle.numero_utenti);

              strcpy(descrizione_card, msg_handle.card.testo);

              printf("\n[NOTIFICA] Ti è stata proposta un'attività!\n"); // INFO LOG
              printf(" - ID Card: %d\n", id_card_proposta); 
              printf(" - Testo: \"%s\"\n", msg_handle.card.testo); 
              printf(" - Altri utenti connessi: %u\n", n_utenti); 

              if (n_utenti > 0) {
                printf(" - Porte degli altri utenti: "); // INFO LOG
                for (int j = 0; j < n_utenti; j++) {
                  // stampo la porta convertendo da network a host order
                  printf("%u ", ntohs(msg_handle.porte[j]));
                }
                printf("\n");
              }

              printf("[NOTIFICA] Digita ACK_CARD per accettare la card.\n"); // INFO LOG
              printf("> ");
              fflush(stdout);
              break;

            // devo distinguere due casi: richiesta manuale tramite comando e
            // richiesta automatica prima della revisione
            case CMD_SEND_USER_LIST:
              struct MsgList msg_list;
              msg_list.hdr = hdr;
              byte_ricevuti = recv(sd_lavagna, &msg_list.numero_utenti,
                                   len_payload, MSG_WAITALL);
              if (byte_ricevuti <= 0) {
                perror("Errore recv payload SEND_USER_LIST "); // INFO LOG
                break;
              }

              msg_list.numero_utenti = ntohl(msg_list.numero_utenti);

              // se sono in stato revisione, controllo se ci sono altri utenti a
              // cui chiedere la revisione
              if (stato_attuale == STATO_REVISIONE) {
                // se non ci sono altri utenti, faccio partire un timer di
                // DURATA_LAVORO secondi per riprovare automaticamente e durante
                // questa attesa l'utente può digitare REVIEW_CARD per bloccare
                // il timer e far partire manualmente una review
                if (msg_list.numero_utenti == 0) {
                  printf(
                      "\n[NOTIFICA] Nessun altro utente online per la "
                      "revisione. Nuovo tentativo automatico tra %d secondi.\n",
                      RETRY_REVIEW); // INFO LOG
                  printf("[NOTIFICA] Digita REVIEW_CARD per forzare un "
                         "tentativo manuale.\n"); // INFO LOG

                  // setto lo stato di attesa revisione e preparo il timer per
                  // il retry automatico
                  stato_attuale = STATO_ATTESA_REVISIONE;
                  inizio_lavoro = time(NULL);
                  // stampo a tastiera ">"
                  printf("\n> ");
                  fflush(stdout);
                } else {
                  // c'è almeno un altro utente connesso e posso fare la review
                  int porta = atoi(argv[1]);
                  int ret = handler_review_card(sd_lavagna, &msg_list,
                                                id_card_in_carico,
                                                descrizione_card, porta);

                  if (ret == 1) {
                    if (DEBUG_LOG) { printf("\n[NOTIFICA] Revisione ok.\n"); } // DEBUG
                    handler_card_done(sd_lavagna);
                    stato_attuale = STATO_LIBERO;
                    id_card_in_carico = -1;
                  } else { // gestisco allo stesso modo il caso di revisione
                           // rifiutata (reto == 0) o errore di connessione (ret
                           // == -1)
                    printf("\n[NOTIFICA] Revisione fallita. Nuovo tentativo "
                           "automatico tra %d secondi.\n",
                           RETRY_REVIEW); // INFO LOG
                    // se fallisce, riprovo dopo RETRY_REVIEW secondi
                    stato_attuale = STATO_ATTESA_REVISIONE;
                    inizio_lavoro = time(NULL);
                    printf("\n> ");
                    fflush(stdout);
                  }
                }
                /* TODO: probabilmente è levabile
                // stampo il prompt se sono libero o in attesa della revisione
                automatica if (stato_attuale == STATO_LIBERO || stato_attuale ==
                STATO_ATTESA_REVISIONE)
                {
                    printf("> ");
                    fflush(stdout);
                } */
              }

              else {
                printf("\n[NOTIFICA] Ecco la lista degli utenti connessi!\n"); // INFO LOG
                printf(" - Altri utenti connessi: %u\n",
                       msg_list.numero_utenti); // INFO LOG
                if (msg_list.numero_utenti > 0) {
                  printf(" - Porte: "); // INFO LOG
                  for (int j = 0; j < msg_list.numero_utenti; j++) {
                    // stampo la porta convertendo da network a host order
                    printf("%u ", ntohs(msg_list.porte[j]));
                  }
                  printf("\n");
                }
                printf("\n> ");
                fflush(stdout);
              }

              break;

            // se arriva un PING dalla lavagna, rispondo subito con un PONG
            case CMD_PING_USER:
              if (DEBUG_LOG) { printf("\n[NOTIFICA] Ping arrivato dalla lavagna...\n"); } // DEBUG
              struct MsgBase msg_pong;
              msg_pong.hdr.comando = CMD_PONG_LAVAGNA;
              msg_pong.hdr.lunghezza = 0;

              send(sd_lavagna, &msg_pong, sizeof(struct MsgBase), 0);
              if (DEBUG_LOG) { printf("[NOTIFICA] ...ho mandato il PONG\n"); } // DEBUG

              // TODO: probabilmente levabile
              if (stato_attuale == STATO_LIBERO ||
                  stato_attuale == STATO_ATTESA_REVISIONE) {
                printf("> ");
                fflush(stdout);
              }

              break;

            // se arriva un messaggio di errore, mostro il testo
            case CMD_ERROR:

              char testo[MAX_TESTO];
              byte_ricevuti =
                  recv(sd_lavagna, &testo, len_payload, MSG_WAITALL);
              if (byte_ricevuti <= 0) {
                perror("Errore recv payload CMD_ERROR "); // INFO LOG
                break;
              }

              printf("\n[NOTIFICA] Errore ricevuto: %s\n", testo); // INFO LOG

              // mostro ">" nel terminale per permettere l'inserimento di
              // comandi
              if (stato_attuale == STATO_LIBERO) {
                printf("> ");
              }

              fflush(stdout);
              break;

            default:
              // se arriva un comando sconosciuto, leggo i byte solo per
              // svuotare il buffer del socket
              printf(
                  "\n[NOTIFICA] Ricevuto messaggio sconosciuto (Comando: %d)\n",
                  hdr.comando); // INFO LOG
              char buffer_scarto[1024];
              recv(sd_lavagna, buffer_scarto, len_payload, MSG_WAITALL);
            }
            // TODO: probabilmente non serve, da testare
            /* //riabilitazione tastiera
            if (stato_attuale == STATO_LIBERO || stato_attuale ==
            STATO_ATTESA_REVISIONE)
            {
                printf("> ");
                fflush(stdout);
            } */
          }
        }
        // porta peer per la review
        else if (i == p2p_review) {
          if (DEBUG_LOG) { printf("\n[NOTIFICA] Richiesta di revisione ricevuta.\n"); } // DEBUG
          //  creo un socket temporaneo per l'invio della review
          int peer = accept(p2p_review, NULL, NULL);
          if (peer == -1) {
            perror("Errore nella accept del P2P "); // INFO LOG
            continue;
          }

          struct MsgReview msg_review;

          int byte_ricevuti =
              recv(peer, &msg_review.hdr, sizeof(struct Header), MSG_WAITALL);
          if (byte_ricevuti <= 0) { // chiusura della connessione o errore
            if (DEBUG_LOG) { printf("\n[NOTIFICA] L'utente da revisionare sul socket %d si e' disconnesso.\n", i); } // DEBUG
            close(peer);
            continue;
          }
          int payload = ntohs(msg_review.hdr.lunghezza);
          byte_ricevuti = recv(peer, &msg_review.id_card, payload, MSG_WAITALL);
          if (byte_ricevuti <= 0) {
            if (DEBUG_LOG) { printf("\n[NOTIFICA] L'utente da revisionare sul socket %d si e' disconnesso.\n", i); } // DEBUG
            close(peer);
          } else if (byte_ricevuti != payload) {
            if (DEBUG_LOG) { printf("Errore: non sono arrivati tutti i byte durante la revisione.\n"); } // DEBUG
            close(peer);
          }

          // Analisi della review... (viene sempre accettata)

          struct MsgBase msg_review_result;
          msg_review_result.hdr.comando =
              CMD_REVIEW_GOOD; // invio sempre esito positivo alla review come
                               // da specifica
          msg_review_result.hdr.lunghezza = 0;
          if (DEBUG_LOG) { printf("\n[NOTIFICA] Invio del risultato della revisione ricevuta.\n"); } // DEBUG
          send(peer, &msg_review_result, sizeof(struct MsgBase), 0);

          // chiudo il socket temporaneo dopo la review
          close(peer);
        }
      }
    }
  }

  return 0;
}

// mia variante di una sleep non bloccante, in modo da poter rispondere ai ping
// della lavagna durante la fase di lavoro controllo ed aggiorno il timer, se
// sono passati "durata" secondi, passo alla fase revisione
void mia_sleep(int sd_lavagna, StatoLavoro *stato, time_t inizio_lavoro,
               int durata, fd_set *master) {
  // calcolo il tempo passato
  if (time(NULL) - inizio_lavoro >= durata) {
    printf("\n[NOTIFICA] Lavoro completato! (Sleep terminata).\n"); // INFO LOG

    // cambio dello stato
    *stato = STATO_REVISIONE;

    // eseguo CMD_REQUEST_USER_LIST in previsione della fase di review
    struct MsgBase msg_request;
    msg_request.hdr.comando = CMD_REQUEST_USER_LIST;
    msg_request.hdr.lunghezza = 0;
    send(sd_lavagna, &msg_request, sizeof(struct MsgBase), 0);

    FD_SET(0, master);    // mi rimetto in ascolto della tastiera
    tcflush(0, TCIFLUSH); // butta via tutto quello digitato nella fase di
                          // lavoro, per evitare che i comandi vengano fatti
                          // tutti insieme quando riabilito la tastiera
  }
}

// handler che gestisce la fase di review
// chiede alla lavagna una lista delle porte degli utenti e poi crea una
// connessione temporanea con essi e chiede una revisione ritorna -1 in caso di
// errori, 0 se è stata rifiutata da almeno un utente, 1 se è stata accettata da
// tutti
int handler_review_card(int sd_lavagna, struct MsgList *users, int id_card,
                        char desc[MAX_TESTO], int mia_porta) {
  printf("\n[NOTIFICA] Inizio fase di revisione con i %u altri utenti...\n",
         users->numero_utenti); // INFO LOG

  // per ogni utente, creo un socket temporaneo, mando la richiesta di review e
  // aspetto l'esito se anche uno solo risponde negativamente ritorno 0
  // altrimenti 1

  for (int i = 0; i < users->numero_utenti; i++) {

    uint16_t porta = ntohs(users->porte[i]);

    int peer_sd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in peer_addr;
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(porta);
    inet_pton(
        AF_INET, SERVER_IP,
        &peer_addr.sin_addr.s_addr); // uso il server ip perché da specifiche
                                     // girano tutti all'ip "127.0.0.1"

    // connessione all'utente
    int ret =
        connect(peer_sd, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    if (ret == -1) {
      printf("[NOTIFICA] Impossibile contattare l'utente alla porta %u. "
             "Revisione interrotta.\n",
             porta); // INFO LOG
      close(peer_sd);
      return -1;
    }

    // creo il messaggio da inviare
    struct MsgReview msg_review;
    msg_review.hdr.comando = CMD_REVIEW_CARD;
    int payload = sizeof(struct MsgReview) - sizeof(struct Header);
    msg_review.hdr.lunghezza = htons(payload);
    msg_review.id_card = htonl((uint32_t)id_card);
    strcpy(msg_review.testo, desc);

    // invio il messaggio e poi aspetto la risposta
    send(peer_sd, &msg_review, sizeof(struct MsgReview), 0);

    struct MsgBase msg_review_result;
    int byte_ricevuti =
        recv(peer_sd, &msg_review_result, sizeof(struct MsgBase), MSG_WAITALL);
    if (byte_ricevuti <= 0) {
      if (DEBUG_LOG) { printf("\n[NOTIFICA] L'utente recensore sul socket %d si e' disconnesso.\n", i); } // DEBUG
      close(peer_sd);
      return -1;
    } else if (byte_ricevuti != sizeof(struct MsgBase)) {
      printf("\n[NOTIFICA] Errore, non sono arrivati tutti i byte durante la "
             "revisione.\n"); // INFO LOG
      close(peer_sd);
      return -1;
    }

    if (msg_review_result.hdr.comando == CMD_REVIEW_BAD) {
      if (DEBUG_LOG) { printf("\n[NOTIFICA] Un revisore ha rifiutato la tua review.\n"); } // DEBUG
      close(peer_sd);
      return 0;
    }
    close(peer_sd);
  }
  return 1;
}

void handler_card_done(int socket) {

  struct MsgBase msg_done;
  msg_done.hdr.comando = CMD_CARD_DONE;
  msg_done.hdr.lunghezza = 0;
  printf("\n[NOTIFICA] Mando messaggio CARD_DONE.\n"); // INFO LOG
  send(socket, &msg_done, sizeof(struct MsgBase), 0);
}

// converte il comando da stringa ad enum, cosicché possa usare uno switch in
// modo più pulito
Comandi convertitore_comandi(const char *input) {
  if (strcmp(input, "HELLO") == 0)
    return CMD_INPUT_HELLO;
  if (strcmp(input, "QUIT") == 0)
    return CMD_INPUT_QUIT;
  if (strcmp(input, "CREATE_CARD") == 0)
    return CMD_INPUT_CREATE_CARD;
  if (strcmp(input, "CARD_DONE") == 0)
    return CMD_INPUT_CARD_DONE;
  if (strcmp(input, "ACK_CARD") == 0)
    return CMD_INPUT_ACK_CARD;
  if (strcmp(input, "REQUEST_USER_LIST") == 0)
    return CMD_INPUT_REQUEST_USER_LIST;
  if (strcmp(input, "REVIEW_CARD") == 0)
    return CMD_INPUT_REVIEW_CARD;
  return CMD_INPUT_UNKNOWN;
}