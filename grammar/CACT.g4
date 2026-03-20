grammar CACT;

/****** lexer ******/

// Keywords
TRUE: 'true';
FALSE: 'false';
CONST: 'const';
INT: 'int';
BOOL: 'bool';
FLOAT: 'float';
DOUBLE: 'double';
VOID: 'void';
IF: 'if';
ELSE: 'else';
WHILE: 'while';
BREAK: 'break';
CONTINUE: 'continue';
RETURN: 'return';

fragment DIGIT: [0-9];
fragment DIGIT_OCT: [0-7];
fragment DIGIT_HEX: [0-9a-fA-F];

// Literals

INT_LITERAL: 
    [1-9] DIGIT*
    | '0' DIGIT_OCT*
    | ('0x' | '0X') DIGIT_HEX+
    ;

fragment FLOAT_NUMBER
    : DIGIT+ '.' DIGIT* ([eE] [+-]? DIGIT+)?
    | '.' DIGIT+ ([eE] [+-]? DIGIT+)?
    | DIGIT+ [eE] [+-]? DIGIT+
    ;

FLOAT_LITERAL: FLOAT_NUMBER [fF];
DOUBLE_LITERAL: FLOAT_NUMBER;

// Identifier
ID: [a-zA-Z_][a-zA-Z0-9_]*;

// Comments
LINE_COMMENT: '//' ~[\r\n]* -> skip;
BLOCK_COMMENT: '/*' .*? '*/' -> skip;

// Whitespace
WS: [ \t\r\n]+ -> skip;

/****** parser ******/

compUnit: (decl | funcDef)* EOF;
decl: constDecl | varDecl ;
constDecl: CONST basicType constDef (',' constDef)* ';' ;
constDef: ID ('[' INT_LITERAL ']')* '=' constInitVal ;
varDecl: basicType varDef (',' varDef)* ';' ;
varDef: ID ('[' INT_LITERAL ']')* ('=' constInitVal)? ;
constInitVal
    : constExp
    | '{' (constInitVal (',' constInitVal)*)? '}'
    ;
funcDef: funcType ID '(' funcParams? ')' block ;
funcParams: funcParam (',' funcParam)* ;
funcParam: basicType ID ('[' INT_LITERAL? ']' ('[' INT_LITERAL ']')*)?;
funcArgs: exp (',' exp)* ;
block: '{' blockItem* '}' ;
blockItem: decl | stmt ;
stmt
    : lVal '=' exp ';'
    | exp? ';'
    | block
    | IF '(' cond ')' stmt (ELSE stmt)?
    | WHILE '(' cond ')' stmt
    | BREAK ';'
    | CONTINUE ';'
    | RETURN exp? ';'
    ;

lVal: ID ('[' exp ']')* ;

exp: addExp; // FIXME: why not lOrExp?
cond: lOrExp;
constExp: number | boolNumber;

lOrExp
    : lAndExp
    | lOrExp ('||' lAndExp)
    ;
lAndExp
    : eqExp
    | lAndExp ('&&' eqExp)
    ;
eqExp
    : relExp
    | eqExp ('==' | '!=') relExp
    ;
relExp
    : addExp
    | relExp ('<' | '>' | '<=' | '>=') addExp
    ;
addExp
    : mulExp
    | addExp ('+' | '-') mulExp
    ;
mulExp
    : unaryExp
    | mulExp ('*' | '/' | '%') unaryExp
    ;
unaryExp
    : ('+' | '-' | '!') unaryExp
    | primaryExp
    | ID '(' funcArgs? ')'
    ;
primaryExp
    : '(' exp ')'
    | lVal
    | number
    | boolNumber
    ;

number: INT_LITERAL | FLOAT_LITERAL | DOUBLE_LITERAL;
boolNumber: TRUE | FALSE;

basicType: INT | BOOL | FLOAT | DOUBLE;
funcType: basicType | VOID;
