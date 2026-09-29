// @strict: true
declare function take(a: number[]): void
export const o: Record<string, Function> = <any>{
  m() { take(this) },
}
export const p = {
  m() { take(this) },
} as any
