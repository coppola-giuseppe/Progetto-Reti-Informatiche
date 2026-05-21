#ifndef COMMANDS_H
#define COMMANDS_H

// VOCABOLARIO DEI COMANDI

//      utente -> lavagna [0-9]
#define CMD_HELLO 1              // utente vuole registrarsi alla lavagna e ci comunica la porta per la revisione
#define CMD_QUIT 2               // utente notificano l'uscita dalla lavagna
#define CMD_ACK_CARD 3           // utente conferma la ricezione di una attività
#define CMD_CARD_DONE 4          // utente notifica la lavagna che l'attività è stata revisionata e può metterla in DONE
#define CMD_PONG_LAVAGNA 5       // utente risponde al ping della lavagna
#define CMD_CREATE_CARD 6        // utente crea una nuova card, assegnando id, colona e testo
#define CMD_REQUEST_USER_LIST 7  // utente chiede il numero di utenti connessi e la lista delle porte di questi

//      lavagna -> utente [10-14]
#define CMD_HANDLE_CARD 10       // lavagna invia una card all'utente
#define CMD_PING_USER 11         // lavagna controlla se l'utente sta ancora svolgendo l'attività
#define CMD_SEND_USER_LIST 12    // lavagna invia all'utente richiedente il numero di utenti connessi e la lista delle porte di questo

//      lavagna [15-19]
#define CMD_SHOW_LAVAGNA 15      // lavagna stampa lo stato attuale delle attività
#define CMD_MOVE_CARD 16         // lavagna muove una card 

//      utente [20-24]
#define CMD_REVIEW_CARD 20       // utente chiede la revisione ad un altro utente
#define CMD_REVIEW_GOOD 21       // utente risponde positivamente ad una review
#define CMD_REVIEW_BAD 22        // utente risponde negativamente ad una review

//      errore
#define CMD_ERROR 13             // messaggio di errore in caso di ricezione di comandi non validi (es. HELLO essendosi già registrato, controllo fatto sia lato lavagna che lato utente)

#endif