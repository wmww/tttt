// @strict: true
// @noEmit: true
// A spread of a mapped type (Partial<R>): its properties are declared where R's are, which orders them (tsc sorts by first
// declaration), and reuse R's annotation only where it says the same with each one's optionality (gemini-cli's makeCase)
interface R { s?: string; f: string; n: number; }
declare const p: Partial<R>;
const x: number = { f: 'a', ...p };
const y: number = { ...p };
const z: number = { q: 1, ...p };
function mk(o: Partial<R> = {}): number {
  return { f: 'a', n: 1, ...o };
}
function mk2(o: { s?: string; f?: string }): number {
  return { f: 'a', n: 1, ...o };
}
