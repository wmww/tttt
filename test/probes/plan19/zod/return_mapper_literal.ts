interface Box<K> { v: K; }
declare function mk3<K>(entries: K[]): Box<K>;
declare function enc4(m: Box<"a" | "b">): void;
enc4(mk3(["b"]));
declare function mk4<K>(entries: K): Box<K>;
enc4(mk4("b"));
