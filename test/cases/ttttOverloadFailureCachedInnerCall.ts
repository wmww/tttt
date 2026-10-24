// A call failing on its only argument, a call of a generic function returning a function: the fallback signature's
// inference (tsc's inferSignatureInstantiationForOverloadFailure, under SkipGenericFunctions) still infers from that
// argument, whose resolved signature getResolvedSignature has cached from the failed check
interface Subscribable<T> {
  subscribe(next?: (value: T) => void): void;
}
interface Observable<T> extends Subscribable<T> {
  pipe<A>(op1: (s: Observable<T>) => Observable<A>): Observable<A>;
}
declare function mergeMap<T, R>(project: (value: T) => Subscribable<R>): (s: Observable<T>) => Observable<R>;
declare const moves: Observable<Event>;
declare const downs: Observable<unknown>;
const drag = downs.pipe(mergeMap((md: MouseEvent) => moves));
drag.subscribe(e => e.timeStamp);
