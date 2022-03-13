const ToggleButton = document.getElementById("lightbtn")

let socket = new WebSocket("wss://" + document.location.host + "/lights/ws")

/*
 * Opening setup. Get the current light status
 */
socket.onopen = () => {
	console.log("Successfully Connected");
	console.log("Requesting current light information...")
	socket.send(
		JSON.stringify({
			header: "getCurrentToggle",
			payload: "---"
		})
	)
	updateLightColor()
};

socket.onclose = event => {
	console.log("Socket Closed Connection: ", event);
	alert("Connection to the server has closed. Please referesh.")
	ToggleButton.disabled = true
};

function updateToggle(toggleVal) {
	if (toggleVal === undefined) {
		console.log("Socket sent bad data!!")
		return
	}
	ToggleButton.innerText = toggleVal
	updateLightColor()
}

socket.onmessage = event => {
	let msg = JSON.parse(event.data)
	if (msg.header === "toggle") {
		updateToggle(msg.payload)
	}
}

ToggleButton.addEventListener("click", toggleLight)
function toggleLight() {
	let currToggle = ToggleButton.innerText
	let nextToggle = "??"

	if (currToggle === "On") {
		nextToggle = "Off"
	} else {
		nextToggle = "On"
	}
	ToggleButton.innerText = nextToggle
	updateLightColor()

	let data = {
		header: "toggleLight",
		payload: nextToggle
	}
	socket.send(JSON.stringify(data))
}

function updateLightColor() {
	let toggleStatus = ToggleButton.innerText
	let color = "black"

	if (toggleStatus === "On") {
		color = "green"
	} else {
		color = "black"
	}
	ToggleButton.style.backgroundColor = color
	ToggleButton.style.color = "white"
}
