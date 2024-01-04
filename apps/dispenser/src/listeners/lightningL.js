const WebSocket = require('ws');
const { dispenseFromPayments } = require('../machine');

// WebSocket URL
const wsUrl = 'wss://send.laisee.org/api/v1/ws/V5UfKc845cdrKrsmMijExW';

// Create a new WebSocket connection
const ws = new WebSocket(wsUrl);


// Event listener for when the connection is open
ws.on('open', function open() {
  console.log('Connected to the server.');
});

// Event listener for when a message is received from the server
ws.on('message', function message(data) {
  //console.log('Received message from server:', data);
  const messageStr = data.toString('utf-8'); // Convert buffer to string
  console.log('Received message from server:', messageStr);
  dispenseFromPayments()
});

// Event listener for handling errors
ws.on('error', function error(err) {
  console.error('WebSocket error:', err);
});
