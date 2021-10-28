package clienttime

import (
	"bytes"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

const (
	rustLtcExe  = "./../ltc/target/release/ltc"
	cLtcExe = "./../ltc/old_ltc/ltc"
	logsDir = "./../logs"
	numClients = 13
	prog = "C"
)

type ClientTimeEntry struct {
	TotalTime  uint64
	ClientName string
}

func fetchClientTime() string {
	var stdout bytes.Buffer
	var cmd *exec.Cmd

	if prog == "C" {
		clientsToPrint := strconv.Itoa(numClients)
		cmd = exec.Command(cLtcExe, "-h", clientsToPrint, "-s", logsDir)
	} else {
		cmd = exec.Command(rustLtcExe, logsDir)
	}
	cmd.Stdout = &stdout
	cmd.Stderr = os.Stderr
	cmd.Run()
	return string(stdout.Bytes())
}

func BuildClientTimes() []ClientTimeEntry {
	var entries []ClientTimeEntry = nil
	times := strings.Split(fetchClientTime(), "\n")

	for i, line := range times {
		if i >= 13 {
			break
		}

		timeNameSplit := strings.Split(line, "\t")
		if len(line) <= 0 {
			continue
		}
		time, err := strconv.ParseUint(timeNameSplit[0], 10, 64)
		if err != nil {
			panic(err)
		}
		name := timeNameSplit[1]
		entries = append(entries, ClientTimeEntry{TotalTime: time, ClientName: name})
	}
	return entries
}
