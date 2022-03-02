Hello there,

How to use
==========

Bot:
	Enter your teamspeak3 server query credentials for the bot to connect
		through. Use: `python3 bot.py` to ensure everything is in working
		order. Then use something like: `python3 bot.py &; disown;` to
		have it detach so it does not shutdown upon terminal exit.

Webserver:
	1. Go into the webserver directory.
	2. Change the config (webserver/config.json) file to match YOUR paramters.
	3. "./serverctl.sh start" to start the server.
		+ Use "./serverctl.sh stop" to stop the server
		* NOTE: These commands only work on *nix! If youre running this
			webserver on windows you will have to manually start and stop
			the server! If you are on windows, use ./tswebserver insead

Manager:
	1. Run `make manager`
	2. Use:
		$ ./manager -w
			* Starts up the webserver
		$ ./manager -b
			* Starts up the bot
		$ ./manager -a
			* Starts up both the webserver and the bot.

	I tried to make adding more modules as clean as possible.
	To add your own module, simply use the DEFINE_MODULE() macro and define
	its init() function. After which, you can simply call it from the main
	method!

	To shut it down, use: $ ./manager -s stop

	Currently in the works: Splitting up the code. Currently working on
		having the manager be much more interactive. Such logging into
		a shell like interface in which you can send commands to it.
