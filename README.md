# MyTurn - gruppo 4 GOPHIES

Progetto di TPSIT (5CI). Una scheda **NUCLEO-L476RG** fa da pulsante per chiedere il turno, un programma **Go** sul PC fa da dashboard: legge lo stato della scheda e lo cambia, anche dal browser tramite un'API JSON.

| LED | stato | significato |
|---|---|---|
| spento | `OFF` | nessuna richiesta |
| lampeggia | `PENDING` | turno richiesto |
| fisso | `APPROVED` | turno approvato (solo dal PC) |

## Cartelle

```
myturn-scheda/   firmware (STM32CubeIDE, HAL)
go-scheda/       programma Go: comandi da terminale + server API + pagina web
go-scheda/bruno/ collezione Bruno con le richieste di prova
riferimento/     main.c di riferimento e appunti di lavoro
```

## Seriale

USART2 tramite ST-Link (porta COM virtuale), 115200 baud 8N1. Un messaggio JSON per riga, terminato da `\n` (la scheda accetta CR o LF).

| direzione | messaggio | quando |
|---|---|---|
| scheda → PC | `{"event":"REQUEST"}` | B1 premuto da OFF |
| scheda → PC | `{"event":"RELEASE"}` | B1 premuto da PENDING o APPROVED |
| PC → scheda | `{"command":"SET_LED","state":"APPROVED"}` | cambia stato, nessuna risposta |
| PC → scheda | `{"command":"GET_LED"}` | risposta `{"led":"APPROVED"}` |

Il bottone non può mettere APPROVED: lo stato approvato arriva solo dal PC.

## Firmware

Importare `myturn-scheda` in STM32CubeIDE (File → Import → Existing Projects), compilare e caricare con Run.

- nessun `HAL_Delay` nel ciclo: antirimbalzo (50 ms) e lampeggio (250 ms) usano `HAL_GetTick()`
- ricezione UART a interrupt, un byte alla volta; il comando viene elaborato nel `main`
- una sola variabile di stato, condivisa da bottone e PC

## Programma Go

Serve Go 1.24 o successivo.

```
cd go-scheda
go run . porte                 # elenca le porte COM
go run . ascolta COM6          # stampa "richiesta di turno" / "rilascio" a ogni pressione di B1
go run . set COM6 APPROVED     # OFF, PENDING o APPROVED
go run . get COM6              # stato del LED, errore se la scheda non risponde entro 2 s
go run . server COM6           # API + pagina su http://localhost:8081
```

Il numero della porta COM può cambiare quando si ricollega la scheda: controllarlo con `go run . porte`.

## API

Il server tiene aperta la seriale finché è acceso e ascolta su tutte le interfacce (porta 8081), quindi è raggiungibile anche da un altro PC della rete con `http://IP-DEL-PC:8081`.

| richiesta | risposta |
|---|---|
| `GET /led` | `200 {"led":"PENDING"}` |
| `PUT /led` con `{"state":"APPROVED"}` | `200 {"led":"APPROVED"}` |
| `GET /` | pagina con i pulsanti OFF, PENDING, APPROVED |

Errori sempre come `{"error":"..."}`:

| caso | status |
|---|---|
| stato inesistente, corpo non JSON, `state` mancante | 400 |
| metodo diverso da GET/PUT su `/led` | 405 |
| percorso inesistente | 404 |
| la scheda non risponde entro 2 s | 504 |
| altro errore della seriale | 500 |

Prova veloce (Git Bash):

```bash
curl localhost:8081/led
curl -X PUT localhost:8081/led -d '{"state":"APPROVED"}'
curl -i -X PUT localhost:8081/led -d '{"state":"BLU"}'    # 400
```

## Storia delle consegne

`inizio` → `tappa 1` (la scheda parla sulla seriale) → `tappa 2` (programma Go) → `api` (server HTTP + pagina)
