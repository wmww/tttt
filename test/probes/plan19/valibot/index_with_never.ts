// Indexing with never is never, whatever the object (valibot's record: `input[k as keyof typeof input]` with
// input: object)
function f(input: unknown, o: { a: number }, n: never) {
  if (input && typeof input === 'object') {
    for (const k in input) {
      const v: unknown = input[k as keyof typeof input];
    }
  }
  const x = o[n];
  const y = [1, 2][n];
}
