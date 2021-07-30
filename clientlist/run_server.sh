#!/bin/bash

test -e "./tswebserver" || go build
nohup ./tswebserver &>/dev/null &
disown
