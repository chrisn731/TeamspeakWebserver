const ChatLog = document.getElementById("chat-log")
const ChatInput = document.getElementById("chat-input")
const ChannelList = document.getElementById("channels")
const ChatSend = document.getElementById("chat-send")

/* Use this to change the pulsing message (Motd.innerText = [message]) */
const Motd = document.getElementById("motd")

let socket = new WebSocket("ws://" + document.location.host + "/ws");
let debug = false

ChatInput.addEventListener("keypress", event => {
	if (event.key == "Enter") {
		sendChatMessage()
	}
})

ChatSend.addEventListener("click", sendChatMessage)

function sendChatMessage() {
	let text = ChatInput.value.trim()
	if (text <= 0) {
		ChatInput.value = ""
		return
	}

	let message = {
		ip: "blah.blah.blah", // Todo: get IP
		message: text,
		time: "69:69 PM" // Todo: get time - I'm lazy af
	}

	let json = JSON.stringify(message)

	let data = {
		header: "chatmessage",
		payload: json
	}

	if (debug) {
		console.log(json)
		console.log(data)
	}

	socket.send(JSON.stringify(data))
	ChatLog.append(`${text}\n`) // probably comment this out once server communication is set up
	ChatLog.scrollTop = ChatLog.scrollHeight
	ChatInput.value = ""
}

socket.onmessage = event => {
	let msg = JSON.parse(event.data)
	if (debug) {
		for (var chans = 0; chans < msg.data.length; chans++) {
			var currChan = msg.data[chans].Clients
			console.log(msg.data[chans].ChannelName)
			for (var clients = 0; clients < currChan.length; clients++) {
				console.log(currChan[clients])
			}
		}
	}
	if (msg.header === "clientlist") {
		updateTable(msg.payload)
	} else if (msg.header === "servermsg") {
		ChatLog.append(`${msg.payload}\n`)
	}
}

socket.onopen = () => {
	console.log("Successfully Connected");
};

socket.onclose = event => {
	console.log("Socket Closed Connection: ", event);
};

socket.onerror = error => { };

function updateTable(clientListData) {
	let html = "";

	if (clientListData === undefined) {
		ChannelList.innerHTML = "<p>Error while loading clientlist</p>"
		return
	}

	if (clientListData === null) {
		ChannelList.innerHTML = "<p>No one is currently online :(</p>"
		return
	}

	// First sort by channel name...
	clientListData.sort((a, b) => a.ChannelName > b.ChannelName ? -1 : 1)
	// ... then sort by how many clients are in each channel
	clientListData.sort((a, b) => a.Clients.length > b.Clients.length ? -1 : 1)
	for(let item of clientListData) {
		html += `<ul class="list-channels">${item.ChannelName}`;
		/* Start inserting clients */
		html += "<li><ul class='list-clients'>";
		item.Clients.sort()
		for (let client of item.Clients) {
			html += `<li>${client}</li>`
		}
		html += '</ul></ul>'
	}
	ChannelList.innerHTML = html
}
