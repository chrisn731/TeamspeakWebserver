package tsc

import (
	"bufio"
	"github.com/multiplay/go-ts3"
	"net"
	"strings"
)

const (
	username = ""
	password = ""
	ip       = "192.168.1.34"
	port     = "10011"
)

type TSConn struct {
	Conn *ts3.Client
}

// This struct's purpose is just to keep some records on the connection
type conn struct {
	conn    net.Conn
	scanner *bufio.Scanner
	buf     []byte
}

func ScanLines(data []byte, atEOF bool) (advance int, token []byte, err error) {
	if atEOF && len(data) == 0 {
		return 0, nil, nil
	}

	if i := strings.Index(string(data), "\n\r"); i >= 0 {
		return i + 2, data[0:i], nil
	}

	if atEOF {
		return len(data), data, nil
	}
	return 0, nil, nil
}

func ConnectToServer() (*TSConn, error) {
	c, err := ts3.NewClient(ip + ":" + port)
	if err != nil {
		return nil, err
	}

	err = c.Login(username, password)
	if err != nil {
		c.Close()
		return nil, err
	}
	c.Use(1)
	return &TSConn{Conn: c}, nil
}

func (c *TSConn) SendGlobalMsg(msg string) error {
	var err error = nil
	s := "gm msg="

	words := strings.Split(msg, " ")
	for _, word := range words {
		if len(word) > 0 {
			s += word + "\\s"
		}
	}
	if len(s) > 0 {
		_, err = c.Conn.Exec(s)
	}
	return err
}

func (c *TSConn) CloseConn() {
	c.Conn.Close()
}

func (c *TSConn) buildChannelClientMap() (map[string][]string, error) {
	methods := ts3.ServerMethods{c.Conn}

	clientList, err := methods.ClientList()
	if err != nil {
		return nil, err
	}

	channelList, err := methods.ChannelList()
	if err != nil {
		return nil, err
	}

	channelClientMap := make(map[string][]string)

	// TODO: Optimize this: k = num of channels => O(32k)
	for _, client := range clientList {
		for _, channel := range channelList {
			if client.ID == channel.ID && client.Type != 1 {
				cName := channel.ChannelName
				channelClientMap[cName] = append(channelClientMap[cName], client.Nickname)
			}
		}
	}
	return channelClientMap, nil
}
