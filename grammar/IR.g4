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
MUT: 'mut';
REF: 'ref';
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

program: (constDecl | letDecl | funcDecl)* EOF;

constDecl: CONST REF? ID ':' type '=' constexpr ';';
letDecl: LET REF? MUT? ID ':' type ('=' constexpr)? ';';

funcDecl:
	FN ID '(' paramList? ')' ('->' type)? '{' (constDecl | letDecl)* block* '}';

paramList: param (',' param)*;
param: ID ':' type;

temp: '$' INT_LITERAL;
var: temp | ID;
value: var | constexpr;
label: ID;

exit:
	RETURN value? ';' # returnExit
	| BRANCH value '?' label ':' label ';' # branchExit
	| JUMP label ';' # jumpExit
	;

block: '.' label ':' inst* exit;

inst:
	var '[' value ']' '=' value ';' # sliceStoreInst
	| '*(' var ')' '=' value ';' # pointerStoreInst
	| var ':' type '=' ID '(' (argList)? ')' ';' # callInst
	| var ':' type '=' value '[' value ']' ';' # sliceLoadInst
	| var ':' type '=' '*(' var ')' ';' # pointerLoadInst
	| var ':' type '=' value binop value ';' # binaryInst
	| var ':' type '=' ('!' value | '-' value | value) ';' # unaryInst
	;

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
		'&' MUT? '[' type ']' // pointer
		| '[' type ';' INT_LITERAL ']' // array
		| INT
		| FLOAT
		| DOUBLE
		| BOOL
		| '(' (type ',')* ')' // product
		| '(' type ('|' type)+ ')' // sum
	);

basicConstexpr:
	'-'? INT_LITERAL
	| '-'? FLOAT_LITERAL
	| '-'? DOUBLE_LITERAL
	| TRUE
	| FALSE
	;

constexpr:
	basicConstexpr
	| '{' basicConstexpr (',' basicConstexpr)* '}' // array literal
	;