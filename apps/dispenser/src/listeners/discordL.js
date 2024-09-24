const { Client, GatewayIntentBits, Collection } = require("discord.js");
const { DISCORD_CHANNEL_ID, DISCORD_TOKEN } = require("../env");
const { dispenseFromDiscord } = require("../machine");

const client = new Client({
  partials: ["MESSAGE", "CHANNEL", "REACTION"],
  intents: [
    GatewayIntentBits.Guilds,
    GatewayIntentBits.GuildMessages,
    GatewayIntentBits.GuildMessageReactions,
    GatewayIntentBits.MessageContent,
  ],
});

client.commands = new Collection();

async function main() {
  console.log("Discord Bepsi listener is ready!");
  console.log(`[Discord] Logged in as ${client.user.tag}!`);

  const channel = await client.channels.fetch(DISCORD_CHANNEL_ID);
  const message = await channel.send("Bitpepsi for all!");

  // Green, red, pink, cherry, purple, orange
  const supportedEmojis = ["🟢", "🔴", "🌸", "🍒", "🟣", "🟠"];
  const emojiToPin = {
    "🟢": 4,
    "🔴": 5,
    "🌸": 6,
    "🍒": 12,
    "🟣": 13,
    "🟠": 16,
  };

  message.react("🟢");
  message.react("🔴");
  message.react("🌸");
  message.react("🍒");
  message.react("🟣");
  message.react("🟠");

  client.on("messageReactionAdd", async (reaction, user) => {
    // Ignore bot's own reaction
    if (user.id === client.user.id) {
      return;
    }

    // Disreacts it
    reaction.users.remove(user);

    // If the reaction was on the submitted message
    if (reaction.message.id !== message.id) {
      return;
    }

    // Make sure the reaction is one of the following
    // eslint-disable-next-line
    if (!supportedEmojis.includes(reaction._emoji.name)) {
      return;
    }

    // eslint-disable-next-line
    const pin = emojiToPin[reaction._emoji.name];
    if (pin === undefined || pin === null) {
      return;
    }

    dispenseFromDiscord(pin);
  });
}

const startDiscordListener = () => {
  client.on("ready", main);
  client.login(DISCORD_TOKEN);
};

module.exports = {
  startDiscordListener,
};
