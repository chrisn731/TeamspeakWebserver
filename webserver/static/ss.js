const displayMediaOptions = {
	noConstraint: {
		video: true,
		audio: true,
	},
	v720p30: {
		video: {
			height: 720,
			frameRate: 30,
		},
		audio: true,
	},
	v480p60: {
		video: {
			height: 480,
			frameRate: 60,
		},
		audio: true,
	},
};

document.getElementById("btn").addEventListener("click", function(evt) {
	startCapture()
}, false);

const video = document.getElementById("video");
async function startCapture() {
	var mediaoption = {
		video: {
			cursor: "always"
		},
		audio: false
	}
	try {
		video.srcObject = await navigator.mediaDevices.getDisplayMedia(mediaoption)
	} catch (err) {
		console.log(err)
	}
}
