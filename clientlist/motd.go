package main

import (
	"bufio"
	"os"
	"math/rand"
	"time"
	"log"
)

const (
	motdPath = "./static/teamspeaksayings.txt"
)

var motds []string = nil

func populateMotds() {
	file, err := os.Open(motdPath)
	if err != nil {
		log.Fatal(err)
	}
	scanner := bufio.NewScanner(file)

	for scanner.Scan() {
		l := scanner.Text()
		if len(l) > 0 {
			motds = append(motds, scanner.Text())
		}
	}

	if err := scanner.Err(); err != nil {
		log.Fatal(err)
	}
}
func getMotd() string {
	if motds == nil {
		rand.Seed(time.Now().UnixNano())
		populateMotds()
	}
	return motds[rand.Intn(len(motds))]
}
