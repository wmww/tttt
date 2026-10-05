// @strictFunctionTypes: true
declare function immediate<TArgs extends any[]>(task: (...args: TArgs) => void, ...args: TArgs): void;
immediate(() => {});
immediate(arg1 => {});
immediate((a, b) => {}, 1, "x");
