package main

import (
	"bytes"
	_ "fmt"
	"html/template"
	"log"
	"net/http"
	"os"
	"os/exec"
	"strings"
	"strconv"
	"time"
	"encoding/json"
	"github.com/gorilla/websocket"
)

var clients = make(map[*websocket.Conn]bool)
var broadcast = make(chan ClientChatMessage)
var clientListChan = make(chan ChannelClientPairs)
var serverMsgChan = make(chan string)
var tsc *TSConn = &TSConn{}

const (
	socketTimeoutSeconds = 5
	socketTimeout = socketTimeoutSeconds * time.Second
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

type ChannelClientPair struct {
	ChannelName string `json:"ChannelName"`
	Clients []string `json:"Clients"`
}

type ChannelClientPairs struct {
	Header string `json:"header"`
	Data []ChannelClientPair `json:"data"`
}

/* struct for receiving data */
type ClientPackage struct {
	Header string `json:"header"`
	Payload string `json:"payload"`
}

type ClientChatMessage struct {
	IP string `json:"ip"`
	Message string `json:"message"`
	Time string `json:"time"`
}

type clientTimeEntry struct {
	TotalTime uint64
	ClientName string
}


/*
 * For now, this is a  wrapper around the ClientList for when we use templates.
 * Eventually more things will be added, I think.
 */
type clientListPage struct {
	ClientTimeEntries []clientTimeEntry
	Motd string
}

func fetchClientTime() string {
	var stdout bytes.Buffer

	cmd := exec.Command("../ltc/ltc", "-h", "10", "-s", "../logs")
	cmd.Stdout = &stdout
	cmd.Stderr = os.Stderr
	cmd.Run()
	return string(stdout.Bytes())
}

func buildClientTime() []clientTimeEntry {
	var entries []clientTimeEntry = nil
	times := strings.Split(fetchClientTime(), "\n")

	for _, line := range times {
		timeNameSplit := strings.Split(line, "\t")
		if len(line) <= 0 {
			continue
		}
		time, err := strconv.ParseUint(timeNameSplit[0], 10, 64)
		if err != nil {
			panic(err)
		}
		name := timeNameSplit[1]
		entries = append(entries, clientTimeEntry{TotalTime: time, ClientName: name})
	}
	return entries;
}

func indexHandler(w http.ResponseWriter, r *http.Request) {
	req := "." + r.URL.Path
	if req == "./" {
		p := clientListPage{
			ClientTimeEntries: buildClientTime(),
			Motd: getMotd(),
		}
		t := template.Must(template.ParseFiles("./static/index.html"))
		if err := t.Execute(w, p); err != nil {
			panic(err)
		}
	} else  {
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
			if err := tsc.sendGlobalMsg(message.Message); err != nil {
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


func pushClientList() {
	for {
		var pairs ChannelClientPairs
		chanClientMap, err := tsc.buildChannelClientMap()
		if err == nil {
			for k, v := range chanClientMap {
				pair := ChannelClientPair{
						ChannelName: k,
						Clients: v,
				}
				pairs.Data = append(pairs.Data, pair)
			}
			pairs.Header = "clientlist"
			clientListChan <- pairs
		}
		time.Sleep(socketTimeout)
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
		case msg := <-clientListChan:
			for client := range clients {
				err := client.WriteJSON(msg)
				if err != nil {
					log.Printf("error: %v", err)
					client.Close()
					delete(clients, client)
				}
			}
		case msg := <-serverMsgChan:
			for client := range clients {
				type servermsg struct {
					Header string `json:"header"`
					Msg string `json:"msg"`
				}
				smsg := servermsg{
					Header: "servermsg",
					Msg: msg,
				}
				client.WriteJSON(smsg)
			}
		}
	}
}

func main() {
	var err error

	tsc, err = connectToServer()
	if err != nil {
		log.Fatal(err)
	}
	defer tsc.closeConn()

	http.HandleFunc("/", indexHandler)
	http.HandleFunc("/ws", handleConnections)

	go handleMessages()
	go pushClientList()
	go listenToServerMessages()
	if err := http.ListenAndServe(":8081", nil); err != nil {
		log.Fatal(err)
	}
}
