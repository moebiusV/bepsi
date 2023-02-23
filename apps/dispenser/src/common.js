const { ethers } = require('ethers');
const {
  DAI_ADDRESS,
  USDC_ADDRESS,
  USDT_ADDRESS,
  MIN_USD_PAYMENT,
  VENDOR_SELECTION_TO_PIN_MAPPING,
} = require('./constants');
const { PAYMENT_ADDRESS } = require('./env');

// eslint-disable-next-line
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const nowTimestamp = () => parseInt((new Date().getTime() / 1000).toString(), 10).toString();

function randomElement(array) {
  return array[Math.floor(Math.random() * array.length)];
}

function randomPin() {
  return randomElement(Object.values(VENDOR_SELECTION_TO_PIN_MAPPING));
}

function match(a, b) {
  return (a || '').toLowerCase() === (b || '').toLowerCase();
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

function isPaymentValid(stablecoinAddress, to, amountWei) {
  // Needs to  be sent to the multisig
  if (!match(to, PAYMENT_ADDRESS)) {
    return false;
  }

  if (match(stablecoinAddress, DAI_ADDRESS)) {
    return ethers.utils
      .parseUnits(amountWei, 0)
      .gte(ethers.utils.parseUnits(MIN_USD_PAYMENT.toString()));
  }

  // USDC and USDT = 6 decimals
  if (contains([USDC_ADDRESS, USDT_ADDRESS], stablecoinAddress)) {
    return ethers.utils
      .parseUnits(amountWei, 0)
      .gte(ethers.utils.parseUnits(MIN_USD_PAYMENT.toString(), 6));
  }

  return false;
}

function getStablecoinName(x) {
  if (match(x, USDC_ADDRESS)) {
    return 'USDC';
  }

  if (match(x, USDT_ADDRESS)) {
    return 'USDT';
  }

  if (match(x, DAI_ADDRESS)) {
    return 'DAI';
  }

  return 'UNKNOWN';
}

function parseStablecoin(addr, x) {
  if (contains([USDC_ADDRESS, USDT_ADDRESS], addr)) {
    return ethers.utils.formatUnits(x, 6);
  }
  return ethers.utils.formatUnits(x, 18);
}

module.exports = {
  sleep,
  match,
  contains,
  isPaymentValid,
  getStablecoinName,
  parseStablecoin,
  randomElement,
  randomPin,
  nowTimestamp,
};
