const { startDiscordListener } = require('./src/listeners/discordL');
const { startPolygonListener } = require('./src/listeners/polygonL');
const { startMachineChecker } = require('./src/listeners/machineL');

const { startLightningListener } = require('./src/listeners/lightningL');

const main = async () => {
  startDiscordListener();
  startPolygonListener();
  startLightningListener();
  startMachineChecker();
};
main();
