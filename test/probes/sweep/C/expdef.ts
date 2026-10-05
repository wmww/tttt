// @strict: true
declare function declare<R>(builder: (api: string) => R): R;
export default declare((api) => api.length);
