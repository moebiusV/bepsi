const { ethers } = require("ethers");
const {
  assertIsAddress,
  createSolanaRpc,
  createSolanaRpcSubscriptions,
} = require("@solana/web3.js");
const {
  findAssociatedTokenPda,
  TOKEN_PROGRAM_ADDRESS,
} = require("@solana-program/token");

const { randomPin } = require("../common");
const {
  NETWORKS,
  VENDOR_SELECTION_TO_PIN_MAPPING,
} = require("../constants.js");
const { SOLANA_TREASURY_ADDRESS } = require("../env.js");
const { dispenseFromPayments } = require("../machine");

const ORDER_MEMO_REGEX = new RegExp(
  '^Program log: Memo \\(len \\d+\\): "YVR-BEPSI:0:(\\d+)"$'
  //                                   ^         ^  ^
  //                                   |         |  |
  //                                   |         |  vendor selection
  //                                   |         version
  //                                   discriminator
);

// The websocket subscription is the fast path for payments, but the RPC server drops it
// every few days. Each watcher resubscribes with backoff, and a poll of the treasury token
// account's recent transactions catches payments made while the subscription was down.
const RECONNECT_MIN_MS = 5 * 1000;
const RECONNECT_MAX_MS = 5 * 60 * 1000;
// A subscription that stayed up this long resets the backoff.
const STABLE_SUBSCRIPTION_MS = 60 * 1000;
const POLL_INTERVAL_MS = 60 * 1000;
const POLL_PAGE_SIZE = 100;
const TX_FETCH_ATTEMPTS = 3;
const TX_FETCH_RETRY_MS = 2 * 1000;
// Signatures already handled, so the subscription and the poll never dispense twice.
const PROCESSED_SIGNATURES_KEPT = 1000;

// Optional overrides, e.g. a Helius or Alchemy URL carrying an API key.
const rpcUrl = (network) => process.env.SOLANA_RPC_URL || network.rpc;
const rpcSubscriptionsUrl = (network) =>
  process.env.SOLANA_WS_URL || network.rpcSubscriptions;
// Only the host is logged or reported, never a key in the path or query.
const hostOf = (url) => {
  try {
    return new URL(url).host;
  } catch {
    return "?";
  }
};

const watchers = [];

const sleepUnlessAborted = (ms, abortSignal) =>
  new Promise((resolve) => {
    const done = () => {
      clearTimeout(timer);
      abortSignal.removeEventListener("abort", done);
      resolve();
    };
    const timer = setTimeout(done, ms);
    abortSignal.addEventListener("abort", done, { once: true });
  });

function getTokenAmountChange(expectedMintAddress, transactionMeta) {
  function balancePredicate({ mint, owner }) {
    return mint === expectedMintAddress && owner === SOLANA_TREASURY_ADDRESS;
  }
  const preBalance =
    transactionMeta.preTokenBalances.find(balancePredicate)?.uiTokenAmount
      .amount;
  const postBalance =
    transactionMeta.postTokenBalances.find(balancePredicate)?.uiTokenAmount
      .amount;
  if (preBalance == null || postBalance == null) {
    return;
  }
  return BigInt(postBalance) - BigInt(preBalance);
}

function findVendorSelection(logMessages) {
  for (const logLine of logMessages || []) {
    const match = logLine.match(ORDER_MEMO_REGEX);
    if (match) {
      return parseInt(match[1], 10);
    }
  }
}

// Runs tasks for one watcher one at a time, so a subscription notification and a poll
// can't handle the same transaction concurrently. A failed task never blocks the next.
function enqueue(watcher, task) {
  watcher.queue = watcher.queue
    .then(task)
    .catch((e) => console.error(`[Solana] ${watcher.label}:`, e));
  return watcher.queue;
}

function markProcessed(watcher, signature) {
  watcher.processed.add(signature);
  if (watcher.processed.size > PROCESSED_SIGNATURES_KEPT) {
    watcher.processed.delete(watcher.processed.values().next().value);
  }
}

async function fetchTransaction(watcher, signature, abortSignal) {
  for (let attempt = 1; attempt <= TX_FETCH_ATTEMPTS; attempt++) {
    try {
      const transaction = await watcher.rpc
        .getTransaction(signature, {
          commitment: "confirmed",
          maxSupportedTransactionVersion: 0,
        })
        .send({ abortSignal });
      if (transaction) {
        return transaction;
      }
    } catch (e) {
      if (abortSignal.aborted) {
        return null;
      }
      console.error(
        `[Solana] ${watcher.label}: getTransaction failed (attempt ${attempt}/${TX_FETCH_ATTEMPTS}): ${e.message}`
      );
    }
    if (attempt < TX_FETCH_ATTEMPTS) {
      await sleepUnlessAborted(TX_FETCH_RETRY_MS, abortSignal);
    }
  }
  return null;
}

// Returns false when the transaction couldn't be fetched, so the poll tries it again later.
async function handleSignature(watcher, signature, abortSignal) {
  if (watcher.processed.has(signature)) {
    return true;
  }
  const explorerUrl = `https://explorer.solana.com/tx/${signature}`;
  const transaction = await fetchTransaction(watcher, signature, abortSignal);
  if (transaction == null) {
    if (!abortSignal.aborted) {
      console.error(
        `[Solana] ${watcher.label}: couldn't fetch ${explorerUrl}, will retry on the next poll`
      );
    }
    return false;
  }
  markProcessed(watcher, signature);
  if (transaction.meta == null || transaction.meta.err != null) {
    // Failed transaction: no money moved.
    return true;
  }
  const transferAmount = getTokenAmountChange(
    watcher.mintAddress,
    transaction.meta
  );
  if (transferAmount == null) {
    // This transaction did not change the stablecoin balance.
    return true;
  }
  if (transferAmount < watcher.minimumPayment) {
    // Transfer was not at least 1 stablecoin.
    console.log(
      `Insufficient Transfer Amount on ${
        watcher.network
      } (${ethers.utils.formatUnits(transferAmount, watcher.decimals)} ${
        watcher.symbol
      }) ${explorerUrl}`
    );
    return true;
  }
  const vendorSelection = findVendorSelection(transaction.meta.logMessages);
  let pin;
  if ((pin = VENDOR_SELECTION_TO_PIN_MAPPING[vendorSelection]) == null) {
    pin = randomPin();
    console.warn(
      `Failed to discern vendor selection from logs for transaction ${explorerUrl}. ` +
        `Dispensing random product from pin ${pin}.`
    );
  }
  console.log(
    `payment received ${ethers.utils.formatUnits(
      transferAmount,
      watcher.decimals
    )} ${watcher.symbol}, network: ${
      watcher.network
    } selection: ${vendorSelection}, pin ${pin}, dispensing...`
  );
  watcher.health.lastPaymentAt = Date.now();
  await dispenseFromPayments(pin, watcher.symbol);
  return true;
}

// Handles every transaction on the treasury token account since the last poll, oldest
// first. The cursor only moves past transactions that were actually handled.
async function poll(watcher, abortSignal) {
  try {
    const config = { limit: POLL_PAGE_SIZE, commitment: "confirmed" };
    if (watcher.lastPolledSignature) {
      config.until = watcher.lastPolledSignature;
    }
    const signatures = await watcher.rpc
      .getSignaturesForAddress(watcher.treasuryTokenAccount, config)
      .send({ abortSignal });
    if (signatures.length === POLL_PAGE_SIZE) {
      console.warn(
        `[Solana] ${watcher.label}: ${POLL_PAGE_SIZE}+ new transactions since the last poll, older ones skipped`
      );
    }
    for (const { signature, err } of [...signatures].reverse()) {
      if (err == null && !(await handleSignature(watcher, signature, abortSignal))) {
        break;
      }
      watcher.lastPolledSignature = signature;
    }
    watcher.health.lastPollAt = Date.now();
    watcher.health.lastPollError = null;
  } catch (e) {
    if (abortSignal.aborted) {
      return;
    }
    watcher.health.lastPollError = { at: Date.now(), message: e.message };
    console.error(`[Solana] ${watcher.label}: poll failed: ${e.message}`);
  }
}

async function pollLoop(watcher, abortSignal) {
  while (!abortSignal.aborted) {
    await sleepUnlessAborted(POLL_INTERVAL_MS, abortSignal);
    if (!abortSignal.aborted) {
      await enqueue(watcher, () => poll(watcher, abortSignal));
    }
  }
}

// Only payments made after startup dispense: the newest existing transaction becomes the
// poll cursor.
async function setBaseline(watcher, abortSignal) {
  [watcher.treasuryTokenAccount] = await findAssociatedTokenPda({
    mint: watcher.mintAddress,
    owner: SOLANA_TREASURY_ADDRESS,
    tokenProgram: TOKEN_PROGRAM_ADDRESS,
  });
  const [newest] = await watcher.rpc
    .getSignaturesForAddress(watcher.treasuryTokenAccount, {
      limit: 1,
      commitment: "confirmed",
    })
    .send({ abortSignal });
  watcher.lastPolledSignature = newest ? newest.signature : null;
  watcher.health.lastPollAt = Date.now();
}

async function subscribeLoop(watcher, abortSignal) {
  let delayMs = RECONNECT_MIN_MS;
  while (!abortSignal.aborted) {
    // One controller per attempt, so a dropped subscription's listeners are released
    // instead of piling up on the app-wide abort signal.
    const attempt = new AbortController();
    const abortAttempt = () => attempt.abort();
    abortSignal.addEventListener("abort", abortAttempt, { once: true });
    let subscribedAt = null;
    try {
      const logs = await createSolanaRpcSubscriptions(watcher.wsUrl)
        .logsNotifications(
          { mentions: [watcher.treasuryTokenAccount] },
          { commitment: "confirmed" }
        )
        .subscribe({ abortSignal: attempt.signal });
      subscribedAt = Date.now();
      const { health } = watcher;
      if (health.disconnectedSince && health.reconnects > 0) {
        console.log(
          `[Solana] ${watcher.label}: resubscribed after ${Math.round(
            (subscribedAt - health.disconnectedSince) / 1000
          )}s`
        );
      } else {
        console.log(
          `Watching ${watcher.symbol} on ${watcher.network} (${watcher.mintAddress}) via ${watcher.rpcHost}`
        );
      }
      health.subscribed = true;
      health.subscribedSince = subscribedAt;
      health.disconnectedSince = null;
      // Catch anything paid while there was no subscription.
      enqueue(watcher, () => poll(watcher, abortSignal));
      for await (const log of logs) {
        enqueue(watcher, () =>
          handleSignature(watcher, log.value.signature, abortSignal)
        );
      }
      throw new Error("subscription ended");
    } catch (e) {
      if (abortSignal.aborted) {
        return;
      }
      const { health } = watcher;
      health.subscribed = false;
      health.disconnectedSince = health.disconnectedSince || Date.now();
      health.reconnects++;
      health.lastError = { at: Date.now(), message: e.message };
      if (subscribedAt && Date.now() - subscribedAt >= STABLE_SUBSCRIPTION_MS) {
        delayMs = RECONNECT_MIN_MS;
      }
      console.error(
        `[Solana] ${watcher.label}: subscription lost (${e.message}); retrying in ${
          delayMs / 1000
        }s, payments still caught by the ${POLL_INTERVAL_MS / 1000}s poll`
      );
    } finally {
      abortSignal.removeEventListener("abort", abortAttempt);
      attempt.abort();
    }
    await sleepUnlessAborted(delayMs, abortSignal);
    delayMs = Math.min(delayMs * 2, RECONNECT_MAX_MS);
  }
}

async function runWatcher(watcher, abortSignal) {
  let delayMs = RECONNECT_MIN_MS;
  while (!abortSignal.aborted) {
    try {
      await setBaseline(watcher, abortSignal);
      break;
    } catch (e) {
      if (abortSignal.aborted) {
        return;
      }
      watcher.health.lastError = { at: Date.now(), message: e.message };
      console.error(
        `[Solana] ${watcher.label}: startup failed (${e.message}); retrying in ${
          delayMs / 1000
        }s`
      );
      await sleepUnlessAborted(delayMs, abortSignal);
      delayMs = Math.min(delayMs * 2, RECONNECT_MAX_MS);
    }
  }
  pollLoop(watcher, abortSignal);
  await subscribeLoop(watcher, abortSignal);
}

function startSolanaListener(abortSignal) {
  assertIsAddress(SOLANA_TREASURY_ADDRESS);
  Object.values(NETWORKS)
    .filter(({ implementation }) => implementation === "SVM")
    .forEach((network) => {
      for (const { address: mintAddress, decimals, symbol } of network.stablecoins) {
        assertIsAddress(mintAddress);
        const wsUrl = rpcSubscriptionsUrl(network);
        const watcher = {
          network: network.name,
          symbol,
          label: `${symbol} on ${network.name}`,
          mintAddress,
          decimals,
          minimumPayment: 1n * 10n ** BigInt(decimals),
          rpc: createSolanaRpc(rpcUrl(network)),
          wsUrl,
          rpcHost: hostOf(wsUrl),
          treasuryTokenAccount: null,
          lastPolledSignature: null,
          processed: new Set(),
          queue: Promise.resolve(),
          health: {
            subscribed: false,
            subscribedSince: null,
            disconnectedSince: Date.now(),
            reconnects: 0,
            lastError: null,
            lastPollAt: null,
            lastPollError: null,
            lastPaymentAt: null,
          },
        };
        watchers.push(watcher);
        runWatcher(watcher, abortSignal).catch((e) =>
          console.error(`[Solana] ${watcher.label}: watcher stopped:`, e)
        );
      }
    });
}

const getSolanaHealth = () => ({
  watchers: watchers.map((w) => ({
    network: w.network,
    symbol: w.symbol,
    rpcHost: w.rpcHost,
    ...w.health,
  })),
});

module.exports = {
  startSolanaListener,
  getSolanaHealth,
};
