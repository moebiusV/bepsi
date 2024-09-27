const { ethers } = require("ethers");
const abiDecoder = require("abi-decoder");
const { dispenseFromPayments } = require("../machine");
const { VENDOR_SELECTION_TO_PIN_MAPPING, NETWORKS } = require("../constants");
const {
  match,
  contains,
  isPaymentValid,
  getStablecoinName,
  parseStablecoin,
  randomPin,
} = require("../common");
const { PAYMENT_ADDRESS } = require("../env");

const tokenAbi = require("../abi/erc20.json");
abiDecoder.addABI(tokenAbi);

const handleStablecoinPayments = async (from, amount, network, stablecoin) => {
  if (
    ethers.BigNumber.from(amount).lt(
      ethers.utils.parseUnits("1", stablecoin.decimals),
    )
  ) {
    console.log(
      `Insufficient Transfer Amount from ${from} on ${network.name} (${ethers.utils.parseUnits(amount, 6)} ${stablecoin.symbol}})`,
    );
    return;
  }

  const selection = parseInt(amount.toString().slice(-1), 16);
  let pin = VENDOR_SELECTION_TO_PIN_MAPPING[selection];
  if (Number.isNaN(pin) || pin === undefined) {
    pin = randomPin();
  }

  console.log(
    `payment received ${ethers.utils.formatUnits(
      amount,
      stablecoin.decimals,
    )} ${stablecoin.symbol}, network: ${network.name} selection: ${
      selection
    }, pin ${pin}, dispensing...`,
  );

  await dispenseFromPayments(pin, stablecoin.symbol);
};

const startEvmListener = () => {
  Object.values(NETWORKS)
    .filter(({ implementation }) => implementation === "EVM")
    .forEach((network) => {
      provider = new ethers.providers.JsonRpcProvider(network.rpc);

      for (i in network.stablecoins) {
        const stablecoin = network.stablecoins[i];
        const contract = new ethers.Contract(
          stablecoin.address,
          tokenAbi,
          provider
        );
        console.log(
          `Watching ${stablecoin.symbol} on ${network.name} (${stablecoin.address})`
        );
        contract.on(
          contract.filters.Transfer(null, PAYMENT_ADDRESS, null),
          (from, to, amount) => {
            handleStablecoinPayments(from, amount, network, stablecoin);
          }
        );
      }
    });
};

module.exports = {
  startEvmListener,
};
