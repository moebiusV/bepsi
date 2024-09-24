const { startDiscordListener } = require("./src/listeners/discordL");
const { startEvmListener } = require("./src/listeners/evmL");
const { startMachineChecker } = require("./src/listeners/machineL");

const { startLightningListener } = require("./src/listeners/lightningL");

const main = async () => {
  //startDiscordListener();
  startEvmListener();
  //startLightningListener();
  //startMachineChecker();
};
main();
