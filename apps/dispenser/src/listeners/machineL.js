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
  const hopperNum = pinToHopper[pinNo];
  let pin;
  try {
    pin = new Gpio(pinNo, "out");
    console.log(`[Hopper] Started listener for hopper ${hopperNum} (GPIO ${pinNo})`);
  } catch (e) {
    console.log(
      `[Hopper] Running on non pi device for pin ${pinNo}, simulating...`,
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
          const isLow = value === 0;
          console.log(`[Hopper] Hopper ${hopperNum} state changed: ${isLow ? 'LOW (less than 6 remaining)' : 'FULL'}`);
          prevValue = value;
          updateInventoryMessage(hopperNum, isLow);
        }
      }),
    1000, // Every second
  );
};

const startMachineChecker = async () => {
  console.log(`[Hopper] Starting hopper detection for ${Object.keys(pinToHopper).length} hoppers...`);
  Object.keys(pinToHopper).forEach((pin) => {
    listenToGpio(pin);
  });
};

module.exports = {
  startMachineChecker,
};
