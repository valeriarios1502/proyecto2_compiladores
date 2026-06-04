#ifndef PARSER_H
#define PARSER_H

#include <vector>
#include <string>
#include "token.h"
#include "scanner.h"
#include "ast.h"

using namespace std;

class Parser {
private:
    Scanner* scanner;
    Token*   current;
    Token*   previous;

    // ── Utilidades ──────────────────────────────────────────
    bool match(Token::Type ttype);
    bool check(Token::Type ttype);
    bool advance();
    bool isAtEnd();
    void expect(Token::Type ttype, const string& msg);

    // ── Top-level ────────────────────────────────────────────
    Programa*  parseP();
    Top_dec*   parseFnOrTemplate();
    Top_dec*   parseVar_dec();
    Top_dec*   parseConts_dec();
    Top_dec*   parseStruct_dec();
    Top_dec*   parseUnion_dec();
    Top_dec*   parsefn_dec();
    Top_dec*   parsetemplate();

    // ── Helpers ──────────────────────────────────────────────
    void       parseParamList(vector<string>& ids, vector<Type*>& tipos);

    // ── Cuerpo y sentencias ──────────────────────────────────
    Body*      parseBody();
    Stmt*      parseStmt();

    // ── Tipos ────────────────────────────────────────────────
    Type*      parseType();

    // ── Expresiones (precedencia) ────────────────────────────
    Exp*       parseExpr();
    Exp*       parseLogicExp();
    Exp*       parseCompareExp();
    Exp*       parseAddExp();
    Exp*       parseMulExp();
    Exp*       parseUnaryExp();
    Exp*       parsePostfix();
    Exp*       parsePrimaryExp();

public:
    Parser(Scanner* sc);
    Programa*  parseProgram();
};

#endif // PARSER_H