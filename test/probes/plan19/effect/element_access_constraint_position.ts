// @strict: true
type Arbs = Array<number> | { [K in string]: number };
function k<const A extends Arbs>(a: A, key: string) {
	if (Array.isArray(a)) return;
	a[key];
	const h = (k2: string) => a[k2];
	const h2 = () => a[key];
}
