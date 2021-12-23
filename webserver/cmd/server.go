package cmd

import (
	"encoding/json"
	"github.com/gorilla/websocket"
	"html/template"
	"log"
	"net/http"
	"os"
	"strconv"
	"time"
	"tswebserver/cmd/clienttime"
	"tswebserver/cmd/tsc"
	"tswebserver/cmd/config"
)

const (
	socketTimeoutSeconds = 5
	socketTimeout        = socketTimeoutSeconds * time.Second
)

/* The general method for sending data over the Websocket */
type SocketMessage struct {
	Header  string      `json:"header"`
	Payload interface{} `json:"payload"`
}

/* struct for receiving data */
type ClientPackage struct {
	Header  string `json:"header"`
	Payload string `json:"payload"`
}

type ClientChatMessage struct {
	IP      string `json:"ip"`
	Message string `json:"message"`
	Time    string `json:"time"`
}

/*
 * For now, this is a wrapper around the ClientList for when we use templates.
 * Eventually more things will be added, I think.
 */
type indexPage struct {
	ClientTimeEntries []clienttime.ClientTimeEntry
	Motd              string
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

var clients = make(map[*websocket.Conn]bool)
var broadcast = make(chan ClientChatMessage)
var tsconn *tsc.TSConn = nil
var connLogFile *os.File = nil


// 8/10/2021
// Dear Jason,
// Not really sure what you want here yet but I hope this is a start
// takes the requested file from the path and then looks up the filename.
// What is returned is simply a path to the file. http.ServeFile() takes care
// of the rest.
//
// Love,
// 	C N <3
func fileDownloadHandler(w http.ResponseWriter, r *http.Request) {
	reqFile := r.URL.Path

	filePath, err := LookupFile(reqFile)
	if err != nil {
		return
	}

	w.Header().Set("Content-Disposition", "attachment; filename="+strconv.Quote(reqFile))
	w.Header().Set("Content-Type", r.Header.Get("Content-Type"))
	http.ServeFile(w, r, filePath)
}

func indexHandler(w http.ResponseWriter, r *http.Request) {
	if connLogFile == nil {
		var err error
		connLogFile, err = os.OpenFile("connections.txt", os.O_CREATE | os.O_WRONLY, 0644)
		if err != nil {
			log.Fatal(err)
		}
	}
	req := "." + r.URL.Path
	_, err := connLogFile.Write([]byte("Request: " + r.RemoteAddr + "\n"))
	if err != nil {
		log.Printf("Failed to write to file for req: %s", r.RemoteAddr)
	}
	if req == "./" {
		p := indexPage{
			ClientTimeEntries: clienttime.BuildClientTimes(),
			Motd:              getMotd(),
		}
		t := template.Must(template.ParseFiles("./static/index.html"))
		if err := t.Execute(w, p); err != nil {
			panic(err)
		}
	} else {
		req = "./static/" + r.URL.Path
		info, err := os.Stat(req)
		if err != nil && os.IsNotExist(err) || info.IsDir() {
			req = "./static/404.html"
		}
		http.ServeFile(w, r, req)
	}
}

func handleConnections(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Fatal(err)
	}
	defer conn.Close()

	clients[conn] = true

	for {
		var p ClientPackage

		err := conn.ReadJSON(&p)
		if err != nil {
			log.Printf("Error reading ClientPackage %v", err)
			if e, ok := err.(*json.SyntaxError); ok {
				log.Printf("Syntax error at byte offset %d", e.Offset)
			}
			return
		}

		switch p.Header {
		case "chatmessage":
			var message ClientChatMessage

			err := json.Unmarshal([]byte(p.Payload), &message)
			if err != nil {
				log.Printf("Error unmarshaling payload: %v", err)
				continue
			}
			log.Printf(message.Message)
			if err := tsconn.SendGlobalMsg(message.Message); err != nil {
				log.Println("Send Msg error: ", err)
			}
			/* Do things with the message here */
			break
		default:
			continue
		}

		if err != nil {
			log.Printf("error: %v", err)
			delete(clients, conn)
			break
		}

	}
}

func handleMessages() {
	for {
		select {
		// Grab the next message from the broadcast channel
		case msg := <-broadcast:
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
		case msg := <-tsc.ClientListChan:
			for client := range clients {
				sockMsg := SocketMessage{
					Header:  "clientlist",
					Payload: msg,
				}
				err := client.WriteJSON(sockMsg)
				if err != nil {
					log.Printf("error: %v", err)
					client.Close()
					delete(clients, client)
				}
			}
		case msg := <-tsc.ServerMsgChan:
			for client := range clients {
				sockMsg := SocketMessage{
					Header:  "servermsg",
					Payload: msg,
				}
				client.WriteJSON(sockMsg)
			}
		}
	}
}

func StartServer() {
	var err error

	config.LoadConfiguration()
	tsconn, err = tsc.ConnectToServer()
	if err != nil {
		log.Fatal(err)
	}
	defer tsconn.CloseConn()

	http.HandleFunc("/", indexHandler)
	http.HandleFunc("/ws", handleConnections)

	go handleMessages()
	if config.Config.ClientListConf.Enabled {
		go tsc.PushClientList(tsconn)
	}
	if config.Config.ServerMessaging.Enabled {
		go tsc.ListenToServerMessages()
	}
	if err := http.ListenAndServeTLS(":8081", "server.crt", "server.key", nil); err != nil {
		log.Fatal(err)
	}
}
