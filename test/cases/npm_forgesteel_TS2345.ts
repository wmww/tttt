// origin: npm forgesteel@14.217.0 https://github.com/andyaiken/forgesteel tsconfig.bench.json seed 20261301
// flip: yes
// @strict: true
// @target: es2024
// @noEmit: true
interface Element { description: string }
interface Title extends Element { }
declare function useState<S>(initial: S | (() => S)): [S];
declare function useParams<T>(): Partial<T>;
declare const titles: Title[];
type K = 'a' | 'b' | 'title';
const { kind } = useParams<{ kind: K }>();
const [ element ] = useState<Element>(() => {
	switch (kind!) {
		case 'a':
		case 'b':
		case 'title':
			return titles.find(e => e.description === 'x')!;
	}
});
