const { ethers } = require("ethers");
const {
  assertIsAddress,
  createSolanaRpc,
  createSolanaRpcSubscriptions,
} = require("@solana/web3.js");
const {
  findAssociatedTokenPda,
  TOKEN_PROGRAM_ADDRESS,
} = require("@solana-program/token");

const { randomPin } = require("../common");
const {
  NETWORKS,
  VENDOR_SELECTION_TO_PIN_MAPPING,
} = require("../constants.js");
const { SOLANA_TREASURY_ADDRESS } = require("../env.js");
const { dispenseFromPayments } = require("../machine");

const ORDER_MEMO_REGEX = new RegExp(
  '^Program log: Memo \\(len \\d+\\): "YVR-BEPSI:0:(\\d+)"$'
  //                                   ^         ^  ^
  //                                   |         |  |
  //                                   |         |  vendor selection
  //                                   |         version
  //                                   discriminator
);

function getTokenAmountChange(expectedMintAddress, transactionMeta) {
  function balancePredicate({ mint, owner }) {
    return mint === expectedMintAddress && owner === SOLANA_TREASURY_ADDRESS;
  }
  const preBalance =
    transactionMeta.preTokenBalances.find(balancePredicate)?.uiTokenAmount
      .amount;
  const postBalance =
    transactionMeta.postTokenBalances.find(balancePredicate)?.uiTokenAmount
      .amount;
  if (preBalance == null || postBalance == null) {
    return;
  }
  return BigInt(postBalance) - BigInt(preBalance);
}

function startSolanaListener(abortSignal) {
  assertIsAddress(SOLANA_TREASURY_ADDRESS);
  Object.values(NETWORKS)
    .filter(({ implementation }) => implementation === "SVM")
    .forEach(async (network) => {
      for (const {
        address: mintAddress,
        decimals,
        symbol,
      } of network.stablecoins) {
        assertIsAddress(mintAddress);
        const minimumPayment = 1n * 10n ** BigInt(decimals);
        const rpc = createSolanaRpc(network.rpc);
        const rpcSubscriptions = createSolanaRpcSubscriptions(
          network.rpcSubscriptions
        );
        const [treasuryTokenAccountAddress] = await findAssociatedTokenPda({
          mint: mintAddress,
          owner: SOLANA_TREASURY_ADDRESS,
          tokenProgram: TOKEN_PROGRAM_ADDRESS,
        });
        const logs = await rpcSubscriptions
          .logsNotifications(
            { mentions: [treasuryTokenAccountAddress] },
            { commitment: "confirmed" }
          )
          .subscribe({ abortSignal });
        console.log(`Watching ${symbol} on ${network.name} (${mintAddress})`);
        for await (const log of logs) {
          try {
            const transaction = await rpc
              .getTransaction(log.value.signature, {
                commitment: "confirmed",
                maxSupportedTransactionVersion: 0,
              })
              .send({ abortSignal });
            const transferAmount = getTokenAmountChange(
              mintAddress,
              transaction.meta
            );
            if (transferAmount == null) {
              // This transaction did not change the stablecoin balance.
              continue;
            }
            if (transferAmount < minimumPayment) {
              // Transfer was not at least 1 stablecoin.
              console.log(
                `Insufficient Transfer Amount on ${
                  network.name
                } (${ethers.utils.formatUnits(
                  transferAmount,
                  decimals
                )} ${symbol}) https://explorer.solana.com/tx/${
                  transaction.transaction.signatures[0]
                }`
              );
              continue;
            }
            let vendorSelection;
            for (const logLine of log.value.logs) {
              const match = logLine.match(ORDER_MEMO_REGEX);
              if (match) {
                vendorSelection = parseInt(match[1], 10);
                break;
              }
            }
            let pin;
            if (
              (pin = VENDOR_SELECTION_TO_PIN_MAPPING[vendorSelection]) == null
            ) {
              pin = randomPin();
              console.warn(
                "Failed to discern vendor selection from logs for transaction " +
                  `https://explorer.solana.com/tx/${transaction.transaction.signatures[0]}. ` +
                  `Dispensing random product from pin ${pin}.`
              );
            }
            console.log(
              `payment received ${ethers.utils.formatUnits(
                transferAmount,
                decimals
              )} ${symbol}, network: ${
                network.name
              } selection: ${vendorSelection}, pin ${pin}, dispensing...`
            );
            await dispenseFromPayments(pin, symbol);
          } catch (e) {
            console.error(
              "Error parsing logs for transaction " +
                `https://explorer.solana.com/tx/${transaction.transaction.signatures[0]}`,
              e
            );
          }
        }
      }
    });
}

module.exports = {
  startSolanaListener,
};
