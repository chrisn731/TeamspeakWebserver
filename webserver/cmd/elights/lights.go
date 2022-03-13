package elights

import (
	"net/http"
	"log"
	"github.com/gorilla/websocket"
	"strings"
	"encoding/json"
	"html/template"
)

/* The general method for sending data over the Websocket */
type ToggleMessage struct {
	Header  string      `json:"header"`
	Payload interface{} `json:"payload"`
}

/* struct for receiving data */
type ClientLightsPackage struct {
	Header  string `json:"header"`
	Payload string `json:"payload"`
}

type LightPageTemplate struct {
	CurrToggle string
}

var (
	upgrader = websocket.Upgrader{
		CheckOrigin: func(r *http.Request) bool {
			return true
		},
	}
	clients = make(map[*websocket.Conn]bool)
	toggle = make(chan string)
	currentToggle = "On"

)

func lightsConnHandler(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Fatal(err)
	}
	defer conn.Close()
	clients[conn] = true
	for {
		var p ClientLightsPackage
		err := conn.ReadJSON(&p)
		if err != nil {
			log.Printf("Error reading ClientPackage %v", err)
			if e, ok := err.(*json.SyntaxError); ok {
				log.Printf("Syntax error at byte offset %d", e.Offset)
			}
			return
		}

		switch p.Header {
		case "toggleLight":
			toToggleTo := p.Payload
			if strings.ToLower(toToggleTo) != "on" && strings.ToLower(toToggleTo) != "off" {
				log.Println("Lights: bad command!")
				continue
			}
			toggle<- toToggleTo
			break
		case "getCurrentToggle":
			toggle<- currentToggle
		}
	}
}

func handleLightToggles() {
	for {
		select {
		case newToggle := <-toggle:
			for client := range clients {
				sockMsg := ToggleMessage{
					Header:  "toggle",
					Payload: newToggle,
				}
				err := client.WriteJSON(sockMsg)
				if err != nil {
					log.Printf("error: %v", err)
					client.Close()
					delete(clients, client)
				}
			}
			currentToggle = newToggle
		}
	}

}
func lightPageHandler(w http.ResponseWriter, r *http.Request) {
	p := LightPageTemplate{
		CurrToggle: currentToggle,
	}
	t := template.Must(template.ParseFiles("./static/light.html"))
	if err := t.Execute(w, p); err != nil {
		panic(err)
	}
}

func SetupEvanLights() {
	http.HandleFunc("/lights/ws", lightsConnHandler)
	http.HandleFunc("/light.html", lightPageHandler)
	go handleLightToggles()
}

