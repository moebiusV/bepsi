const { Gpio } = require('onoff');
const axios = require('axios');
const { NOCODB_API_TOKEN } = require('../env');

// GPIO 8 is wonky atm
// Lime, .... -> orange
// 17, 18, 19, 20, 21
// 0 means empty, 1 is full
const pinToItem = {
  // 4: 'lime',
  17: 'strawberry',
  18: 'grapefruit',
  19: 'cherry',
  20: 'purple',
  21: 'orange',
};

const getItemNameFromPin = (pinNo) => pinToItem[pinNo] || `unmarked-${pinNo}`;

const getItemStatus = (i) => (i === 0 ? 'limited' : 'refill');

const NOCO_CREATE_NEW_MACHINE_STATE_URL = 'http://nocodb.dctrl.wtf/api/v1/db/data/v1/bepsi/machine_state';

const listenToGpio = (pinNo) => {
  let pin;
  try {
    pin = new Gpio(pinNo, 'out');
  } catch (e) {
    console.log(`[listenToGpio] Running on non pi device for pin ${pinNo}, simulating...`);
    return;
  }

  // Assume full by default
  // Once the thing triggers, only 6 remaining
  let prevValue = 1;
  setInterval(
    () => pin.read((err, value) => {
      if (value !== prevValue) {
        // New value, send to the database
        console.log(`${getItemNameFromPin(pinNo)} status changed to ${getItemStatus(value)}`);
        prevValue = value;

        //
        axios.post(
          NOCO_CREATE_NEW_MACHINE_STATE_URL,
          {
            item: getItemNameFromPin(pinNo),
            status: getItemStatus(value),
          },
          {
            headers: {
              accept: 'application/json',
              'xc-token': NOCODB_API_TOKEN,
              'Content-Type': 'application/json',
            },
          },
        ).catch((e) => console.log(`[listenToGpio] ${pinNo} POST TO NOCODE DB FAILURE ${e}`));
      }
    }),
    1000, // Every second
  );
};

const startMachineChecker = () => {
  listenToGpio(17);
  listenToGpio(18);
  listenToGpio(19);
  listenToGpio(20);
  listenToGpio(21);
};

module.exports = {
  startMachineChecker,
};
