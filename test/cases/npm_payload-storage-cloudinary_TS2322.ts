// origin: npm payload-storage-cloudinary@1.3.0 https://github.com/nick-vogel/payload-storage-cloudinary tsconfig.bench.json seed 20261114
// flip: yes
// @strict: true
type SelectIncludeType = { [k: string]: SelectIncludeType | true };
type SelectExcludeType = { [k: string]: false | SelectExcludeType };
type SelectType = SelectExcludeType | SelectIncludeType;
interface Coll { slug: string; forceSelect?: SelectType }
declare function pluginReadSelect(): Record<string, true>;
declare const colls: Coll[];
export const out: Coll[] = colls.map((collection) => ({
  ...collection,
  forceSelect: { ...collection.forceSelect, ...pluginReadSelect() },
}));
