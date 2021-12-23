package cmd

import (
	"errors"
	"io/ioutil"
	"log"
	"strconv"
	"strings"
)

const (
	serverFilesDirectory = "/home/chrisnap/Backups/.tsbak/files/virtualserver_1/"
)

type FileChannel struct {
	ChannelName     string `json:"channel_name"`
	ChannelFilePath string `json:"channel_file_path"`
	ChannelID       int64 `json:"channel_id"`
}

var ServerFiles = make(map[string]FileChannel)

func InitServerFileMap() error {
	channelDirs, err := ioutil.ReadDir(serverFilesDirectory)
	if err != nil {
		return err
	}

	for _, channelDir := range channelDirs {
		if !channelDir.IsDir() {
			continue
		}
		formPath := func(cats ...string) string {
			base := serverFilesDirectory
			for _, cat := range cats {
				if base[len(base) - 1] != '/' {
					base += "/"
				}
				base += cat
			}
			return base
		}
		// Each file in this directory should follow the
		// format: "channel_##" where ## is it's ID.
		nameSplit := strings.Split(channelDir.Name(), "_")
		if len(nameSplit) != 2 {
			// This is only really here to make sure we do
			// not touch the "internal" folder that resides
			// alongside the directories we truly want
			continue
		}

		channelFiles, err := ioutil.ReadDir(formPath(channelDir.Name()))
		if err != nil {
			return err
		}

		channelID, err := strconv.ParseInt(nameSplit[1], 10, 64)
		if err != nil {
			// This is fatal because if we made it here then
			// there is most likely something off with the
			// directory and should be fixed before proceeding
			log.Fatal(err)
		}

		// Begin adding files in this channel to the map
		// If the data structure of the map changes, this is
		// the ONLY part that would need to be updated.
		for _, file := range channelFiles {
			filePath := formPath(channelDir.Name(), file.Name())
			ServerFiles[filePath] = FileChannel{
				ChannelName: channelDir.Name(),
				ChannelFilePath: formPath(channelDir.Name()),
				ChannelID: channelID,
			}
		}
	}
	return nil
}

func LookupFile(fileName string) (string, error) {
	parentDir, ok := ServerFiles[fileName]
	if !ok {
		return "", errors.New("No file found")
	}

	return parentDir.ChannelFilePath + fileName, nil
}
