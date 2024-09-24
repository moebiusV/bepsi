# Bepsi-pi

Code that lives in the bepsi vending machine that does two things:

- Listens to MATIC/DAI/USDC/USDT payments to `PAYMENT_ADDRESS`, on payment dispenses bepsi
- Listens to discord emoji reaction, on reaction dispenses bubbly

```bash
yarn
yarn start

# Start
npm install -g pm2
pm2 start index.js --name bepsi-pi
```
