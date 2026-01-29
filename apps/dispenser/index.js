const express = require("express");
const { createExitAwareAbortController } = require("./src/common");
const { startDiscordListener } = require("./src/listeners/discordL");
const { startEvmListener } = require("./src/listeners/evmL");
const { startSolanaListener } = require("./src/listeners/solanaL");
const { startMachineChecker } = require("./src/listeners/machineL");
const { startArkadeListener } = require("./src/listeners/arkadeL");
const { startLightningListener } = require("./src/listeners/lightningL");
const { startSparkListener } = require("./src/listeners/sparkL");
const { isServiceEnabled } = require("./src/env");
const { setupPaymentEvents } = require("./src/payment-events");

const main = async () => {
  const abortController = createExitAwareAbortController();

  // Start Express server for SSE (Server-Sent Events)
  // Broadcasts real-time payment notifications to connected frontend clients
  const app = express();
  setupPaymentEvents(app);

  const server = app.listen(3500, () => {
    console.log("[SSE] Payment events server running on port 3500");
  });

  // Start services only if they are properly configured and not disabled
  if (isServiceEnabled("discord")) {
    try {
      startDiscordListener();
    } catch (error) {
      console.error("❌ Discord listener failed to start:", error.message);
    }
  } else {
    console.log(
      "[" +
        new Date().toLocaleTimeString() +
        "] - Discord listener disabled or misconfigured",
    );
  }

  if (isServiceEnabled("evm")) {
    try {
      startEvmListener();
    } catch (error) {
      console.error("❌ EVM listener failed to start:", error.message);
    }
  } else {
    console.log(
      "[" +
        new Date().toLocaleTimeString() +
        "] - EVM listener disabled or misconfigured",
    );
  }

  if (isServiceEnabled("solana")) {
    try {
      startSolanaListener(abortController.signal);
    } catch (error) {
      console.error("❌ Solana listener failed to start:", error.message);
    }
  } else {
    console.log(
      "[" +
        new Date().toLocaleTimeString() +
        "] - Solana listener disabled or misconfigured",
    );
  }

  if (isServiceEnabled("arkade")) {
    try {
      startArkadeListener();
    } catch (error) {
      console.error("❌ Arkade listener failed to start:", error.message);
    }
  } else {
    console.log(
      "[" +
        new Date().toLocaleTimeString() +
        "] - Arkade listener disabled or misconfigured",
    );
  }

  if (isServiceEnabled("lightning")) {
    try {
      startLightningListener();
    } catch (error) {
      console.error("❌ Lightning listener failed to start:", error.message);
    }
  } else {
    console.log(
      "[" +
        new Date().toLocaleTimeString() +
        "] - Lightning listener disabled or misconfigured",
    );
  }

  if (isServiceEnabled("spark")) {
    try {
      startSparkListener();
    } catch (error) {
      console.error("❌ [Spark] Listener failed to start:", error.message);
    }
  } else {
    console.log(
      "[" +
        new Date().toLocaleTimeString() +
        "] - [Spark] Listener disabled or misconfigured",
    );
  }

  // startMachineChecker();
};

main();
