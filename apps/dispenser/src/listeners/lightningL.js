const WebSocket = require("ws");
const { dispenseFromPayments, logPayment } = require("../machine");
const { notifyPaymentSuccess } = require("../payment-events");
const { LIGHTNING_LNBIT_URL } = require("../env");

// WebSocket URL
const wsUrl = LIGHTNING_LNBIT_URL;

const startLightningListener = async () => {
  // Create a new WebSocket connection
  const ws = new WebSocket(wsUrl);

  // Event listener for when the connection is open
  await ws.on("open", function open() {
    console.log("Connected to LNbits " + wsUrl);
  });

  // Event listener for when a message is received from the server
  await ws.on("message", function message(data) {
    //console.log('Received message from server:', data);
    const messageStr = data.toString("utf-8"); // Convert buffer to string
    console.log("Received message from LNbits server:", messageStr);
    // example: 0-1000 or 516-1000
    const parts = messageStr.split("-");
    const pinNo = parts[0];
    const amount = parts[1] ? parseInt(parts[1]) : null;

    logPayment(pinNo, "sats", amount, "lightning");
    dispenseFromPayments(pinNo, "sats");

    // Notify frontend via SSE
    console.log("[Lightning] Notifying SSE - payment received");
    notifyPaymentSuccess(pinNo, "lightning", null, "sats", amount);
  });
  
 ws.onclose = (event) => {
    const reconnectInterval = 60000; // 60000 milliseconds = 1 minute
    console.log("Connection cannot be established. Reconnecting in 1 minute");
    setTimeout(startLightningListener, reconnectInterval);
};

  // Error handling
  ws.onerror = (error) => {
  console.error("LNbits WebSocket error:", error.message);
  };
};

module.exports = {
  startLightningListener,
};
