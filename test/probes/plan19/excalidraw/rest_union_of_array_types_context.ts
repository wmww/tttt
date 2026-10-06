// @strict: true
type Subscriber<T extends any[]> = (...payload: T) => void;
declare class Emitter<T extends any[] = []> {
  on(...handlers: Subscriber<T>[] | Subscriber<T>[][]): () => void;
}
function f<TEvent>(emitter: Emitter<[TEvent]>) {
  emitter.on((event) => {});
}
declare const e: Emitter<[number]>;
e.on((event) => {});
