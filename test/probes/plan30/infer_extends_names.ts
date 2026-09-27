type obj = { foo: 1; bar: 2; };
type K1<keys> = keys extends infer key extends keyof obj ? obj[key] : never;
type K2<str extends string, keys> = keys extends infer key extends keyof obj ? key extends `${string}${str}${string}` ? obj[key] : never : never;
type K3<keys> = keys extends infer key extends keyof obj ? key extends `${string}a${string}` ? obj[key] : never : never;
