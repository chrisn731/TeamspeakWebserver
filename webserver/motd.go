package main

import (
	"bufio"
	"log"
	"math/rand"
	"os"
	"time"
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
	defer file.Close()
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
