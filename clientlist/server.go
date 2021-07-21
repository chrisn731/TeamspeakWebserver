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
)


var validpath = regexp.MustCompile("^/$")

/*
 * For now, this is a  wrapper around the ClientList for when we use templates.
 * Eventually more things will be added, I think.
 */
type clientListPage struct {
	ClientList map[string][]string
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
		clients := make([]string, 0)

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

func indexHandler(w http.ResponseWriter, r *http.Request) {
	if m := validpath.FindStringSubmatch(r.URL.Path); m == nil {
		http.ServeFile(w, r, "./404.html")
		return
	}
	p := clientListPage{ClientList: buildChannelClientMap()}
	t := template.Must(template.ParseFiles("clientlist.html"))
	if err := t.Execute(w, p); err != nil {
		panic(err)
	}
}

func main() {
	http.HandleFunc("/", indexHandler)
	if err := http.ListenAndServe(":8081", nil); err != nil {
		log.Fatal(err)
	}
}
