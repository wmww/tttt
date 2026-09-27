// Omit<X, K> of a generic X has the properties of X's constraint minus K (tsc's resolveMappedTypeMembers over the
// lower bound of the key type Exclude<keyof X, K>), and X's base constraint goes through `T["k"] & {}`
// (valibot's expectSchemaIssue). tsc prints the spread's intersection as { requirement: undefined; } & Omit<...> & {...}
interface Issue { readonly kind: string; readonly input: unknown; readonly received: string; readonly requirement?: unknown | undefined; }
interface Schema<TIssue extends Issue> { readonly '~types'?: { readonly issue: TIssue } | undefined }
type InferIssue<T extends Schema<Issue>> = NonNullable<T['~types']>['issue'];
function f<TSchema extends Schema<Issue>>(base: Omit<InferIssue<TSchema>, 'input' | 'received'>, value: unknown) {
  const x = { requirement: undefined, ...base, input: value, received: 'x' };
  const y: InferIssue<TSchema> = x;
  const z: InferIssue<TSchema> = { requirement: undefined, ...base, input: value, received: 'x' };
}
function g<TSchema extends Schema<Issue>>(w: Omit<InferIssue<TSchema>, 'input' | 'received'> & { input: unknown; received: string }) {
  const i: Issue = w;
  const y: InferIssue<TSchema> = w;
  const k: string = w.kind;
}
