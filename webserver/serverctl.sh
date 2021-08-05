#!/bin/bash

start_server() {
	test -e "./tswebserver" || go build
	nohup ./tswebserver &>/dev/null &
	disown
	return 0
}

stop_server() {
	SERVER_PID=$(ps aux | grep "tswebserver" | awk '{print $2;exit;}')
	if [ -z $SERVER_PID ]; then
		echo "Server not currently running"
		return 1
	fi
	kill -TERM $SERVER_PID || kill -KILL $SERVER_PID
	return $?
}

case "$1" in
start)
	start_server
	exit $?
	;;
stop)
	stop_server
	exit $?
	;;
*)
	echo "Usage ${0} [start | stop]"
	exit 1
	;;
esac
