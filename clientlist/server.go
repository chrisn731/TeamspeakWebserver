package main

import (
	"bytes"
	"fmt"
	"html/template"
	"log"
	"net/http"
	"os"
	"os/exec"
	"regexp"
	"strings"
	"strconv"
	"time"
	"github.com/gorilla/websocket"
)


var validpath = regexp.MustCompile("^/$")

var clients = make(map[*websocket.Conn]bool)
var broadcast = make(chan Message)
var mockData = make(chan Message)

const (
	socketTimeoutSeconds = 5
	socketTimeout = socketTimeoutSeconds * 1000
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

type clientTimeEntry struct {
	TotalTime uint64
	ClientName string
}

type Message struct {
	Data []ChannelClientPair `json:"data"`
}

type ChannelClientPair struct {
	ChannelName string
	Clients []string
}

/*
 * For now, this is a  wrapper around the ClientList for when we use templates.
 * Eventually more things will be added, I think.
 */
type clientListPage struct {
	ClientList map[string][]string
	ClientTimeEntries []clientTimeEntry
}

func fetchClientTime() string {
	var stdout bytes.Buffer

	cmd := exec.Command("../ltc/ltc", "-h", "10", "-s", "../logs")
	cmd.Stdout = &stdout
	cmd.Stderr = os.Stderr
	cmd.Run()
	return string(stdout.Bytes())
}

func fetchClientList() string {
	var stdout bytes.Buffer

	cmd := exec.Command("python", "clientlist.py", "-g")
	cmd.Stdout = &stdout
	cmd.Stderr = os.Stderr
	cmd.Run()
	return string(stdout.Bytes())
}

func buildChannelClientMap() map[string][]string {
	channelClientMap := make(map[string][]string)
	clientList := fetchClientList()

	channelClientLines := strings.Split(clientList, "\n")
	for _, line := range channelClientLines {
		if len(line) <= 0 {
			continue
		}

		channelClientSplit := strings.Split(line, ":")
		if len(channelClientSplit) != 2 && len(channelClientSplit) != 0 {
			fmt.Println("ayo? ", channelClientSplit)
			os.Exit(1)
		}

		channelName := channelClientSplit[0]
		clientSplit := strings.Split(channelClientSplit[1], "|")

		var clients []string = nil
		for _, clientName := range clientSplit {
			/*
			 * When we are splitting strings, make sure we
			 * are not getting a blank string
			 */
			if len(clientName) > 0 {
				clients = append(clients, strings.TrimSpace(clientName))
			}
		}
		channelClientMap[channelName] = clients
	}
	return channelClientMap
}

func buildClientTime() []clientTimeEntry {
	var entries []clientTimeEntry = nil
	times := strings.Split(fetchClientTime(), "\n")

	for _, line := range times {
		timeNameSplit := strings.Split(line, " ")
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
	if m := validpath.FindStringSubmatch(r.URL.Path); m == nil {
		http.ServeFile(w, r, "./404.html")
		return
	}
	p := clientListPage{
		ClientList: buildChannelClientMap(),
		ClientTimeEntries: buildClientTime(),
	}
	t := template.Must(template.ParseFiles("clientlist.html"))
	if err := t.Execute(w, p); err != nil {
		panic(err)
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
		var msg Message

		err := conn.ReadJSON(&msg)
		if err != nil {
			log.Printf("error: %v", err)
			delete(clients, conn)
			break
		}
		broadcast <-msg
	}
}


func fillData() {
	for {
		var shit Message
		for k, v := range buildChannelClientMap() {
			pair := ChannelClientPair{
					ChannelName: k,
					Clients: v,
				}
			shit.Data = append(shit.Data, pair)
		}
		mockData <- shit
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

		case msg := <-mockData:
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

func main() {
	http.HandleFunc("/", indexHandler)
	http.HandleFunc("/ws", handleConnections)

	go handleMessages()
	go fillData()
	if err := http.ListenAndServe(":8081", nil); err != nil {
		log.Fatal(err)
	}
}
