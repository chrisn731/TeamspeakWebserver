package tsc

import (
	"time"
)

const (
	clientListSocketTimeout = 5 * time.Second
)

type ChannelClientPair struct {
	ChannelName string   `json:"ChannelName"`
	Clients     []string `json:"Clients"`
}

var ClientListChan = make(chan []ChannelClientPair)


func PushClientList(tsconn *TSConn) {
	for {
		var pairs []ChannelClientPair

		chanClientMap, err := tsconn.buildChannelClientMap()
		if err != nil {
			// TODO: Push an error to the client
			time.Sleep(clientListSocketTimeout)
			continue
		}

		for k, v := range chanClientMap {
			pair := ChannelClientPair{
				ChannelName: k,
				Clients:     v,
			}
			pairs = append(pairs, pair)
		}
		ClientListChan <- pairs
		time.Sleep(clientListSocketTimeout)
	}
}
