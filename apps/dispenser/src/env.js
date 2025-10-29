require("dotenv").config();

// Inject datetime into console.log
const myLog = console.log;
console.log = (...args) => {
  myLog.apply(
    console,
    [`[${new Date().toISOString().substring(11, 23)}] -`].concat(...args),
  );
};

// Resilient environment variable checking
// Only warn about missing variables, don't crash the entire application

const SERVICE_ENV_VARS = {
  discord: ["DISCORD_TOKEN", "DISCORD_CHANNEL_ID"],
  evm: ["PAYMENT_ADDRESS"],
  solana: ["SOLANA_TREASURY_ADDRESS"],
  arkade: ["ARKADE_WS_URL"],
  lightning: ["LIGHTNING_LNBIT_URL"],
  spark: [], // Spark can work with just individual pin configs, no required globals
  database: ["NOCODB_API_TOKEN"]
};

// Track which services have missing variables
const missingByService = {};
const allMissingVars = [];

// Check each service's environment variables
for (const [service, vars] of Object.entries(SERVICE_ENV_VARS)) {
  const missing = vars.filter(envVar => !process.env[envVar]);
  if (missing.length > 0) {
    missingByService[service] = missing;
    allMissingVars.push(...missing);
  }
}

// Log missing variables by service (warnings, not fatal)
for (const [service, missing] of Object.entries(missingByService)) {
  console.log(`⚠️  ${service.toUpperCase()} service: Missing ENV_VARS ${missing.join(", ")}`);
}

// Only fatal error if NO payment system is configured
const hasAnyPaymentSystem = process.env.PAYMENT_ADDRESS ||
                           process.env.SOLANA_TREASURY_ADDRESS ||
                           process.env.ARKADE_WS_URL ||
                           process.env.LIGHTNING_LNBIT_URL ||
                           process.env.SPARK_PAYMENT_AMOUNT; // Spark can work with pin configs

if (!hasAnyPaymentSystem) {
  console.error("❌ FATAL: No payment system configured. Need at least one of: PAYMENT_ADDRESS, SOLANA_TREASURY_ADDRESS, LIGHTNING_LNBIT_URL, or SPARK_PAYMENT_AMOUNT");
  process.exit(1);
}

if (allMissingVars.length > 0) {
  console.log(`ℹ️  Application will start with reduced functionality. Missing services will be disabled.`);
}

// Helper function to check if a service should be enabled
const isServiceEnabled = (serviceName) => {
  // Check if explicitly disabled
  const disableEnvVar = `DISABLE_${serviceName.toUpperCase()}`;
  if (process.env[disableEnvVar] === 'true') {
    return false;
  }

  // Check if required environment variables are present
  const requiredVars = SERVICE_ENV_VARS[serviceName.toLowerCase()];
  if (!requiredVars) return true; // Unknown service, let it try

  return !requiredVars.some(envVar => !process.env[envVar]);
};

const {
  DISCORD_TOKEN,
  DISCORD_CHANNEL_ID,
  NOCODB_API_TOKEN,
  PAYMENT_ADDRESS,
  ARKADE_WS_URL,
  LIGHTNING_LNBIT_URL,
  SOLANA_TREASURY_ADDRESS,
  SPARK_PAYMENT_AMOUNT,
} = process.env;

// Only log payment address if EVM service is configured
if (PAYMENT_ADDRESS) {
  console.log(`Listening to payments at address ${PAYMENT_ADDRESS}`);
}

module.exports = {
  DISCORD_TOKEN,
  DISCORD_CHANNEL_ID,
  NOCODB_API_TOKEN,
  PAYMENT_ADDRESS,
  ARKADE_WS_URL,
  LIGHTNING_LNBIT_URL,
  SOLANA_TREASURY_ADDRESS,
  SPARK_PAYMENT_AMOUNT,
  isServiceEnabled,
  missingByService
};
