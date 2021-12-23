package tsc

import (
	"bufio"
	"log"
	"net"
	"os"
	"strings"
	"time"
)

var ServerMsgChan = make(chan string)

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
func ListenToServerMessages() {
	f, err := os.OpenFile("chathistory.txt", os.O_APPEND | os.O_CREATE | os.O_WRONLY, 0644)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()

	for {
		c := initServerConnection()
		if err := listenForMessages(c, f); err != nil {
			log.Println(err)
		}
		if err := c.conn.Close(); err != nil {
			log.Println(err)
		}
	}
}

func initServerConnection() *conn {
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
	return c
}

func listenForMessages(c *conn, f *os.File) error {
	logMsg := func(layout, msg string) {
		t := time.Now()
		line := t.Format(layout) + "\t" + msg + "\n"
		_, err := f.Write([]byte(line))
		if err != nil {
			log.Fatal(err)
		}
	}

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

			logMsg(time.UnixDate, serverMsg)
			ServerMsgChan <- serverMsg

		}
		time.Sleep(1 * time.Second)
		/* Simply send a new line to keep the connection alive */
		_, err := c.conn.Write([]byte("\n"))
		if err != nil {
			return err
		}
	}
	f.Sync()
	return nil
}
