// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2020"], "skipLibCheck": true } }
// @filename: a.ts
const id = <T>(x: T) => x;
const nest = <T>(x: T) => id({ nested: { tag: 0, ...x } });
const eight = <T>(x: T) => nest(nest(nest(nest(nest(nest(nest(nest(x))))))));
const forty = eight(eight(eight(eight(eight({ value: 0 })))));
declare function consume<T>(x: typeof forty): T;
consume(forty);
consume<number>(forty);
const ninety = eight(eight(eight(eight(eight(eight(eight(eight(eight(eight(eight(nest(nest({ value: 0 })))))))))))));
declare function consumeNinety<T>(x: typeof ninety): T;
consumeNinety(ninety);
const flat = <T>(x: T) => id({ tag: 0, ...x });
const flatEight = <T>(x: T) => flat(flat(flat(flat(flat(flat(flat(flat(x))))))));
const fortyFlat = flatEight(flatEight(flatEight(flatEight(flatEight({ value: 0 })))));
declare function consumeFlat<T>(x: typeof fortyFlat): T;
consumeFlat(fortyFlat);
const wrong: { nested: { tag: string } } = nest({ value: 0 });
const sentinel: number = "";
export {};
