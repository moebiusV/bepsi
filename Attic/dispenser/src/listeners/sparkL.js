// Dependencies
const { dispenseFromPayments, logPayment } = require("../machine");
const { notifyPaymentSuccess } = require("../payment-events");

// Dynamic import for ES modules in CommonJS context
let SparkWallet;

// Load and cache the Spark SDK module
async function loadSparkSDK() {
  if (!SparkWallet) {
    const module = await import("@buildonspark/spark-sdk");
    SparkWallet = module.SparkWallet;
  }
  return SparkWallet;
}

// ============================================================================
// Configuration
// ============================================================================

// Get list of vending machine pins from environment
const getVendingPins = () => {
  const pinsEnv = process.env.SPARK_VENDING_PINS || '516,517,518,524,525,528';
  return pinsEnv.split(',').map(pin => parseInt(pin.trim()));
};

// Get human-readable name for a pin
const getPinName = (pinNo) => {
  return process.env[`PIN_${pinNo}_NAME`] || `pin ${pinNo}`;
};

// Create mapping of pin numbers to human-readable names
const pinToItem = {};
getVendingPins().forEach(pin => {
  pinToItem[pin] = getPinName(pin);
});

// Build token configuration from environment variables
// Returns supported tokens with pin-specific amounts
const getSupportedTokens = () => {
  const tokens = {};
  const pins = getVendingPins();

  // Get token identifiers from env
  const tokenKeys = process.env.SPARK_SUPPORTED_TOKEN_KEYS ? process.env.SPARK_SUPPORTED_TOKEN_KEYS.split(',') : ['BepsiToken'];

  tokenKeys.forEach(tokenKey => {
    const identifier = process.env[`SPARK_${tokenKey.toUpperCase()}_IDENTIFIER`];
    const name = process.env[`SPARK_${tokenKey.toUpperCase()}_NAME`] || tokenKey;

    if (identifier) {
      const pinAmounts = {};
      pins.forEach(pin => {
        const amount = parseFloat(process.env[`SPARK_${tokenKey.toUpperCase()}_PIN_${pin}_AMOUNT`]);
        if (isNaN(amount)) {
          throw new Error(`Missing required environment variable: SPARK_${tokenKey.toUpperCase()}_PIN_${pin}_AMOUNT`);
        }
        pinAmounts[pin] = amount;
      });

      tokens[tokenKey] = {
        identifier,
        name,
        pinAmounts
      };
    }
  });

  return tokens;
};

// Get Spark wallet address and mnemonic for a specific pin
const getPinConfig = (pinNo) => {
  const address = process.env[`SPARK_PIN_${pinNo}_ADDRESS`];
  const mnemonic = process.env[`SPARK_PIN_${pinNo}_MNEMONIC`];

  if (!address || !mnemonic) {
    throw new Error(`Missing environment variables for pin ${pinNo}. Need SPARK_PIN_${pinNo}_ADDRESS and SPARK_PIN_${pinNo}_MNEMONIC`);
  }

  return { address, mnemonic };
};

// Get treasury address for fund consolidation (optional)
const getTreasuryAddress = () => {
  const address = process.env.SPARK_TREASURY_ADDRESS;
  if (!address) {
    console.log("[Spark] Treasury consolidation disabled - missing SPARK_TREASURY_ADDRESS");
    return null;
  }
  return address;
};

const SUPPORTED_TOKENS = getSupportedTokens();

// ============================================================================
// Runtime State
// ============================================================================
const pinPaymentAddresses = new Map(); // Payment addresses for each pin
const pinWallets = new Map(); // Wallet instances for each pin
const previousSatsBalances = new Map();
const previousTokenBalances = new Map();
let initialBalanceScanComplete = false;
const satsProcessedViaEvent = new Set();
const streamDisconnectedAt = new Map(); // pin → timestamp of disconnect
const DISCONNECT_WARN_MS = 15 * 60 * 1000; // 15 minutes

// A pin only detects payments after its own first balance scan, so a pin that
// loads late never mistakes funds that arrived while it was unwatched for a new payment.
const baselinedPins = new Set();

// Pins that failed to load at startup are retried with backoff until they load.
const failedPins = new Map(); // pin → { attempts, nextRetryAt, lastError }
let pinRetryInProgress = false;

// Payments detected per pin since that pin's last sweep. Funds swept from a pin
// with no detection mean someone paid and nothing dispensed.
const detectedSinceSweep = new Map();
const unmatchedSweeps = []; // most recent first, capped
const MAX_UNMATCHED_SWEEPS = 50;

let pollInProgress = false;
let sweepInProgress = false;
let lastPollCompletedAt = null;
let lastPollError = null;

const POLL_INTERVAL_MS = parseInt(process.env.SPARK_POLL_INTERVAL_MS || "5000");
const PIN_RETRY_BASE_MS = parseInt(process.env.SPARK_PIN_RETRY_BASE_MS || "60000");
const PIN_RETRY_MAX_MS = parseInt(process.env.SPARK_PIN_RETRY_MAX_MS || "600000");
const SWEEP_INTERVAL_MS = parseInt(process.env.SPARK_SWEEP_INTERVAL_MS || "600000");

const recordDetection = (pinNo) => {
  detectedSinceSweep.set(pinNo, (detectedSinceSweep.get(pinNo) || 0) + 1);
};

// ============================================================================
// Utility Functions
// ============================================================================

const getWalletForProduct = async (pinNo) => {
  if (pinWallets.has(pinNo)) {
    return pinWallets.get(pinNo);
  }

  try {
    const SparkWallet = await loadSparkSDK();
    const { mnemonic } = getPinConfig(pinNo);

    const { wallet } = await SparkWallet.initialize({
      mnemonicOrSeed: mnemonic,
      options: {
        network: "MAINNET",
      },
    });

    pinWallets.set(pinNo, wallet);
    console.log(`[Spark] Initialized wallet for pin ${pinNo}: ${wallet.sparkAddress}`);

    setupEventEmitterForPin(pinNo, wallet);

    return wallet;
  } catch (error) {
    console.error(`Failed to create Spark wallet for pin ${pinNo}:`, error.message);
    throw error;
  }
};

// ============================================================================
// Payment Detection
// ============================================================================

const setupEventEmitterForPin = (pinNo, wallet) => {
  wallet.on('transfer:claimed', async (transferId, updatedBalance) => {
    if (!baselinedPins.has(pinNo)) {
      return;
    }

    try {
      const currentSatsNum = updatedBalance != null
        ? Number(updatedBalance)
        : Number((await wallet.getBalance()).balance);
      const previousSats = previousSatsBalances.get(pinNo) || 0;
      const paymentRequest = pinPaymentAddresses.get(pinNo);

      if (!paymentRequest) {
        return;
      }

      const requiredAmount = parseInt(paymentRequest.amount);
      const satsIncrease = currentSatsNum - previousSats;

      if (currentSatsNum >= requiredAmount && currentSatsNum > previousSats) {
        console.log(`[Spark] ✅ SATS PAYMENT DETECTED (EventEmitter) for pin ${pinNo}!`);
        console.log(`[Spark] - Transfer ID: ${transferId}`);
        console.log(`[Spark] - Amount: ${satsIncrease} sats`);
        console.log(`[Spark] - New balance: ${currentSatsNum} sats`);
        console.log(`[Spark] - Required: ${requiredAmount} sats`);
        console.log(`[Spark] - Address: ${paymentRequest.address}`);

        previousSatsBalances.set(pinNo, currentSatsNum);
        satsProcessedViaEvent.add(transferId);
        recordDetection(pinNo);

        console.log(`[Spark] 🥤 Dispensing for pin ${pinNo}...`);
        logPayment(pinNo, "sats", satsIncrease, "spark");
        dispenseFromPayments(pinNo, "sats");

        // Notify frontend via SSE
        const drinkName = getPinName(pinNo);
        notifyPaymentSuccess(pinNo, paymentRequest.address, drinkName, "sats", satsIncrease);
      }
    } catch (error) {
      console.error(`[Spark] Error handling transfer event for pin ${pinNo}:`, error.message);
    }
  });

  wallet.on('stream:connected', () => {
    if (streamDisconnectedAt.has(pinNo)) {
      const downtime = Date.now() - streamDisconnectedAt.get(pinNo);
      console.log(`[Spark] 🔗 EventEmitter reconnected for pin ${pinNo} (was down ${Math.round(downtime / 1000)}s)`);
      streamDisconnectedAt.delete(pinNo);
    }
  });

  wallet.on('stream:disconnected', (reason) => {
    streamDisconnectedAt.set(pinNo, Date.now());
  });
};

const checkForTokenPayments = async () => {
  // On a slow network a round can outlast the interval; never stack rounds on top of each other.
  if (pollInProgress) {
    return;
  }
  pollInProgress = true;

  try {
    let roundError = null;

    for (const [pinNo, paymentRequest] of pinPaymentAddresses) {
      try {
        const wallet = await getWalletForProduct(pinNo);
        const currentBalance = await wallet.getBalance();
        const currentSatsNum = Number(currentBalance.balance);

        previousSatsBalances.set(pinNo, currentSatsNum);

        for (const [tokenKey, tokenConfig] of Object.entries(SUPPORTED_TOKENS)) {
          try {
            const requiredTokenAmount = tokenConfig.pinAmounts[pinNo];

            // Skip if this pin doesn't have a configured token amount
            if (!requiredTokenAmount) {
              continue;
            }

            // Get token amount (0 if token doesn't exist in wallet)
            let tokenAmount = 0;
            if (currentBalance.tokenBalances.has(tokenConfig.identifier)) {
              const tokenData = currentBalance.tokenBalances.get(tokenConfig.identifier);
              if (tokenData && tokenData.ownedBalance != null) {
                const decimals = tokenData.tokenMetadata?.decimals ?? 6;
                tokenAmount = Number(tokenData.ownedBalance) / Math.pow(10, decimals);
              }
            }


            // Get previous token balance for this pin and token
            const tokenBalanceKey = `${pinNo}_${tokenConfig.identifier}`;
            const previousTokenAmount = previousTokenBalances.get(tokenBalanceKey) || 0;

            const paymentAmount = tokenAmount - previousTokenAmount;
            if (paymentAmount >= requiredTokenAmount && tokenAmount > previousTokenAmount && !isNaN(tokenAmount) && baselinedPins.has(pinNo)) {
              console.log(`[Spark] ✅ TOKEN PAYMENT DETECTED (Polling) for pin ${pinNo}!`);
              console.log(`[Spark] - Token: ${tokenConfig.name}`);
              console.log(`[Spark] - New balance: ${tokenAmount} (was ${previousTokenAmount})`);
              console.log(`[Spark] - Increase: ${tokenAmount - previousTokenAmount} tokens`);
              console.log(`[Spark] - Required: ${requiredTokenAmount}`);
              console.log(`[Spark] - Address: ${paymentRequest.address}`);

              // Update stored token balance
              previousTokenBalances.set(tokenBalanceKey, tokenAmount);
              recordDetection(pinNo);

              // Dispense the product
              console.log(`[Spark] 🥤 Dispensing for pin ${pinNo}...`);
              const tokenIncrease = tokenAmount - previousTokenAmount;
              logPayment(pinNo, "tokens", tokenIncrease, "spark");
              dispenseFromPayments(pinNo, "tokens");

              // Notify frontend via SSE
              const drinkName = getPinName(pinNo);
              notifyPaymentSuccess(pinNo, paymentRequest.address, drinkName, "tokens", tokenAmount - previousTokenAmount);
              break;
            }

            // Always update the tracked token balance even if no payment
            previousTokenBalances.set(tokenBalanceKey, tokenAmount);
          } catch (tokenError) {
            // Token balance check failed, continue to next token
            console.error(`[Spark] Error checking ${tokenConfig.name} balance for pin ${pinNo}:`, tokenError.message);
          }
        }

        if (!baselinedPins.has(pinNo)) {
          baselinedPins.add(pinNo);
          console.log(`[Spark] Pin ${pinNo} baseline recorded, now detecting payments`);
        }
      } catch (pinError) {
        // One unreachable pin must not stop the other pins from being checked.
        roundError = pinError;
        console.error(`[Spark] Error checking payments for pin ${pinNo}:`, pinError.message);
      }
    }

    if (roundError) {
      lastPollError = { at: Date.now(), message: roundError.message };
    } else {
      lastPollCompletedAt = Date.now();
    }

    if (!initialBalanceScanComplete) {
      initialBalanceScanComplete = true;
      console.log('[Spark] Initial balance scan complete');
      console.log('[Spark] - Sats: EventEmitter (real-time)');
      console.log('[Spark] - Tokens: Polling (5-second intervals)');
    }

    // Warn if any stream has been disconnected for too long
    for (const [pinNo, disconnectTime] of streamDisconnectedAt) {
      const downtime = Date.now() - disconnectTime;
      if (downtime >= DISCONNECT_WARN_MS) {
        console.log(`[Spark] ⚠️  EventEmitter for pin ${pinNo} disconnected for ${Math.round(downtime / 60000)}min — sats events may be missed`);
      }
    }

  } catch (error) {
    lastPollError = { at: Date.now(), message: error.message };
    console.error(`[Spark] Error checking token payments:`, error.message);
  } finally {
    pollInProgress = false;
  }
};


// ============================================================================
// Treasury Management
// ============================================================================

// Sweep all funds from pin wallets to treasury address
const sweepAllFundsToTreasury = async () => {
  const treasuryAddress = getTreasuryAddress();
  if (!treasuryAddress) {
    console.log(`[Spark] Treasury not configured, skipping sweep`);
    return;
  }

  // A sweep on a slow network can outlast the interval; never run two at once.
  if (sweepInProgress) {
    return;
  }
  sweepInProgress = true;

  try {
    // Only sweep pins that are being watched. Funds in an unwatched pin stay put
    // (and are flagged once the pin loads) instead of silently vanishing.
    let swept = false;

    for (const pinNo of pinPaymentAddresses.keys()) {
      if (!baselinedPins.has(pinNo)) {
        continue;
      }

      try {
        const pinWallet = await getWalletForProduct(pinNo);
        const balance = await pinWallet.getBalance();
        const availableSats = Number(balance.balance);
        const sweptItems = [];

        // Check and sweep sats. Skip sats the payment detector hasn't seen yet so
        // a payment arriving mid-sweep is dispensed first and swept next time.
        const satsSeenByDetector = previousSatsBalances.get(pinNo) ?? 0;
        if (availableSats > 100 && availableSats > satsSeenByDetector) {
          console.log(`[Spark] Pin ${pinNo} has ${availableSats - satsSeenByDetector} unprocessed sats, deferring sweep`);
        } else if (availableSats > 100) {
          try {
            await pinWallet.transfer({
              receiverSparkAddress: treasuryAddress,
              amountSats: availableSats
            });
            swept = true;
            sweptItems.push(`${availableSats} sats`);
            console.log(`[Spark] ✅ Swept ${availableSats} sats from pin ${pinNo}`);
          } catch (err) {
            console.error(`[Spark] ❌ Failed to sweep ${availableSats} sats from pin ${pinNo}:`, err.message);
          }
        }

        // Check and sweep tokens
        if (balance.tokenBalances && balance.tokenBalances.size > 0) {
          for (const [tokenId, tokenData] of balance.tokenBalances) {
            try {
              const rawAmount = tokenData.ownedBalance;
              if (rawAmount > 0n) {
                const decimals = tokenData.tokenMetadata?.decimals ?? 0;
                const tokenAmount = Number(rawAmount) / Math.pow(10, decimals);

                const tokensSeenByDetector = previousTokenBalances.get(`${pinNo}_${tokenId}`);
                if (tokensSeenByDetector !== undefined && tokenAmount > tokensSeenByDetector) {
                  console.log(`[Spark] Pin ${pinNo} has ${tokenAmount - tokensSeenByDetector} unprocessed tokens, deferring sweep`);
                  continue;
                }

                await pinWallet.transferTokens({
                  tokenIdentifier: tokenId,
                  tokenAmount: rawAmount,
                  receiverSparkAddress: treasuryAddress
                });

                swept = true;
                sweptItems.push(`${tokenAmount} tokens`);
                console.log(`[Spark] ✅ Swept ${tokenAmount} tokens from pin ${pinNo}`);
              }
            } catch (tokenErr) {
              console.error(`[Spark] ❌ Failed to sweep tokens from pin ${pinNo}:`, tokenErr.message);
            }
          }
        }

        if (sweptItems.length > 0) {
          if (!detectedSinceSweep.get(pinNo)) {
            const event = { at: Date.now(), pin: pinNo, drink: getPinName(pinNo), swept: sweptItems.join(", ") };
            unmatchedSweeps.unshift(event);
            unmatchedSweeps.length = Math.min(unmatchedSweeps.length, MAX_UNMATCHED_SWEEPS);
            console.error(`[Spark] ⚠️ UNMATCHED SWEEP: swept ${event.swept} from pin ${pinNo} (${event.drink}) with no detected payment since the last sweep`);
          }
          detectedSinceSweep.set(pinNo, 0);
        }
      } catch (error) {
        console.error(`[Spark] Error checking pin ${pinNo}:`, error.message);
      }
    }

    if (swept) {
      console.log(`[Spark] Fund sweep complete`);
    }
  } finally {
    sweepInProgress = false;
  }
};

// ============================================================================
// Pin Loading
// ============================================================================

// Initialize a pin's wallet and start watching it for payments. Throws on failure.
const loadPin = async (pinNo) => {
  const { address } = getPinConfig(pinNo);

  // Initialize the wallet (this verifies the mnemonic works)
  await getWalletForProduct(pinNo);

  // Get pin-specific sats amount (required)
  const pinSpecificAmount = process.env[`SPARK_PIN_${pinNo}_AMOUNT`];
  if (!pinSpecificAmount) {
    throw new Error(`Missing required environment variable: SPARK_PIN_${pinNo}_AMOUNT`);
  }
  const amount = parseInt(pinSpecificAmount);

  const paymentRequest = {
    pinNo,
    address: address, // Use pre-generated address
    amount: amount
  };

  pinPaymentAddresses.set(pinNo, paymentRequest);
  console.log(`[Spark] Pin ${pinNo}: Send ${paymentRequest.amount} sats to ${address}`);
};

const markPinFailed = (pinNo, error) => {
  const previous = failedPins.get(pinNo);
  const attempts = (previous?.attempts || 0) + 1;
  const delay = Math.min(PIN_RETRY_BASE_MS * Math.pow(2, attempts - 1), PIN_RETRY_MAX_MS);
  failedPins.set(pinNo, { attempts, nextRetryAt: Date.now() + delay, lastError: error.message });
  console.error(`[Spark] Failed to load pin ${pinNo} (attempt ${attempts}), retrying in ${Math.round(delay / 1000)}s:`, error.message);
};

// Retry pins that failed to load. One retry pass at a time: wallet init can take minutes on a bad network.
const retryFailedPins = async () => {
  if (pinRetryInProgress || failedPins.size === 0) {
    return;
  }
  pinRetryInProgress = true;

  try {
    for (const [pinNo, state] of failedPins) {
      if (Date.now() < state.nextRetryAt) {
        continue;
      }
      try {
        await loadPin(pinNo);
        failedPins.delete(pinNo);
        console.log(`[Spark] ✅ Pin ${pinNo} loaded after ${state.attempts} failed attempt(s)`);
      } catch (error) {
        markPinFailed(pinNo, error);
      }
    }
  } finally {
    pinRetryInProgress = false;
  }
};

// Snapshot of listener state for the /health endpoint.
const getSparkHealth = () => {
  const expectedPins = getVendingPins();
  return {
    pinsExpected: expectedPins.length,
    pinsLoaded: pinPaymentAddresses.size,
    pinsDetecting: baselinedPins.size,
    failedPins: Object.fromEntries(
      [...failedPins].map(([pinNo, s]) => [pinNo, { attempts: s.attempts, lastError: s.lastError, nextRetryAt: s.nextRetryAt }])
    ),
    lastPollCompletedAt,
    lastPollError,
    pollInProgress,
    streamsDisconnectedSince: Object.fromEntries(streamDisconnectedAt),
    unmatchedSweeps,
  };
};

// ============================================================================
// Main Entry Point
// ============================================================================

// Initialize and start the Spark payment listener system
const startSparkListener = async () => {
  console.log("[Spark] Starting Spark payment listener...");

  try {
    // Load pre-generated payment addresses for all vending machine pins
    const availablePins = getVendingPins(); // Get pins from environment

    console.log("[Spark] Loading pre-generated wallets for each pin...");
    for (const pinNo of availablePins) {
      try {
        await loadPin(pinNo);
      } catch (error) {
        markPinFailed(pinNo, error);
      }
    }

    if (failedPins.size > 0) {
      console.error(`[Spark] ⚠️ ${failedPins.size} of ${availablePins.length} pins failed to load and will be retried: ${[...failedPins.keys()].join(", ")}`);
    }
    setInterval(retryFailedPins, Math.min(10000, PIN_RETRY_BASE_MS));

    console.log(`[Spark] 🎯 Vending machine ready! Each pin has unique address:`);
    for (const [pinNo, paymentRequest] of pinPaymentAddresses) {
      console.log(`[Spark] Pin ${pinNo}: ${paymentRequest.address}`);
    }

    // Show supported tokens
    if (Object.keys(SUPPORTED_TOKENS).length > 0) {
      console.log(`[Spark] 🪙 Accepted tokens:`);
      for (const [tokenKey, tokenConfig] of Object.entries(SUPPORTED_TOKENS)) {
        console.log(`[Spark] - ${tokenConfig.name} requirements:`);
        for (const [pinNo, amount] of Object.entries(tokenConfig.pinAmounts)) {
          const drinkName = pinToItem[pinNo] || `pin ${pinNo}`;
          console.log(`[Spark]   Pin ${pinNo} (${drinkName}): ${amount} tokens`);
        }
      }
    }

    setInterval(async () => {
      await checkForTokenPayments();
    }, POLL_INTERVAL_MS);

    // Check if treasury is configured
    const treasuryAddress = getTreasuryAddress();
    if (treasuryAddress) {
      console.log(`[Spark] Treasury consolidation enabled: ${treasuryAddress}`);

      // Start periodic fund sweep every 10 minutes
      setInterval(() => {
        sweepAllFundsToTreasury();
      }, SWEEP_INTERVAL_MS);
    }

    console.log("[Spark] Spark payment listener started successfully");

  } catch (error) {
    console.error("[Spark] Failed to start Spark listener:", error.message);

    // Retry connection after a delay
    const reconnectInterval = 60000; // 1 minute
    console.log(`[Spark] Retrying connection in ${reconnectInterval / 1000} seconds...`);
    setTimeout(startSparkListener, reconnectInterval);
  }
};

module.exports = {
  startSparkListener,
  getSparkHealth,
};