// Consegna "api": il browser legge e cambia lo stato del LED tramite un'API
// in Go che risponde in JSON.
//
//	GET  /led                          -> {"led":"PENDING"}
//	PUT  /led  {"state":"APPROVED"}    -> {"led":"APPROVED"}
//	GET  /                             -> pagina con i pulsanti OFF, PENDING, APPROVED
//
// Se qualcosa va storto la risposta è {"error":"..."} con uno status adatto.
package main

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"strings"
	"sync"

	"go.bug.st/serial"
)

// La pagina sta in pagina.html; go:embed la mette dentro l'eseguibile
// al momento della compilazione, così non serve portarsi dietro il file.
//
//go:embed pagina.html
var pagina []byte

// ":8081" senza indirizzo = ascolta su tutte le schede di rete del PC,
// quindi funziona sia localhost:8081 sia IP-DEL-PC:8081 da un altro computer.
const indirizzo = ":8081"

// Scheda tiene aperta la porta seriale per tutto il tempo in cui il server è acceso.
//
// mu è il lucchetto: net/http gestisce ogni richiesta in una goroutine diversa,
// quindi due richieste possono arrivare nello stesso momento (per esempio la
// pagina del prof e Bruno). Senza lucchetto scriverebbero insieme sulla seriale
// e una leggerebbe la risposta dell'altra. Con Lock/Unlock passano una alla volta.
type Scheda struct {
	mu    sync.Mutex
	porta serial.Port
}

// Stato chiede GET_LED alla scheda.
func (s *Scheda) Stato() (string, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return chiediStato(s.porta)
}

// Imposta manda SET_LED e poi rilegge lo stato con GET_LED:
// così la risposta conferma quello che la scheda ha fatto davvero.
func (s *Scheda) Imposta(stato string) (string, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if err := impostaStato(s.porta, stato); err != nil {
		return "", err
	}
	return chiediStato(s.porta)
}

// RichiestaLed è il corpo di PUT /led: {"state":"APPROVED"}
type RichiestaLed struct {
	State string `json:"state"`
}

// Errore è la risposta quando qualcosa va storto: {"error":"..."}
type Errore struct {
	Error string `json:"error"`
}

// server apre la porta una volta sola e avvia l'API.
func server(nome string) error {
	porta, err := apri(nome)
	if err != nil {
		return err
	}
	defer porta.Close()

	scheda := &Scheda{porta: porta}

	// Le rotte "METODO /percorso" (Go 1.22+): vince la più specifica.
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", paginaHandler) // solo "/" esatto
	mux.HandleFunc("GET /led", scheda.getLed) // leggi lo stato
	mux.HandleFunc("PUT /led", scheda.putLed) // cambia lo stato
	mux.HandleFunc("/led", metodoNonAmmesso)  // /led con POST, DELETE, ...
	mux.HandleFunc("/", nonTrovato)           // tutto il resto

	fmt.Println("scheda su", nome, "- server acceso (Ctrl+C per fermarlo)")
	fmt.Println("  da questo PC:   http://localhost:8081/")
	for _, ip := range indirizziLocali() {
		fmt.Printf("  dalla rete:     http://%s:8081/\n", ip)
	}
	return http.ListenAndServe(indirizzo, mux)
}

// GET /led -> {"led":"PENDING"}
func (s *Scheda) getLed(w http.ResponseWriter, r *http.Request) {
	stato, err := s.Stato()
	if err != nil {
		rispondiErrore(w, err)
		return
	}
	rispondiJSON(w, http.StatusOK, RispostaLed{Led: stato})
}

// PUT /led con {"state":"APPROVED"} -> {"led":"APPROVED"}
func (s *Scheda) putLed(w http.ResponseWriter, r *http.Request) {
	// 1. leggo il corpo JSON (al massimo 1 KB: a noi bastano pochi byte)
	var req RichiestaLed
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1024)).Decode(&req); err != nil {
		rispondiJSON(w, http.StatusBadRequest,
			Errore{`corpo non valido, serve un JSON come {"state":"APPROVED"}`})
		return
	}

	// 2. controllo lo stato PRIMA di toccare la scheda
	stato := strings.ToUpper(strings.TrimSpace(req.State))
	if stato == "" {
		rispondiJSON(w, http.StatusBadRequest, Errore{`manca il campo "state"`})
		return
	}
	if !valido(stato) {
		rispondiJSON(w, http.StatusBadRequest,
			Errore{fmt.Sprintf("stato %q non esiste, usa OFF, PENDING o APPROVED", req.State)})
		return
	}

	// 3. cambio lo stato e rileggo quello vero
	nuovo, err := s.Imposta(stato)
	if err != nil {
		rispondiErrore(w, err)
		return
	}
	if nuovo != stato {
		rispondiJSON(w, http.StatusInternalServerError,
			Errore{fmt.Sprintf("ho chiesto %s ma la scheda è in %s", stato, nuovo)})
		return
	}
	log.Printf("PUT /led da %s -> %s", r.RemoteAddr, nuovo)
	rispondiJSON(w, http.StatusOK, RispostaLed{Led: nuovo})
}

// GET / -> la pagina con i tre pulsanti
func paginaHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write(pagina)
}

// /led con un metodo diverso da GET e PUT -> 405
func metodoNonAmmesso(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Allow", "GET, PUT")
	rispondiJSON(w, http.StatusMethodNotAllowed,
		Errore{"metodo " + r.Method + " non ammesso su /led, usa GET o PUT"})
}

// qualsiasi altro percorso -> 404
func nonTrovato(w http.ResponseWriter, r *http.Request) {
	rispondiJSON(w, http.StatusNotFound, Errore{"percorso " + r.URL.Path + " non trovato"})
}

// rispondiJSON: prima l'header, poi lo status, poi il corpo. L'ordine conta.
func rispondiJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(v)
}

// rispondiErrore sceglie lo status in base all'errore della seriale.
func rispondiErrore(w http.ResponseWriter, err error) {
	status := http.StatusInternalServerError // 500: errore nostro / della seriale
	if errors.Is(err, ErrTimeout) {
		status = http.StatusGatewayTimeout // 504: la scheda dietro di noi non risponde
	}
	log.Printf("errore: %v", err)
	rispondiJSON(w, status, Errore{err.Error()})
}

// indirizziLocali restituisce gli IPv4 del PC, per sapere quale dare al prof.
func indirizziLocali() []string {
	var ips []string
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return nil
	}
	for _, a := range addrs {
		ipnet, ok := a.(*net.IPNet)
		if !ok || ipnet.IP.To4() == nil || ipnet.IP.IsLoopback() || ipnet.IP.IsLinkLocalUnicast() {
			continue
		}
		ips = append(ips, ipnet.IP.String())
	}
	return ips
}
