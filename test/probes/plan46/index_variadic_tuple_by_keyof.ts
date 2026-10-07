type A<E extends readonly unknown[]> = { [P in keyof [...E, "abc"]]: [...E, "abc"][P] };
type B<E extends unknown[]> = { [P in keyof [...E, "abc"]]: [...E, "abc"][P] };
type C<E extends unknown[]> = { [P in keyof [...E]]: [...E][P] };
type D<E extends unknown[], K extends keyof [...E, 1]> = [...E, 1][K];
