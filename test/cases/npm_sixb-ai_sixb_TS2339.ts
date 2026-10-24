// origin: npm sixb-ai/sixb@main https://github.com/sixb-ai/sixb packages/core/tsconfig.json seed 20261400
// flip: no
// @strict: true
// @noEmit: true
// @target: esnext
// TValue inferred from an async generator returned by an immediately invoked async function* inside an async arrow
// (`readValues`). tsgo infers no candidate from it (TValue falls back to its constraint DatasetWriteValue, so
// `item.account` is TS2339); tttt infers `{ value: string; account: string }` and reports nothing.
export interface DatasetWriteValue {}
export interface WriteDatasetInput<TValue extends DatasetWriteValue = DatasetWriteValue> {
  readValues(): Promise<Iterable<TValue> | AsyncIterable<TValue>>
  mapValidationError?(error: unknown, value: TValue, itemIndex: number): unknown
}
declare function writeDataset<TValue extends DatasetWriteValue>(input: WriteDatasetInput<TValue>): void
writeDataset({
  readValues: async () =>
    (async function* () {
      try {
        yield { value: "x", account: "A" }
      } finally {
      }
    })(),
  mapValidationError(error, item, index) {
    item.account
  },
})
