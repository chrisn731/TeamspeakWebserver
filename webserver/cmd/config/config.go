package config

import (
	"os"
	"encoding/json"
	"log"
)

const (
	configFile = "config.json"
)

type ServerConfig struct {
	ClientTimeConf struct {
		Enabled bool `json:"enabled,string"`
		Prog string `json:"prog"`
		Exe string `json:"exe"`
	} `json:"clienttime"`

	ClientListConf struct {
		Enabled bool `json:"enabled,string"`
	} `json:"clientlist"`

	ServerMessaging struct {
		Enabled bool `json:"enabled,string"`
	} `json:"servermessages"`

	Credentials struct {
		Username string `json:"username"`
		Password string `json:"password"`
		IP string	`json:"ip"`
		Port string	`json:port"`
	} `json:"credentials"`

	ServerConnection struct {
		Enabled bool `json:"enabled,string"`
	} `json:"serverconnection"`
}

var Config ServerConfig

func LoadConfiguration() {
	f, err := os.Open(configFile)
	log.Print("Loading config")
	defer f.Close()

	if err != nil {
		log.Fatal(err)
	}
	jsonParser := json.NewDecoder(f)
	err = jsonParser.Decode(&Config)
	if err != nil {
		log.Fatal(err)
	}
}
