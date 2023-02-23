const { Gpio } = require('onoff');
const { sleep } = require('./common');
const { NOCODB_API_TOKEN } = require('./env');

let isDispensing = false;

// Wrapper around this so we can easily keep track of things io nocodedb
const dispenseFromDiscord = async (pinNo) => {

};

const dispenseFromPayments = async (pinNo, currency) => {

};

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

// Right to left, pins
// [4, 5, 6, 12, 13, 16, 9]

module.exports = {
  dispense,
  dispenseFromDiscord,
};
