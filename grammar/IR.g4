grammar IR;

/****** lexer ******/

// Keywords
FN: 'fn';
LET: 'let';
I1: 'bool';
I32: 'i32';
INT: 'int';
F32: 'f32';
F64: 'f64';
FLOAT: 'float';
CONST: 'const';
MUT: 'mut';
REF: 'ref';
TRUE: 'true';
FALSE: 'false';
RETURN: 'return';
JUMP: '=>';
IF: 'if';
ELSE: 'else';
PHI: 'phi';

ID: [a-zA-Z_][a-zA-Z0-9_]*;

fragment DIGIT: [0-9];

// Literals

INT_LITERAL: DIGIT+;

fragment FLOAT_NUMBER:
	DIGIT+ '.' DIGIT* ([eE] [+-]? DIGIT+)?
	| DIGIT+ [eE] [+-]? DIGIT+;

FLOAT_LITERAL: FLOAT_NUMBER [fF];
DOUBLE_LITERAL: FLOAT_NUMBER;

// Whitespace
WS: [ \t\r\n]+ -> skip;

/****** parser ******/

program: (constDecl | letDecl | funcDecl)* EOF;

constDecl: CONST REF? ID ':' type '=' constexpr ';';
letDecl: LET REF? MUT? ID ':' type ('=' constexpr)? ';';

funcDecl:
	FN ID '(' paramList? ')' ('->' type)? '{' (
		constDecl
		| letDecl
	)* block* '}';

paramList: param (',' param)*;
param: MUT? ID ':' type;

temp: '%' INT_LITERAL;
ssa: '%' ID '.' INT_LITERAL;
name: '@' ID;
var: temp | ssa | name;
value: var | constexpr;
label: '\'' ID;

exit:
	RETURN value? ';'										# returnExit
	| JUMP IF value '{' label '}' ELSE '{' label '}' ';'	# branchExit
	| JUMP label ';'										# jumpExit;

block: label ':' '{' phiInst* inst* exit '}';

def: var ':' type '=' ;

inst
	: def? name '(' (argList)? ')' ';'							# callInst
	| def '*' '(' type ')' var ';'								# loadInst
	| def '&' MUT? var ';'										# borrowInst
	| def value '[' value ']' ';'								# loadElemInst
	| def '&' MUT? var '[' value ']' ';'						# borrowElemInst
	| def? value binop value ';'								# binaryInst
	| def ('!' value | '-' value | value) ';'					# unaryInst;

phiInst:
	var ':' type '=' PHI '(' (
		label ':' value (',' label ':' value)*
	)? ')' ';';

argList: value (',' value)*;

binop:
	'*'
	| '/'
	| '%'
	| '+'
	| '-'
	| '<='
	| '>='
	| '<'
	| '>'
	| '=='
	| '!='
	| '&&'
	| '||'
	| '<-';

type:
	'&' MUT? type							# pointerType
	| '&' MUT? '[' type ']'					# sliceType
	| '[' type ';' INT_LITERAL ']'			# arrayType
	| (I32 | F32 | F64 | I1 | INT | FLOAT)	# primitiveType
	| '(' (type ',')* ')'					# productType
	| '(' type ('|' type)+ ')'				# sumType;

basicConstexpr:
	'-'? INT_LITERAL
	| '-'? FLOAT_LITERAL
	| '-'? DOUBLE_LITERAL
	| TRUE
	| FALSE;

constexpr:
	basicConstexpr
	| '{' basicConstexpr (',' basicConstexpr)* '}'; // array literal