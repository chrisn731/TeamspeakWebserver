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
)


var validpath = regexp.MustCompile("^/$")

type clientTimeEntry struct {
	TotalTime uint64
	ClientName string
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
	p := clientListPage{ClientList: buildChannelClientMap(), ClientTimeEntries: buildClientTime()}
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
