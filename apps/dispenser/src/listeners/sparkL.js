// Dependencies
const { dispenseFromPayments, logPayment } = require("../machine");
const { notifyPaymentSuccess } = require("../payment-events");

// Dynamic import for ES modules in CommonJS context
let IssuerSparkWallet;

// Load and cache the Spark SDK module
async function loadSparkSDK() {
  if (!IssuerSparkWallet) {
    const module = await import("@buildonspark/issuer-sdk");
    IssuerSparkWallet = module.IssuerSparkWallet;
  }
  return IssuerSparkWallet;
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
  wallet.on('transfer:claimed', async (transferId) => {
    if (!initialBalanceScanComplete) {
      return;
    }

    try {
      const currentBalance = await wallet.getBalance();
      const currentSatsNum = Number(currentBalance.balance);
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
    console.log(`[Spark] 🔗 EventEmitter connected for pin ${pinNo} (sats detection active)`);
  });

  wallet.on('stream:disconnected', (reason) => {
    console.log(`[Spark] ⚠️  EventEmitter disconnected for pin ${pinNo}: ${reason}`);
  });
};

const checkForTokenPayments = async () => {
  try {
    for (const [pinNo, paymentRequest] of pinPaymentAddresses) {
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
            if (tokenData && tokenData.balance) {
              const rawBalance = BigInt(tokenData.balance);
              // BEPSI tokens have 6 decimals - hardcode since blockchain metadata is wrong
              const decimals = 6; // tokenData.decimals is returning 0, but BEPSI has 6 decimals
              const divisor = Math.pow(10, decimals);
              tokenAmount = Number(rawBalance) / divisor;
            }
          }


          // Get previous token balance for this pin and token
          const tokenBalanceKey = `${pinNo}_${tokenConfig.identifier}`;
          const previousTokenAmount = previousTokenBalances.get(tokenBalanceKey) || 0;

          const paymentAmount = tokenAmount - previousTokenAmount;
          if (paymentAmount >= requiredTokenAmount && tokenAmount > previousTokenAmount && !isNaN(tokenAmount) && initialBalanceScanComplete) {
            console.log(`[Spark] ✅ TOKEN PAYMENT DETECTED (Polling) for pin ${pinNo}!`);
            console.log(`[Spark] - Token: ${tokenConfig.name}`);
            console.log(`[Spark] - New balance: ${tokenAmount} (was ${previousTokenAmount})`);
            console.log(`[Spark] - Increase: ${tokenAmount - previousTokenAmount} tokens`);
            console.log(`[Spark] - Required: ${requiredTokenAmount}`);
            console.log(`[Spark] - Address: ${paymentRequest.address}`);

            // Update stored token balance
            previousTokenBalances.set(tokenBalanceKey, tokenAmount);


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
    }

    if (!initialBalanceScanComplete) {
      initialBalanceScanComplete = true;
      console.log('[Spark] Initial balance scan complete');
      console.log('[Spark] - Sats: EventEmitter (real-time)');
      console.log('[Spark] - Tokens: Polling (5-second intervals)');
    }

  } catch (error) {
    console.error(`[Spark] Error checking token payments:`, error.message);
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

  // Sweep from each pin wallet
  const allPins = getVendingPins();

  for (const pinNo of allPins) {
    try {
      const pinWallet = await getWalletForProduct(pinNo);
      const balance = await pinWallet.getBalance();
      const availableSats = Number(balance.balance);

      // Check and sweep sats
      if (availableSats > 100) {
        try {
          await pinWallet.transfer({
            receiverSparkAddress: treasuryAddress,
            amountSats: availableSats
          });
          console.log(`[Spark] ✅ Swept ${availableSats} sats from pin ${pinNo}`);
        } catch (err) {
          console.error(`[Spark] ❌ Failed to sweep ${availableSats} sats from pin ${pinNo}:`, err.message);
        }
      }

      // Check and sweep tokens
      if (balance.tokenBalances && balance.tokenBalances.size > 0) {
        for (const [tokenId, tokenData] of balance.tokenBalances) {
          try {
            const rawAmount = BigInt(tokenData.balance);
            if (rawAmount > 0n) {
              const decimals = tokenData.decimals || 0;
              const divisor = Math.pow(10, decimals);
              const tokenAmount = Number(rawAmount) / divisor;

              await pinWallet.transferTokens({
                tokenIdentifier: tokenId,
                tokenAmount: rawAmount,
                receiverSparkAddress: treasuryAddress
              });

              console.log(`[Spark] ✅ Swept ${tokenAmount} tokens from pin ${pinNo}`);
            }
          } catch (tokenErr) {
            console.error(`[Spark] ❌ Failed to sweep tokens from pin ${pinNo}:`, tokenErr.message);
          }
        }
      }
    } catch (error) {
      console.error(`[Spark] Error checking pin ${pinNo}:`, error.message);
    }
  }

  // Only log if something was actually swept
  console.log(`[Spark] Fund sweep complete`);
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

      } catch (error) {
        console.error(`[Spark] Failed to load pin ${pinNo}:`, error.message);
      }
    }

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
    }, 5000);

    // Check if treasury is configured
    const treasuryAddress = getTreasuryAddress();
    if (treasuryAddress) {
      console.log(`[Spark] Treasury consolidation enabled: ${treasuryAddress}`);

      // Start periodic fund sweep every 10 minutes
      setInterval(() => {
        sweepAllFundsToTreasury();
      }, 10 * 60 * 1000); // 10 minutes
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
};