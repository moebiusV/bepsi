const NETWORKS = {
  polygon: {
    implementation: "EVM",
    name: "Polygon",
    rpc: "https://polygon-rpc.com/",
    stablecoins: [
      {
        symbol: "USDC",
        decimals: 6,
        address: "0x3c499c542cEF5E3811e1192ce70d8cC03d5c3359",
      },
      {
        symbol: "USDC.e",
        decimals: 6,
        address: "0x2791bca1f2de4661ed88a30c99a7a9449aa84174",
      },
    ],
  },
  base: {
    implementation: "EVM",
    name: "Base",
    rpc: "https://mainnet.base.org",
    stablecoins: [
      {
        symbol: "USDC",
        decimals: 6,
        address: "0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913",
      },
    ],
  },
  arbitrum: {
    implementation: "EVM",
    name: "Arbitrum",
    rpc: "https://arb1.arbitrum.io/rpc",
    stablecoins: [
      {
        symbol: "USDC.e",
        decimals: 6,
        address: "0xFF970A61A04b1cA14834A43f5dE4533eBDDB5CC8",
      },
      {
        symbol: "USDC",
        decimals: 6,
        address: "0xaf88d065e77c8cC2239327C5EDb3A432268e5831",
      },
    ],
  },
  optimism: {
    implementation: "EVM",
    name: "Optimism",
    rpc: "https://rpc.ankr.com/optimism",
    stablecoins: [
      {
        symbol: "USDC",
        decimals: 6,
        address: "0x0b2C639c533813f4Aa9D7837CAf62653d097Ff85",
      },
    ],
  },
  hyperevm: {
    implementation: "HYPEREVM",
    name: "Hyperliquid",
    rpc: "https://rpc.hyperliquid.xyz/evm",
    stablecoins: [
      {
        symbol: "USDC",
        decimals: 6,
        address: "0xb88339CB7199b77E23DB6E890353E22632Ba630f",
      },
      {
        symbol: "USDT0",
        decimals: 6,
        address: "0xB8CE59FC3717ada4C02eaDF9682A9e934F625ebb",
      },
      {
        symbol: "BESPI",
        decimals: 18,
        address: "0xDF400dFcd64590703C7A647141e1a30BE31F8888",
      },
    ],
  },
  solana: {
    implementation: "SVM",
    name: "Solana",
    rpc: "https://api.mainnet-beta.solana.com",
    rpcSubscriptions: "wss://api.mainnet-beta.solana.com",
    stablecoins: [
      {
        symbol: "USDC",
        decimals: 6,
        address: "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v",
      },
    ],
  },
};

// Selection to pin
// [4, 5, 6, 12, 13, 16, 9]
const VENDOR_SELECTION_TO_PIN_MAPPING = {
  1: 516,
  2: 517,
  3: 518,
  4: 524,
  5: 525,
  6: 528,
};

module.exports = {
  NETWORKS,
  VENDOR_SELECTION_TO_PIN_MAPPING,
};
