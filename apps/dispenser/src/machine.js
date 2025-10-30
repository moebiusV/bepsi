const { Gpio } = require("onoff");
const axios = require("axios");
const { sleep, nowTimestamp } = require("./common");
const { NOCODB_API_TOKEN } = require("./env");

let isDispensing = false;

const NOCO_CREATE_NEW_PURCHASE_URL =
  process.env.NOCO_CREATE_NEW_PURCHASE_URL || "https://nocodb.dctrl.wtf/api/v1/db/data/v1/bepsi/purchases";

// Build pin to item mapping from environment variables
const VENDING_PINS = [516, 517, 518, 524, 525, 528];
const pinToItem = {};
VENDING_PINS.forEach(pin => {
  const envName = process.env[`PIN_${pin}_NAME`];
  if (envName) {
    pinToItem[pin] = envName;
  }
});

const getDispenseItemGivenPin = (pinNo) =>
  pinToItem[pinNo] || `unmarked-${pinNo}`;

const dispense = async (pinNo) => {
  // Can only dispense one at a time to avoid overloading
  if (isDispensing) return;
  isDispensing = true;

  try {
    const pin = new Gpio(pinNo, "out");
    pin.writeSync(0);
    await sleep(500);
    pin.writeSync(1);
    console.log(`Dispensed pin ${pinNo} successfully`);
  } catch (error) {
    console.log(error.message);
  }

  isDispensing = false;
};

// Wrapper around this so we can easily keep track of things io nocodedb
const dispenseFromDiscord = async (pinNo) => {
  await axios
    .post(
      NOCO_CREATE_NEW_PURCHASE_URL,
      {
        currency: "discord",
        timestamp: new Date().toISOString(),
        item: getDispenseItemGivenPin(pinNo),
      },
      {
        headers: {
          accept: "application/json",
          "xc-token": NOCODB_API_TOKEN,
          "Content-Type": "application/json",
        },
      },
    )
    .catch((e) =>
      console.log(`[dispenseFromDiscord] POST TO NOCODE DB FAILURE ${e}`),
    );

  dispense(pinNo);
};

const dispenseFromPayments = async (pinNo, currency) => {
  // Only handle physical dispensing - logging is done by logPayment()
  console.log("Dispensing pin " + pinNo);
  dispense(pinNo);
};

const logPayment = async (pinNo, currency, amount = null, paymentMethod = "unknown") => {
  await axios
    .post(
      NOCO_CREATE_NEW_PURCHASE_URL,
      {
        currency,
        amount: amount,
        payment_method: paymentMethod,
        timestamp: new Date().toISOString(),
        item: getDispenseItemGivenPin(pinNo),
      },
      {
        headers: {
          accept: "application/json",
          "xc-token": NOCODB_API_TOKEN,
          "Content-Type": "application/json",
        },
      },
    )
    .catch((e) =>
      console.log(`[logPayment] POST TO NOCODE DB FAILURE ${e}`),
    );
};

// Right to left, pins
// [4, 5, 6, 12, 13, 16, 9]

module.exports = {
  dispense,
  dispenseFromDiscord,
  dispenseFromPayments,
  logPayment,
};
