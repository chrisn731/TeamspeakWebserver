package main

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"net/http"
	"log"
	"regexp"
)

var validpath = regexp.MustCompile("^/$")

func fetch_client_list() (string) {
	var stdout bytes.Buffer

	cmd := exec.Command("python", "clientlist.py")
	cmd.Stdout = &stdout
	cmd.Stderr = os.Stderr
	cmd.Run()
	outstr := string(stdout.Bytes())
	return outstr
}

func handler(w http.ResponseWriter, r *http.Request) {
	fmt.Printf("Remote Address connection: %q\n", r.RemoteAddr)
	m := validpath.FindStringSubmatch(r.URL.Path)
	if m != nil {
		fmt.Fprintf(w, fetch_client_list())
	} else {
		http.NotFound(w, r)
	}
}

func main() {
	http.HandleFunc("/", handler)
	if err := http.ListenAndServe(":8081", nil); err != nil {
		log.Fatal(err)
	}
}
