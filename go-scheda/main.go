// Programma Go che fa la parte della dashboard di MyTurn:
// legge cosa succede sulla scheda e le dice cosa fare.
//
// Uso:
//
//	go run . porte                 elenca le porte seriali
//	go run . ascolta COM4          stampa "richiesta di turno" / "rilascio" a ogni pressione di B1
//	go run . set COM4 APPROVED     cambia lo stato del LED (OFF, PENDING, APPROVED)
//	go run . get COM4              stampa lo stato del LED (errore dopo 2 secondi senza risposta)
//
// Ogni comando apre la porta, fa una cosa sola e la chiude.
package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	"go.bug.st/serial"
)

// Comando è il messaggio JSON che il PC manda alla scheda.
// omitempty: se State è vuoto (GET_LED) il campo non viene scritto.
type Comando struct {
	Command string `json:"command"`
	State   string `json:"state,omitempty"`
}

// RispostaLed è la risposta della scheda a GET_LED: {"led":"APPROVED"}
type RispostaLed struct {
	Led string `json:"led"`
}

// Gli stati che la scheda conosce.
var statiValidi = []string{"OFF", "PENDING", "APPROVED"}

// Quanto aspettiamo la risposta a GET_LED prima di arrenderci.
const attesaMassima = 2 * time.Second

var ErrTimeout = errors.New("la scheda non ha risposto entro 2 secondi")

func main() {
	if len(os.Args) < 2 {
		uso()
		os.Exit(1)
	}

	var err error
	switch os.Args[1] {
	case "porte":
		err = porte()
	case "ascolta":
		if len(os.Args) < 3 {
			uso()
			os.Exit(1)
		}
		err = ascolta(os.Args[2])
	case "set":
		if len(os.Args) < 4 {
			uso()
			os.Exit(1)
		}
		err = set(os.Args[2], os.Args[3])
	case "get":
		if len(os.Args) < 3 {
			uso()
			os.Exit(1)
		}
		var stato string
		stato, err = get(os.Args[2])
		if err == nil {
			fmt.Println("LED:", stato)
		}
	default:
		uso()
		os.Exit(1)
	}

	if err != nil {
		fmt.Fprintln(os.Stderr, "errore:", err)
		os.Exit(1)
	}
}

func uso() {
	fmt.Println("uso: go run . porte | ascolta PORTA | set PORTA STATO | get PORTA")
	fmt.Println("     STATO = OFF, PENDING oppure APPROVED")
}

// apri apre la porta a 115200 baud, come la console di CubeIDE.
func apri(nome string) (serial.Port, error) {
	porta, err := serial.Open(nome, &serial.Mode{BaudRate: 115200})
	if err != nil {
		return nil, fmt.Errorf("non riesco ad aprire %s (la console di CubeIDE è ancora aperta?): %w", nome, err)
	}
	return porta, nil
}

// 2.1 — porte: elenca le porte seriali del PC.
func porte() error {
	nomi, err := serial.GetPortsList()
	if err != nil {
		return err
	}
	if len(nomi) == 0 {
		fmt.Println("nessuna porta seriale trovata")
		return nil
	}
	for _, n := range nomi {
		fmt.Println(n)
	}
	return nil
}

// 2.2 — ascolta: legge riga per riga e traduce gli eventi del bottone.
// Si ferma con Ctrl+C. Qui niente timeout: aspettare per sempre è proprio quello che vogliamo.
func ascolta(nome string) error {
	porta, err := apri(nome)
	if err != nil {
		return err
	}
	defer porta.Close()

	fmt.Println("in ascolto su", nome, "(Ctrl+C per uscire)")
	scanner := bufio.NewScanner(porta)
	for scanner.Scan() {
		riga := scanner.Text()
		switch {
		case strings.Contains(riga, "REQUEST"):
			fmt.Println("richiesta di turno")
		case strings.Contains(riga, "RELEASE"):
			fmt.Println("rilascio")
		}
	}
	return scanner.Err()
}

// 2.3 — set: controlla lo stato PRIMA di scrivere, poi manda SET_LED.
func set(nome, stato string) error {
	stato = strings.ToUpper(stato)
	if !valido(stato) {
		// errore prima di aprire la porta: sulla seriale non arriva niente
		return fmt.Errorf("stato %q non valido, usa uno tra %v", stato, statiValidi)
	}

	porta, err := apri(nome)
	if err != nil {
		return err
	}
	defer porta.Close()

	if err := scrivi(porta, Comando{Command: "SET_LED", State: stato}); err != nil {
		return err
	}
	fmt.Println("inviato: LED ->", stato)
	return nil
}

// 2.4 — get: manda GET_LED e aspetta {"led":"..."} al massimo 2 secondi.
func get(nome string) (string, error) {
	porta, err := apri(nome)
	if err != nil {
		return "", err
	}
	defer porta.Close()

	porta.ResetInputBuffer() // butto via eventuali righe vecchie
	if err := scrivi(porta, Comando{Command: "GET_LED"}); err != nil {
		return "", err
	}

	scadenza := time.Now().Add(attesaMassima)
	for {
		riga, err := leggiRiga(porta, scadenza)
		if err != nil {
			return "", err
		}
		var r RispostaLed
		// se nel frattempo arriva un evento del bottone, non è la risposta: lo salto
		if json.Unmarshal([]byte(riga), &r) == nil && r.Led != "" {
			return r.Led, nil
		}
	}
}

// scrivi trasforma il comando in JSON e lo manda con "\n" in fondo.
func scrivi(porta serial.Port, c Comando) error {
	dati, err := json.Marshal(c)
	if err != nil {
		return err
	}
	if _, err := porta.Write(append(dati, '\n')); err != nil {
		return err
	}
	return porta.Drain() // aspetto che i byte siano davvero partiti prima di chiudere
}

// leggiRiga legge un byte alla volta fino a '\n', ma si arrende alla scadenza.
// SetReadTimeout fa tornare Read anche se non arriva niente, così possiamo
// controllare l'orologio invece di restare bloccati per sempre.
func leggiRiga(porta serial.Port, scadenza time.Time) (string, error) {
	porta.SetReadTimeout(100 * time.Millisecond)
	var riga []byte
	buf := make([]byte, 1)
	for time.Now().Before(scadenza) {
		n, err := porta.Read(buf)
		if err != nil {
			return "", err
		}
		if n == 0 {
			continue // timeout di 100 ms: riprovo finché c'è tempo
		}
		if buf[0] == '\n' {
			return strings.TrimSpace(string(riga)), nil
		}
		riga = append(riga, buf[0])
	}
	return "", ErrTimeout
}

func valido(stato string) bool {
	for _, s := range statiValidi {
		if s == stato {
			return true
		}
	}
	return false
}
