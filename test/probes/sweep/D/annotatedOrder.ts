// @strict: true
interface Config<S, T> {
    getCurrentValue: (source: S) => T;
    subscribe: (source: S, callback: (newValue: T) => void) => () => void;
}
declare function createSubscription<S, T>(config: Config<S, T>): [S, T];
const r = createSubscription({
    getCurrentValue: (a: number) => null,
    subscribe: (a: string, callback) => (() => undefined),
});
