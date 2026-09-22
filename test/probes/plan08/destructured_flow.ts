// @strict: true
interface State { cache?: { read: () => string }; host: { x: number } }
function f(state: State) {
    if (!state.cache) return;
    const { cache, host } = state;
    cache.read();
}
