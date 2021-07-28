package main

import (
	"github.com/multiplay/go-ts3"
	"strings"
)

const (
	username = ""
	password = ""
)

type TSConn struct {
	Conn *ts3.Client
}

func connectToServer() (*TSConn, error) {
	c, err := ts3.NewClient("192.168.1.34:10011")
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

func (c *TSConn) sendGlobalMsg(msg string) error {
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

func (c *TSConn) closeConn() {
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
