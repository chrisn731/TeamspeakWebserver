package cmd

import (
	"io/ioutil"
	"log"
	"math/rand"
	"strings"
	"time"
)

const (
	motdPath = "./static/teamspeaksayings.txt"
)

var motds []string

func getMotd() string {
	if motds == nil {
		rand.Seed(time.Now().UnixNano())

		data, err := ioutil.ReadFile(motdPath)
		if err != nil {
			log.Fatal(err)
		}

		for _, line := range strings.Split(string(data), "\n") {
			if len(line) > 0 {
				motds = append(motds, line)
			}
		}
	}
	return motds[rand.Intn(len(motds))]
}
