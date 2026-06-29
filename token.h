#ifndef TOKEN_H
#define TOKEN_H

#include <string>
#include <ostream>

using namespace std;

class Token {
public:
    // Tipos de token
    enum Type {
        PLUS,     // +
        MINUS,    // -
        STAR,      // *
        DIV,      // /
        MODULO,   // %
        MENOR,    // <
        MENORIGUAL, // <=
        MAYOR,    // >
        MAYORIGUAL, // >=
        IGUALIGUAL, // ==
        DIFERENTE,  // !=
        AND,       // &&
        OR,        //

        FOR,
        ENDFOR,    // for
        WHILE,
        DO,
        ENDWHILE,  // while
        IF,
        ELSE,
        THEN,
        ENDIF,     // if
        BREAK,    // break
        PRINT,   // print
        FN,
        ENDFN,     // fn (function)
        ASSIGN,    // =

        VAR,       // var
        CONST,     // const
        STRUCT,    // struct
        RETURN,    // return
        TRUE,      // true
        FALSE,     // false
        TYPE,     // type
        NEW,      // new
        CONTINUE,
        PUB,
        DELETE,   // delete
        NOT,  
        PIPE,      // !
        QUESTION, // ?
        DOTQUESTION, // .?
        ENUM,
        UNION,
        ERROR,
        SWITCH,
        CASE,
        TRY,
        CATCH,
        DEFER,
        COMPTIME,
        NULLTOK,
        UNDEFINED,

        NUMDECIMAL,     // Número
        NUMFLOTANTE,
        NUMHEX,
        NUMBIN,
        ID,      // IDENT
        FREE,

        COMILLASDOBLES,
        COMILLASSIMPLES,
        COMA,
        PUNTO,
        DOSPUNTOS,
        SEMICOL,
        LPAREN,   // (
        RPAREN,   // )
        LBRACE,
        RBRACE,
        LCORCHETE,
        RCORCHETE,
        REFERENCIA, // &algomas

        ERR,     // Error
        END      // Fin de entrada
    };

    // Atributos
    Type type;
    string text;

    // Constructores
    Token(Type type);
    Token(Type type, char c);
    Token(Type type, const string& source, int first, int last);

    // Sobrecarga de operadores de salida
    friend ostream& operator<<(ostream& outs, const Token& tok);
    friend ostream& operator<<(ostream& outs, const Token* tok);
};

#endif // TOKEN_H