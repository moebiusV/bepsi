const { startDiscordListener } = require('./src/listeners/discordL');
const { startPolygonListener } = require('./src/listeners/polygonL');
const { startMachineChecker } = require('./src/listeners/machineL');

const main = async () => {
  startDiscordListener();
  startPolygonListener();
  startMachineChecker();
};
main();
