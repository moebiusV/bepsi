const { ethers } = require("ethers");
const {
  DAI_ADDRESS,
  USDC_ADDRESS,
  USDT_ADDRESS,
  MIN_USD_PAYMENT,
  VENDOR_SELECTION_TO_PIN_MAPPING,
} = require("./constants");
const { PAYMENT_ADDRESS } = require("./env");

// eslint-disable-next-line
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const nowTimestamp = () =>
  parseInt((new Date().getTime() / 1000).toString(), 10);

function randomElement(array) {
  return array[Math.floor(Math.random() * array.length)];
}

function randomPin() {
  return randomElement(Object.values(VENDOR_SELECTION_TO_PIN_MAPPING));
}

function match(a, b) {
  return (a || "").toLowerCase() === (b || "").toLowerCase();
}

function contains(a, b) {
  let isT = false;
  for (let i = 0; i < a.length; i++) {
    if (match(a[i], b)) {
      isT = true;
    }
  }
  return isT;
}

let abortController;
function createExitAwareAbortController() {
  if (!abortController) {
    abortController = new AbortController();
    const abortOnExit = () => {
      if (!abortController.signal.aborted) {
        abortController.abort();
      }
    };
    process.on("SIGINT", abortOnExit);
    process.on("SIGTERM", abortOnExit);
  }
  return abortController;
}

module.exports = {
  createExitAwareAbortController,
  sleep,
  match,
  contains,
  randomElement,
  randomPin,
  nowTimestamp,
};
