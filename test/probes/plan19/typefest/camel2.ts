// @strict: true
type Whitespace = ' ' | '\t';
type WordSeparators = '-' | '_' | Whitespace;
type TrimLeft<V extends string> = V extends `${Whitespace}${infer R}` ? TrimLeft<R> : V;
type TrimRight<V extends string> = V extends `${infer R}${Whitespace}` ? TrimRight<R> : V;
type Trim<V extends string> = TrimLeft<TrimRight<V>>;
type IsLowerCase<T extends string> = T extends Lowercase<T> ? true : false;
type IsUpperCase<T extends string> = T extends Uppercase<T> ? true : false;
type IsNumeric<T extends string> = T extends `${number}` ? Trim<T> extends T ? true : false : false;
type SkipEmptyWord<Word extends string> = Word extends '' ? [] : [Word];
type RemoveLastCharacter<Sentence extends string, Character extends string> = Sentence extends `${infer LeftSide}${Character}` ? SkipEmptyWord<LeftSide> : never;
type SplitWords<Sentence extends string, LastCharacter extends string = '', CurrentWord extends string = ''> = Sentence extends `${infer FirstCharacter}${infer RemainingCharacters}`
	? FirstCharacter extends WordSeparators
		? [...SkipEmptyWord<CurrentWord>, ...SplitWords<RemainingCharacters>]
		: LastCharacter extends ''
			? SplitWords<RemainingCharacters, FirstCharacter, FirstCharacter>
			: [false, true] extends [IsNumeric<LastCharacter>, IsNumeric<FirstCharacter>]
				? [...SkipEmptyWord<CurrentWord>, ...SplitWords<RemainingCharacters, FirstCharacter, FirstCharacter>]
				: [true, false] extends [IsNumeric<LastCharacter>, IsNumeric<FirstCharacter>]
					? [...SkipEmptyWord<CurrentWord>, ...SplitWords<RemainingCharacters, FirstCharacter, FirstCharacter>]
					: [true, true] extends [IsNumeric<LastCharacter>, IsNumeric<FirstCharacter>]
						? SplitWords<RemainingCharacters, FirstCharacter, `${CurrentWord}${FirstCharacter}`>
						: [true, true] extends [IsLowerCase<LastCharacter>, IsUpperCase<FirstCharacter>]
							? [...SkipEmptyWord<CurrentWord>, ...SplitWords<RemainingCharacters, FirstCharacter, FirstCharacter>]
							: [true, true] extends [IsUpperCase<LastCharacter>, IsLowerCase<FirstCharacter>]
								? [...RemoveLastCharacter<CurrentWord, LastCharacter>, ...SplitWords<RemainingCharacters, FirstCharacter, `${LastCharacter}${FirstCharacter}`>]
								: SplitWords<RemainingCharacters, FirstCharacter, `${CurrentWord}${FirstCharacter}`>
	: [...SkipEmptyWord<CurrentWord>];
type CamelCaseOptions = { preserveConsecutiveUppercase?: boolean };
type CamelCaseFromArray<Words extends string[], Options extends CamelCaseOptions, OutputString extends string = ''> = Words extends [infer FirstWord extends string, ...infer RemainingWords extends string[]]
	? Options['preserveConsecutiveUppercase'] extends true
		? `${Capitalize<FirstWord>}${CamelCaseFromArray<RemainingWords, Options>}`
		: `${Capitalize<Lowercase<FirstWord>>}${CamelCaseFromArray<RemainingWords, Options>}`
	: OutputString;
type CamelCase<Type, Options extends CamelCaseOptions = {preserveConsecutiveUppercase: true}> = Type extends string
	? string extends Type ? Type : Uncapitalize<CamelCaseFromArray<SplitWords<Type extends Uppercase<Type> ? Lowercase<Type> : Type>, Options>>
	: Type;
declare function expectType<T>(x: T): void;
const camelFromMixed: CamelCase<'foo-bar_abc xyzBarFoo'> = 'fooBarAbcXyzBarFoo';
const camelFromVendorPrefixedCssProperty: CamelCase<'-webkit-animation'> = 'webkitAnimation';
const camelFromDoublePrefixedKebab: CamelCase<'--very-prefixed'> = 'veryPrefixed';
expectType<CamelCase<'foo BAR-Biz_BUZZ'>>('fooBARBizBUZZ');
expectType<CamelCase<'foo BAR-Biz_BUZZ', {preserveConsecutiveUppercase: false}>>('fooBarBizBuzz');
