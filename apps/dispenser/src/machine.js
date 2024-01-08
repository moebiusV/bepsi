const { Gpio } = require('onoff');
const axios = require('axios');
const { sleep, nowTimestamp } = require('./common');
const { NOCODB_API_TOKEN } = require('./env');

let isDispensing = false;

const NOCO_CREATE_NEW_PURCHASE_URL = 'https://nocodb.dctrl.wtf/api/v1/db/data/v1/bepsi/purchases';

const pinToItem = {
  4: 'lime',
  5: 'strawberry',
  6: 'grapefruit',
  12: 'cherry',
  13: 'purple',
  16: 'orange',
};

const getDispenseItemGivenPin = (pinNo) => pinToItem[pinNo] || `unmarked-${pinNo}`;

const dispense = async (pinNo) => {
  // Can only dispense one at a time to avoid overloading
  if (isDispensing) return;
  isDispensing = true;

  try {
    const pin = new Gpio(pinNo, 'out');
    pin.writeSync(0);
    await sleep(1000);
    pin.writeSync(1);
    console.log(`dispense ${pinNo} successful`);
  } catch (e) {
    console.log('Unable to find GPIO pins, running in simulation..');
  }

  isDispensing = false;
};

// Wrapper around this so we can easily keep track of things io nocodedb
const dispenseFromDiscord = async (pinNo) => {
  await axios.post(
    NOCO_CREATE_NEW_PURCHASE_URL,
    {
      currency: 'discord',
      timestamp: nowTimestamp(),
      item: getDispenseItemGivenPin(pinNo),
    },
    {
      headers: {
        accept: 'application/json',
        'xc-token': NOCODB_API_TOKEN,
        'Content-Type': 'application/json',
      },
    },
  ).catch((e) => console.log(`[dispenseFromDiscord] POST TO NOCODE DB FAILURE ${e}`));

  dispense(pinNo);
};

const dispenseFromPayments = async (pinNo, currency) => {
  await axios.post(
    NOCO_CREATE_NEW_PURCHASE_URL,
    {
      currency,
      timestamp: nowTimestamp(),
      item: getDispenseItemGivenPin(pinNo),
    },
    {
      headers: {
        accept: 'application/json',
        'xc-token': NOCODB_API_TOKEN,
        'Content-Type': 'application/json',
      },
    },
  ).catch((e) => console.log(`[dispenseFromDiscord] POST TO NOCODE DB FAILURE ${e}`));
  console.log('dispensing ' + pinNo);
  dispense(pinNo);
};

// Right to left, pins
// [4, 5, 6, 12, 13, 16, 9]

module.exports = {
  dispense,
  dispenseFromDiscord,
  dispenseFromPayments,
};
