# Gruppo 4 GOPHIES — Laboratorio «dalla scheda a Go»

Scheda: **NUCLEO-L476RG** · 3 ore · Tappa 1 (scheda, ~70 min) + Tappa 2 (Go, ~85 min)

## Cosa c'è già sul PC (cartella `C:\Users\h\STM32CubeIDE\workspace_2.2.0`)

| File | Cosa è | Stato |
|---|---|---|
| `riferimento/main_tappa1.c` | `main.c` completo della Tappa 1 | compilato con la HAL L4, 0 warning |
| `go-scheda/main.go` + `go.mod` | programma Go con `porte`, `ascolta`, `set`, `get` | compilato per Windows, provato con una scheda simulata |
| `.gitignore` | le 4 righe della scheda | pronto |

**Attenzione:** il progetto CubeMX vero (`STM-LED-3-Stati`, quello con `B1_Pin` e `LD2_Pin`) **non c'è più**. Era sul Desktop e lì non si trova. Le cartelle `led` ed `esercizioTPSIT` del workspace non hanno la HAL e non compilano. Il codice incollato inoltre **non aveva la USART2 attivata**: niente `huart2` e niente `MX_USART2_UART_Init`. Quindi prima di tutto va creato un progetto nuovo (passo 0).

---

## Piano dei tempi e dei ruoli

I ruoli cambiano a ogni passo. Chiamate A, B, C i tre membri.

| Ora (circa) | Passo | Tastiera | Controllo | Terminale |
|---|---|---|---|---|
| 0:00–0:15 | 0. Progetto CubeMX + USART2 + repo Git | A | B | C |
| 0:15–0:35 | Codice Tappa 1 nel `main.c`, compila, flash | B | C | A |
| 0:35–0:55 | Passi 1–2 (bottone) + 6 (lampeggio non blocca) | C | A | B |
| 0:55–1:10 | Passi 3–5 (SET_LED, GET_LED, B1 dopo APPROVED) → firma → **commit "tappa 1"** | A | B | C |
| 1:10–1:15 | pausa / chiudere la console seriale | – | – | – |
| 1:15–1:35 | Tappa 2: `go mod init`, `go get`, 2.1 `porte` | B | C | A |
| 1:35–1:55 | 2.2 `ascolta` | C | A | B |
| 1:55–2:25 | 2.3 `set` + 2.4 `get` → firma → **commit "tappa 2"** | A | B | C |
| 2:25–2:45 | Bonus (se avanza tempo) | B | C | A |
| 2:45–3:00 | margine + ripasso delle domande del prof | tutti | | |

---

## Passo 0 — Progetto della scheda (CubeIDE 2.x usa CubeMX separato)

1. Aprite **STM32CubeMX** → *New Project* → **Board Selector** → `NUCLEO-L476RG` → *Start Project*. Alla domanda «Initialize all peripherals with their default Mode?» rispondete **Yes**.
2. **Connectivity → USART2**: Mode = *Asynchronous*. Nei *Parameter Settings* controllate 115200 baud, 8 bit, nessuna parità, 1 stop.
3. **USART2 → NVIC Settings**: spuntate **USART2 global interrupt**. Senza questa spunta la scheda non riceve niente dal PC.
4. **Project Manager**: nome `myturn-scheda`, location `C:\Users\h\STM32CubeIDE\workspace_2.2.0`, Toolchain **STM32CubeIDE** → **GENERATE CODE**.
5. In CubeIDE: *File → Import → General → Existing Projects into Workspace* → scegliete `myturn-scheda`.
6. Aprite `Core/Src/main.c` e copiate da `riferimento/main_tappa1.c` **solo le parti tra `USER CODE BEGIN` e `USER CODE END`**: Includes, PTD, PD, PV, PFP, 0, 2, 3, 4. Il resto lo genera CubeMX.
7. Build (martello) → Run. La console: *Open Console → Command Shell Console → Serial Port → New*, porta «STLink Virtual COM Port», 115200.

> Se ritrovate il progetto vecchio su un altro PC, basta rifare lì i passi 2, 3 e 6.

---

## Repository Git (una volta sola, Git Bash dentro il workspace)

```bash
cd ~/STM32CubeIDE/workspace_2.2.0
# le due cartelle vecchie non compilano: spostatele fuori dal workspace prima dell'add
git init --bare -b main //OLIVETTI/condivisa/5CI2627/GIT/myturn-gruppo-04.git
git init -b main
# .gitignore c'è già (.metadata/ · Debug/ · Release/ · *.exe)
git add -A
git commit -m "inizio"
git remote add origin //OLIVETTI/condivisa/5CI2627/GIT/myturn-gruppo-04.git
git push -u origin main
```

Chiedete al prof se il numero va scritto `04` o `4`. A ogni tappa: `git add -A` → `git commit -m "tappa 1"` (o `"tappa 2"`) → `git push`.

Se Git chiede nome ed email: `git config user.name "GOPHIES"` e `git config user.email "..."`.

---

## Tappa 1 — come funziona il codice della scheda

### L'idea in una frase
C'è **una sola variabile** `ledState` (OFF, PENDING, APPROVED). Il bottone e il PC cambiano quella, e il LED la segue. Il `while(1)` non si ferma mai: niente `HAL_Delay`.

### Cosa è cambiato rispetto al codice vecchio
| Prima | Ora | Perché |
|---|---|---|
| `state = (state+1) % 3`: il bottone arrivava anche ad APPROVED | `onButtonPressed()`: OFF→PENDING, altrimenti →OFF | APPROVED lo mette solo il PC |
| `HAL_Delay(50)` come antirimbalzo | antirimbalzo con `HAL_GetTick()` | passo 6: il programma non deve bloccarsi |
| variabili locali in `main` | `ledState` globale | bottone e PC devono cambiare la stessa variabile (passo 5) |
| nessuna seriale | `sendLine()` + ricezione a interrupt | i passi 1–4 |
| lampeggio ogni 50 ms | ogni 250 ms | a 50 ms sembrava quasi acceso fisso |

### I pezzi principali
- **`sendLine(testo)`**: `HAL_UART_Transmit` del testo e poi di `"\r\n"`. Ogni riga finisce con un a capo, altrimenti le righe escono attaccate.
- **Antirimbalzo (USER CODE 3)**: quando la lettura di B1 cambia, faccio partire un cronometro (`lastChange`). Considero il cambio vero solo se la lettura resta uguale per 50 ms. Se il nuovo stato stabile è «premuto» (`GPIO_PIN_RESET`, perché B1 è attivo basso) chiamo `onButtonPressed()` **una volta sola**.
- **`HAL_UART_RxCpltCallback`** (USER CODE 4): la HAL la chiama a ogni byte ricevuto. Accumulo i byte in `rxBuf`. Con `'\r'` **oppure** `'\n'` la riga è finita: la copio in `cmdLine` e alzo `cmdReady = 1`. Alla fine rilancio `HAL_UART_Receive_IT` per il byte successivo.
- **`handleCommand(riga)`**: nel ciclo principale, non nell'interrupt. Con `strstr` cerco `"SET_LED"`, `"GET_LED"` e lo stato. SET_LED cambia `ledState` e non risponde. GET_LED risponde `{"led":"..."}`.
- **`updateLed()`**: OFF → spento, APPROVED → acceso, PENDING → toggle quando sono passati 250 ms (`HAL_GetTick() - lastToggle`).
- **`HAL_UART_ErrorCallback`**: se arrivano byte troppo in fretta (overrun) la HAL ferma la ricezione, e qui la riavviamo.

### Quale riga fa passare quale passo
| Passo | Codice responsabile |
|---|---|
| 1 B1 → REQUEST + lampeggio | antirimbalzo in USER CODE 3 + `onButtonPressed` + `sendLine` |
| 2 B1 di nuovo → RELEASE | `onButtonPressed`, ramo `else` |
| 3 SET_LED APPROVED → LED fisso, nessuna risposta | `HAL_UART_RxCpltCallback` (CR o LF) + `handleCommand` |
| 4 GET_LED → `{"led":"APPROVED"}` | `handleCommand`, ramo GET_LED |
| 5 B1 da APPROVED → RELEASE | `onButtonPressed` usa la stessa `ledState` cambiata dal PC |
| 6 bottone sempre pronto | nessun `HAL_Delay`: lampeggio e antirimbalzo usano `HAL_GetTick()` |

Per i passi 3 e 4 dalla console di CubeIDE: scrivete la riga e premete Invio. La console manda CR, e la scheda accetta sia CR sia LF.

---

## Tappa 2 — il programma Go (`go-scheda/main.go`)

Dentro `go-scheda`, nel terminale di VS Code:
```bash
go get go.bug.st/serial@v1.6.2     # scarica la libreria (go.mod c'è già)
go run . porte
go run . ascolta COM4              # Ctrl+C per uscire
go run . set COM4 APPROVED
go run . set COM4 BLU              # deve dare errore e non scrivere niente
go run . get COM4
```
Prima **chiudete la console seriale di CubeIDE**: la porta la può usare un solo programma alla volta.

### Come è fatto
- **`main`** legge `os.Args[1]` (il comando) e con uno `switch` chiama `porte`, `ascolta`, `set` o `get`. Ogni funzione restituisce un `error`, e `main` lo stampa e termina con codice 1.
- **`apri(nome)`**: `serial.Open(nome, &serial.Mode{BaudRate: 115200})`. Ogni comando apre la porta e la chiude con `defer porta.Close()`.
- **`porte`**: `serial.GetPortsList()` e stampa i nomi.
- **`ascolta`**: `bufio.NewScanner(porta)` legge riga per riga. `strings.Contains` riconosce REQUEST → «richiesta di turno» e RELEASE → «rilascio». Qui **non** c'è timeout, perché deve aspettare per sempre.
- **`set`**: **prima** controlla che lo stato sia OFF, PENDING o APPROVED, e solo dopo apre la porta. Per questo con `BLU` non scrive niente. Il JSON lo crea `json.Marshal` dalla struct `Comando`, e il tag `omitempty` toglie `state` quando è vuoto. Poi aggiunge `'\n'` e chiama `Drain()` per essere sicuro che i byte siano partiti prima di chiudere.
- **`get`**: manda `GET_LED` e poi chiama `leggiRiga` con una scadenza di 2 secondi. `SetReadTimeout(100ms)` fa tornare `Read` anche se non arriva niente, così controlliamo l'orologio invece di restare bloccati. Se la riga è un evento del bottone (non ha `"led"`) la salta e legge la successiva. La risposta si legge con `json.Unmarshal` nella struct `RispostaLed`.

### Già provato con una scheda simulata
```
$ set PORTA APPROVED   → inviato: LED -> APPROVED   (la scheda riceve {"command":"SET_LED","state":"APPROVED"})
$ get PORTA            → LED: APPROVED              (salta un REQUEST arrivato in mezzo)
$ set PORTA BLU        → errore: stato "BLU" non valido, usa uno tra [OFF PENDING APPROVED]
$ get (scheda muta)    → errore: la scheda non ha risposto entro 2 secondi   (dopo 2,0 s)
$ ascolta              → richiesta di turno / rilascio
```

### Bonus (solo se avanza tempo): `set` controlla il proprio lavoro
In `set`, dopo `scrivi(...)`, aggiungete:
```go
if err := scrivi(porta, Comando{Command: "GET_LED"}); err != nil {
	return err
}
scadenza := time.Now().Add(attesaMassima)
for {
	riga, err := leggiRiga(porta, scadenza)
	if err != nil {
		return err
	}
	var r RispostaLed
	if json.Unmarshal([]byte(riga), &r) == nil && r.Led != "" {
		if r.Led != stato {
			return fmt.Errorf("la scheda è in %s, non in %s", r.Led, stato)
		}
		fmt.Println("verificato: LED ->", r.Led)
		return nil
	}
}
```
Lo stesso ciclo c'è già in `get`. Per farlo «bene» si sposta in una funzione `attendiLed(porta)` usata da tutte e due.

---

## Errori comuni
| Sintomo | Causa |
|---|---|
| Nessuna riga in console | USART2 non attivata nel `.ioc`, porta sbagliata o baud diverso da 115200 |
| Righe attaccate o «a scaletta» | manca `"\r\n"` in `sendLine` |
| SET_LED non fa niente | manca la spunta *USART2 global interrupt* (NVIC) o `HAL_UART_Receive_IT` in USER CODE 2 |
| Due righe per una pressione | antirimbalzo troppo corto: alzate `DEBOUNCE_MS` |
| `undefined: huart2` o errori in `main.c` | avete copiato tutto il file invece dei soli blocchi USER CODE, oppure la USART2 non è attivata |
| Go: `Access is denied` / `Serial port busy` | la console di CubeIDE (o `ascolta`) tiene la porta occupata |
| Go: `go.mod file not found` | il terminale non è dentro `go-scheda` |
| `get` resta bloccato | non succede: dopo 2 s esce con errore. Se succede, controllate di non aver tolto `SetReadTimeout` |
| Git: `.lock` / `File exists` | cancellate il file `.git/index.lock` e rifate il comando |

---

## Domande che il prof potrebbe fare (con la risposta)
1. **Perché niente `HAL_Delay`?** Ferma tutto il programma. Mentre aspetta, il bottone non viene letto e i comandi non vengono elaborati (passo 6). Con `HAL_GetTick()` misuriamo il tempo passato senza fermarci.
2. **Come funziona l'antirimbalzo?** Il contatto meccanico «rimbalza» per qualche millisecondo. Accettiamo il cambio solo se la lettura resta uguale per 50 ms.
3. **Perché la ricezione è a interrupt?** I byte arrivano quando vuole il PC. Con l'interrupt nessun byte va perso anche mentre il `while` fa altro.
4. **Perché il comando si elabora nel `main` e non nella callback?** La callback deve essere velocissima. In più `HAL_UART_Transmit` (la risposta a GET_LED) è bloccante e non va chiamata dentro un interrupt.
5. **Perché `cmdReady` è `volatile`?** È scritta dall'interrupt e letta dal `main`. `volatile` obbliga il compilatore a rileggerla ogni volta dalla memoria.
6. **Perché bottone e PC usano la stessa variabile?** Se il PC mette APPROVED e poi premo B1, il bottone deve «vedere» APPROVED e mandare RELEASE (passo 5).
7. **Go: perché `set BLU` non scrive niente?** Il controllo `valido(stato)` avviene prima di aprire la porta.
8. **Go: come fa `get` a non bloccarsi?** Con `SetReadTimeout` `Read` torna dopo 100 ms anche senza dati, e il ciclo controlla la scadenza dei 2 secondi.
9. **Go: perché `defer porta.Close()`?** Chiude la porta qualunque cosa succeda, anche se la funzione esce con un errore.
10. **Go: cosa fa `omitempty`?** Se `State` è vuoto il campo non compare nel JSON, quindi GET_LED diventa `{"command":"GET_LED"}`.
