const NETWORKS = {
  polygon: {
    name: "Polygon",
    rpc: "https://polygon.llamarpc.com",
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
    name: "Base",
    rpc: "https://base.llamarpc.com",
    stablecoins: [
      {
        symbol: "USDC",
        decimals: 6,
        address: "0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913",
      },
    ],
  },
  arbitrum: {
    name: "Arbitrum",
    rpc: "https://arbitrum.llamarpc.com",
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
    name: "Optimism",
    rpc: "https://optimism.llamarpc.com",
    stablecoins: [
      {
        symbol: "USDC",
        decimals: 6,
        address: "0x0b2C639c533813f4Aa9D7837CAf62653d097Ff85",
      },
    ],
  },
};

// Selection to pin
// [4, 5, 6, 12, 13, 16, 9]
const VENDOR_SELECTION_TO_PIN_MAPPING = {
  1: 4,
  2: 5,
  3: 6,
  4: 12,
  5: 13,
  6: 16,
};

module.exports = {
  NETWORKS,
  VENDOR_SELECTION_TO_PIN_MAPPING,
};
