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
