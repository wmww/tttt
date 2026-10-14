// Of the property suggestions at the best distance, the first declared wins (tsgo's getSpellingSuggestion breaks ties
// with compareSymbols): an inherited member declared earlier beats the class's own
class X { propertyX: any; }
class A extends X { propertyA: number; }
declare const a: A;
a.propertyAX;
