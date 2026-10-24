// origin: npm @ensnode/ensnode-sdk@1.16.0 https://github.com/namehash/ensnode packages/ensnode-sdk/tsconfig.bench.json seed 20261110
// flip: yes
// @strict: true
// @target: esnext
interface PriceEth { currency: "ETH"; amount: bigint }
interface PriceDai { currency: "DAI"; amount: bigint }
type Price = PriceEth | PriceDai;

export function addPrices<const PriceType extends Price = Price>(
  ...prices: [PriceType, PriceType, ...PriceType[]]
): PriceType {
  const firstPrice = prices[0];
  const { currency } = firstPrice;
  return prices.reduce(
    (acc, price) => ({
      amount: acc.amount + price.amount,
      currency,
    }),
    {
      amount: 0n,
      currency: firstPrice.currency,
    },
  ) as PriceType;
}
