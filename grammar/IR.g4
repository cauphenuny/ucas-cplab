grammar IR;

/****** lexer ******/

// Keywords
FN: 'fn';
LET: 'let';
INT: 'i32';
FLOAT: 'f32';
DOUBLE: 'f64';
BOOL: 'bool';
CONST: 'const';
TRUE: 'true';
FALSE: 'false';
RETURN: 'return';
BRANCH: 'branch';
JUMP: 'jump';

ID: [a-zA-Z_][a-zA-Z0-9_]*;

fragment DIGIT: [0-9];

// Literals

INT_LITERAL: DIGIT+;

fragment FLOAT_NUMBER:
	DIGIT+ '.' DIGIT* ([eE] [+-]? DIGIT+)?
	| '.' DIGIT+ ([eE] [+-]? DIGIT+)?
	| DIGIT+ [eE] [+-]? DIGIT+;

FLOAT_LITERAL: FLOAT_NUMBER [fF];
DOUBLE_LITERAL: FLOAT_NUMBER;

// Whitespace
WS: [ \t\r\n]+ -> skip;

/****** parser ******/

program: (globalDecl | funcDecl)* EOF;

globalDecl: LET ID ':' type ( '=' constexpr)? ';';

funcDecl:
	FN ID '(' paramList? ')' ('->' type)? '{' localDecl* block* '}';

paramList: param (',' param)*;
param: ID ':' type;

localDecl: LET ID ':' type ( '=' constexpr)? ';';

temp: '$' INT_LITERAL;
label: '.' ID;
var: temp | ID;
value: var | constexpr;

exit:
	RETURN value? ';'
	| BRANCH value '?' label ':' label ';'
	| JUMP label ';';

block: label ':' inst* exit;

inst:
	var '[' value ']' '=' value ';' // store
	| var ':' type '=' ID '(' (argList)? ')' ';' // call
	| var ':' type '=' value '[' value ']' ';' // load
	| var ':' type '=' value binop value ';' // binary op
	| var ':' type '=' ('!' value | '-' var | value) ';'; // unary or simple assign

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
	| '||';

type:
	(
		'&' '[' type ']' // pointer
		| '[' type ';' INT_LITERAL ']' // array
		| INT
		| FLOAT
		| DOUBLE
		| BOOL
		| '(' (type ',')* ')' // product
		| '(' type ('|' type)+ ')' // sum
	) (CONST)?;

constexpr:
	'-'? INT_LITERAL
	| '-'? FLOAT_LITERAL
	| '-'? DOUBLE_LITERAL
	| TRUE
	| FALSE
	| '{' (constexpr (',' constexpr)*)? '}';