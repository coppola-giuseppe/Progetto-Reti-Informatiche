# Variabili
CC = gcc
#	cerca gli include nella cartella /include
CFLAGS = -Wall -I./include

HEADERS = include/*.h

# Regola generale
all: lavagna utente

# Regola per l'eseguibile Lavagna
lavagna: build/lavagna.o
	$(CC) build/lavagna.o -o lavagna

# Regola per il file oggetto Lavagna
build/lavagna.o: src/lavagna.c $(HEADERS)
	$(CC) $(CFLAGS) -c src/lavagna.c -o build/lavagna.o

# Regola per l'eseguibile Utente
utente: build/utente.o
	$(CC) build/utente.o -o utente

# Regola per il file oggetto Utente
build/utente.o: src/utente.c $(HEADERS)
	$(CC) $(CFLAGS) -c src/utente.c -o build/utente.o

# Regola per pulire i file creati
clean:
	rm -f lavagna utente build/*.o