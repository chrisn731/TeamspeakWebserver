const ChannelList = document.getElementById("channels")

/* Use this to change the pulsing message (Motd.innerText = [message]) */
const Motd = document.getElementById("motd")

let socket = new WebSocket("ws://" + document.location.host + "/ws");
let debug = false

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
    updateTable(msg.data)
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

    for(let item of clientListData) {
        html += `<ul class="list-channels">${item.ChannelName}`;
        /* Start inserting clients */
        html += "<li><ul class='list-clients'>";
        for (let client of item.Clients) {
            html += `<li>${client}</li>`
        }
        html += '</ul></ul>'
    }

    ChannelList.innerHTML = html
}
