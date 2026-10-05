// @strict: true
declare function f(builder: (api: string) => number): number;
export default f((api) => api.length);
