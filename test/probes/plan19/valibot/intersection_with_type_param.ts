interface S1<T> { readonly kind: 'schema'; readonly type: string; readonly reference: (...args: any[]) => S1<unknown>; readonly expects: string }
interface V1<T> { readonly kind: 'validation'; readonly type: string; readonly reference: (...args: any[]) => V1<any>; readonly expects: string | null }
type Context = S1<unknown> | V1<any>;
function addIssue<const TContext extends Context>(
  context: TContext & { expects?: string | null; message?: string },
): void {
  const k = context.kind;
  const t = context.type;
  const r = context.reference;
  const e = context.expects;
}
function plain<TContext extends Context>(context: TContext & { extra?: number }) {
  const k = context.kind;
}
