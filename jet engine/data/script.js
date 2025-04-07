let socket;

function connectWebSocket() {
  socket = new WebSocket("ws://" + window.location.hostname + ":81");

  socket.onopen = () => {
    console.log("WebSocket connected.");
  };

  socket.onmessage = (event) => {
    const data = parseSensorData(event.data);

    setText("temperature", data.Temp);
    setText("humidity", data.Humidity);
    setText("aqi", data.AQI);
    setText("co2", data.CO2);
    setText("nh3", data.NH3);
    setText("nox", data.NOx);
    setText("alcohol", data.Alcohol);
    setText("benzene", data.Benzene);
    setText("smoke", data.Smoke);

    // Flame detection
    const flameElement = document.getElementById("flame");
    const flameWarning = document.getElementById("flame-warning");

    if (data.Flame === "1") {
      setText("flame", "Detected");
      flameElement.style.color = "red";
      flameWarning.style.display = "block";
    } else {
      setText("flame", "Not Detected");
      flameElement.style.color = "green";
      flameWarning.style.display = "none";
    }

    setText("last-update", new Date().toLocaleTimeString());
  };

  socket.onclose = () => {
    console.log("WebSocket disconnected. Retrying in 3s...");
    setTimeout(connectWebSocket, 3000);
  };

  socket.onerror = (err) => {
    console.error("WebSocket error:", err);
    socket.close();
  };
}

function setText(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = value;
}

function parseSensorData(data) {
  return data.split(",").reduce((obj, item) => {
    const [key, val] = item.split(":");
    if (key && val !== undefined) obj[key.trim()] = val.trim();
    return obj;
  }, {});
}

// Motor speed control
document.addEventListener("DOMContentLoaded", () => {
  connectWebSocket();

  const speedSlider = document.getElementById("motorSpeed");
  const speedValue = document.getElementById("motorSpeedValue");

  if (speedSlider && speedValue) {
    speedSlider.addEventListener("input", () => {
      const speed = speedSlider.value;
      speedValue.textContent = speed;
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send("MOTOR_SPEED:" + speed);
      }
    });
  }
});
