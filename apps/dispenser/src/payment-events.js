/**
 * Server-Sent Events (SSE) for Payment Notifications
 *
 * This module provides a real-time payment notification system using SSE.
 * SSE allows the server to push updates to connected web clients over HTTP.
 *
 * Unlike WebSockets, SSE is:
 * - Unidirectional (server → client only)
 * - Built on standard HTTP
 * - Automatically reconnects on disconnect
 * - Simple to implement on both server and client
 *
 * Use Case:
 * Display real-time payment notifications on a web dashboard or display screen.
 *
 * Usage:
 * 1. Connect to http://localhost:3500/payment-events from your frontend
 * 2. Listen for 'payment_received' events
 *
 * Example (JavaScript):
 *   const eventSource = new EventSource('http://localhost:3500/payment-events');
 *   eventSource.onmessage = (event) => {
 *     const data = JSON.parse(event.data);
 *     if (data.event === 'payment_received') {
 *       console.log(`Payment: ${data.amount} ${data.currency} for ${data.drink}`);
 *     }
 *   };
 *
 * Events:
 * - 'connected': Sent immediately upon connection
 * - 'payment_received': Sent when any payment method receives payment
 */
const clients = new Set();

// SSE endpoint for payment events
const setupPaymentEvents = (app) => {
  app.get('/payment-events', (req, res) => {
    // Set SSE headers
    res.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache',
      'Connection': 'keep-alive',
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Headers': 'Cache-Control'
    });

    // Add client to active connections
    clients.add(res);
    // console.log(`[SSE] Client connected. Total clients: ${clients.size}`);

    // Send initial connection confirmation
    res.write(`data: ${JSON.stringify({
      event: 'connected',
      timestamp: Date.now()
    })}\n\n`);

    // Keep connection alive with heartbeat
    const heartbeat = setInterval(() => {
      res.write(':heartbeat\n\n');
    }, 30000);

    // Clean up on disconnect
    req.on('close', () => {
      clearInterval(heartbeat);
      clients.delete(res);
      // console.log(`[SSE] Client disconnected. Total clients: ${clients.size}`);
    });
  });
};

// Notify all connected clients of a payment
const notifyPaymentSuccess = (pinNo, sparkAddress, drinkName, currency, amount) => {
  const message = {
    event: 'payment_received',
    pin: pinNo,
    address: sparkAddress,
    drink: drinkName,
    currency: currency,
    amount: amount,
    timestamp: Date.now()
  };

  console.log(`[SSE] Broadcasting payment notification:`, message);

  // Send to all connected clients
  clients.forEach(client => {
    try {
      client.write(`data: ${JSON.stringify(message)}\n\n`);
    } catch (error) {
      // Remove dead connections
      clients.delete(client);
      console.log(`[SSE] Removed dead client. Total clients: ${clients.size}`);
    }
  });
};

module.exports = {
  setupPaymentEvents,
  notifyPaymentSuccess
};