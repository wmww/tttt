// @strict: true
function f(this: { readonly a: number }) { this.a = 1 }
const o: { m(): void } & ThisType<{ readonly a: number }> = {
  m() { const t = this; t.a = 1; this.a = 1 },
}
declare function def<T>(o: { props: T; render?(): void } & ThisType<Readonly<T>>): void
def({
  props: { a: 1 },
  render() {
    this.a = 1
  },
})
const p = { a: 1, m() { this.a = 2 } } as const
