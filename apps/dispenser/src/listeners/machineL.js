const { Gpio } = require("onoff");
const axios = require("axios");
const { updateInventoryMessage } = require("./discordL.js");
const { sleep } = require("../common.js");

// GPIO 8 is wonky atm
// 17, 18, 19, 20, 21
// 0 means empty, 1 is full
const pinToHopper = {
  // 4: 1,
  529: 2,
  530: 3,
  531: 4,
  532: 5,
  533: 6,
};

const listenToGpio = (pinNo) => {
  let pin;
  try {
    pin = new Gpio(pinNo, "out");
  } catch (e) {
    console.log(
      `[listenToGpio] Running on non pi device for pin ${pinNo}, simulating...`,
    );
    return;
  }

  // Assume full by default
  // Once the thing triggers, only 6 remaining
  let prevValue = 1;
  setInterval(
    () =>
      pin.read(async (err, value) => {
        if (value !== prevValue) {
          // New value, send to the database
          prevValue = value;
          updateInventoryMessage(pinToHopper[pin], value === 0);
        }
      }),
    1000, // Every second
  );
};

const startMachineChecker = async () => {
  Object.keys(pinToHopper).forEach((pin) => {
    listenToGpio(pin);
  });
};

module.exports = {
  startMachineChecker,
};
