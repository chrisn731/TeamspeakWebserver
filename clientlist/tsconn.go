package main

import (
	"bufio"
	"github.com/multiplay/go-ts3"
	"log"
	"net"
	"strings"
	"time"
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

/*
 * listenToServerMessages - listen to server text events and pass them to
 *				relevant channel
 *
 * The reason this function does not include any of the handy ts3 package
 * functions because
 *	1) It does not include the "servernotifyregister" server function
 *	2) It provides no way to poll the connection for incoming messages
 * Therefore, it had to be done manually.
 */
func listenToServerMessages() {
	// This struct's purpose is just to keep some records on the connection
	type conn struct {
		conn    net.Conn
		scanner *bufio.Scanner
		buf     []byte
	}

	var err error
	c := &conn{
		buf: make([]byte, 4096),
	}

	if c.conn, err = net.Dial("tcp", ip+":"+port); err != nil {
		log.Fatal(err)
	}

	c.scanner = bufio.NewScanner(bufio.NewReader(c.conn))
	c.scanner.Buffer(c.buf, 10<<20)
	c.scanner.Split(ScanLines)
	if !c.scanner.Scan() {
		log.Fatal("Scan 1 failed")
	}

	if l := c.scanner.Text(); l != "TS3" {
		log.Fatal("Failed reading TS3 header")
	}

	if !c.scanner.Scan() {
		log.Fatal("Scan 2 failed")
	}

	_, err = c.conn.Write([]byte("login " + username + " " + password + "\n"))
	if err != nil {
		log.Fatal("Login write failed")
	}
	c.scanner.Scan() // read in err=0 msg=ok

	if _, err := c.conn.Write([]byte("use 1\n")); err != nil {
		log.Fatal(err)
	}
	c.scanner.Scan() // read in err=0 msg=ok

	if _, err := c.conn.Write([]byte("servernotifyregister event=textserver\n")); err != nil {
		log.Fatal("idk2")
	}
	c.scanner.Scan() // read in err=0 msg=ok

	for {
		for c.scanner.Scan() {
			serverMsg := ""
			l := c.scanner.Text()

			serverCheck := strings.Index(l, "invokerid=0")
			if serverCheck != -1 {
				continue
			}

			msgIDStart := strings.Index(l, "msg=")
			if msgIDStart == -1 {
				continue
			}

			msgEnd := strings.Index(l, "invokerid=")
			if msgEnd == -1 {
				continue
			}

			onlyMsg := l[msgIDStart+4 : msgEnd]
			for _, word := range strings.Split(onlyMsg, "\\s") {
				serverMsg += word + " "
			}
			serverMsgChan <- serverMsg

		}
		time.Sleep(1 * time.Second)
		/* Simply send a new line to keep the connection alive */
		c.conn.Write([]byte("\n"))
	}
}
