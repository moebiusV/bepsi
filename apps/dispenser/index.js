const { startDiscordListener } = require('./src/listeners/discordL');
const { startPolygonListener } = require('./src/listeners/polygonL');

const main = async () => {
  startDiscordListener();
  startPolygonListener();
};
main();
