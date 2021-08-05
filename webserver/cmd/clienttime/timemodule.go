package clienttime

import (
	"bytes"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

const (
	ltcExe  = "./../ltc/ltc"
	logsDir = "./../logs"
)

type ClientTimeEntry struct {
	TotalTime  uint64
	ClientName string
}

func fetchClientTime() string {
	var stdout bytes.Buffer

	cmd := exec.Command(ltcExe, "-h", "10", "-s", logsDir)
	cmd.Stdout = &stdout
	cmd.Stderr = os.Stderr
	cmd.Run()
	return string(stdout.Bytes())
}

func BuildClientTimes() []ClientTimeEntry {
	var entries []ClientTimeEntry = nil
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
		entries = append(entries, ClientTimeEntry{TotalTime: time, ClientName: name})
	}
	return entries
}
