package main

import (
	"log"
	"net/http"
	"html/template"
	"time"
	"github.com/gorilla/websocket"
)

var clients = make(map[*websocket.Conn]bool)
var broadcast = make(chan Message)
var mockData = make(chan Message)

// Configure the upgrader
var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

// Define our message object
type Message struct {
	Username string `json:"username"`
	Message  string `json:"message"`
}

func main() {
	// Create a simple file server
	http.HandleFunc("/", handleRoot)

	// Configure websocket route
	http.HandleFunc("/ws", handleConnections)

	// Start listening for incoming chat messages
	// go is a keyword for starting a goroutine, which
	// is are do be concurrent operations ("ligh weighted threads")
	go handleMessages()
	go fillData()

	log.Println("http server started on :8081")
	err := http.ListenAndServe(":8081", nil)
	if err != nil {
		log.Fatal("ListenAndServe: ", err)
	}
}

// function to constantly populate data
func fillData() {
	for {
		mockData <- Message{
			Username: "Chris",
			Message: "Touches grass",
		}
		time.Sleep(100 * time.Millisecond)
	}
}

// root handler - replace with home page or sum ting
func handleRoot(res http.ResponseWriter, req *http.Request) {
	t, _ := template.ParseFiles("index.html")
	t.Execute(res, nil)
}

func handleConnections(w http.ResponseWriter, r *http.Request) {
	// Upgrade initial GET request to a websocket
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Fatal(err)
	}
	// Make sure we close the connection when the function returns
	defer conn.Close()

	// Register our new client
	clients[conn] = true

	for {
		var msg Message
		// Read in a new message as JSON and map it to a Message object
		err := conn.ReadJSON(&msg)
		if err != nil {
			log.Printf("error: %v", err)
			delete(clients, conn)
			break
		}
		// Send the newly received message to the broadcast channel
		broadcast <- msg
	}
}

func handleMessages() {
	for {
		select {

		// Grab the next message from the broadcast channel
		case msg := <- broadcast:
			// Send it out to every client that is currently connected
			for client := range clients {
				err := client.WriteJSON(msg)
				// if writing the messaged gave an error, close the connection i guess pog
				if err != nil {
					log.Printf("error: %v", err)
					client.Close()
					delete(clients, client)
				}
			}

		case msg := <- mockData:
			for client := range clients {
				err := client.WriteJSON(msg)
				if err != nil {
					log.Printf("error: %v", err)
					client.Close()
					delete(clients, client)
				}
			}

		}
	}
}