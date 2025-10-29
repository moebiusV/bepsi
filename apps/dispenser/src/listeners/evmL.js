const { ethers } = require("ethers");
const abiDecoder = require("abi-decoder");
const { dispenseFromPayments, logPayment } = require("../machine");
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
  const formattedAmount = ethers.utils.formatUnits(amount, stablecoin.decimals);
  const minAmount = ethers.utils.parseUnits("1", stablecoin.decimals);

  if (ethers.BigNumber.from(amount).lt(minAmount)) {
    console.log(
      `[EVM] Insufficient amount: ${formattedAmount} ${stablecoin.symbol} from ${from} on ${network.name}`,
    );
    return;
  }

  const selection = parseInt(amount.toString().slice(-1), 10);
  let pin = VENDOR_SELECTION_TO_PIN_MAPPING[selection];

  if (Number.isNaN(pin) || pin === undefined) {
    pin = randomPin();
  }

  console.log(
    `[EVM] Payment received: ${formattedAmount} ${stablecoin.symbol} on ${network.name}, dispensing pin ${pin}`,
  );

  try {
    const numericAmount = parseFloat(formattedAmount);
    logPayment(pin, stablecoin.symbol, numericAmount, "evm");
    await dispenseFromPayments(pin, stablecoin.symbol);
  } catch (error) {
    console.error(`[EVM] Dispense failed for pin ${pin}:`, error.message);
  }
};

const startEvmListener = () => {
  console.log("[EVM] Starting EVM payment listener");
  console.log(`[EVM] Watching for payments to: ${PAYMENT_ADDRESS}`);

  const evmNetworks = Object.values(NETWORKS).filter(({ implementation }) => implementation === "EVM");

  evmNetworks.forEach((network) => {
    let provider;
    try {
      provider = new ethers.providers.JsonRpcProvider(network.rpc);
    } catch (error) {
      console.error(`[EVM] Failed to connect to ${network.name}:`, error.message);
      return;
    }

    for (i in network.stablecoins) {
      const stablecoin = network.stablecoins[i];

      let contract;
      try {
        contract = new ethers.Contract(
          stablecoin.address,
          tokenAbi,
          provider
        );
      } catch (error) {
        console.error(`[EVM] Failed to create contract for ${stablecoin.symbol}:`, error.message);
        continue;
      }

      console.log(`[EVM] Watching ${stablecoin.symbol} on ${network.name}`);

      const filter = contract.filters.Transfer(null, PAYMENT_ADDRESS, null);
      contract.on(filter, (from, to, amount, event) => {
        handleStablecoinPayments(from, amount, network, stablecoin);
      });

      contract.on("error", (error) => {
        console.error(`[EVM] Contract error for ${stablecoin.symbol} on ${network.name}:`, error.message);
      });
    }
  });
};

module.exports = {
  startEvmListener,
};
