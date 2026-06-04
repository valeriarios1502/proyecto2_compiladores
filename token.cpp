#include <iostream>
#include "token.h"

using namespace std;

// -----------------------------
// Constructores
// -----------------------------

Token::Token(Type type) 
    : type(type), text("") { }

Token::Token(Type type, char c) 
    : type(type), text(string(1, c)) { }

Token::Token(Type type, const string& source, int first, int last) 
    : type(type), text(source.substr(first, last)) { }

// -----------------------------
// Sobrecarga de operador <<
// -----------------------------

// Para Token por referencia
ostream& operator<<(ostream& outs, const Token& tok) {
    switch (tok.type) {
        case Token::PLUS:   outs << "TOKEN(PLUS, \""   << tok.text << "\")"; break;
        case Token::MINUS:  outs << "TOKEN(MINUS, \""  << tok.text << "\")"; break;
        case Token::STAR:   outs << "TOKEN(STAR, \""   << tok.text << "\")"; break;
        case Token::DIV:    outs << "TOKEN(DIV, \""    << tok.text << "\")"; break;
        case Token::MODULO: outs << "TOKEN(MODULO, \"" << tok.text << "\")"; break;
        case Token::LPAREN: outs << "TOKEN(LPAREN, \"" << tok.text << "\")"; break;
        case Token::RPAREN: outs << "TOKEN(RPAREN, \"" << tok.text << "\")"; break;
        case Token::LBRACE: outs << "TOKEN(LBRACE, \"" << tok.text << "\")"; break;
        case Token::RBRACE: outs << "TOKEN(RBRACE, \"" << tok.text << "\")"; break;
        case Token::LCORCHETE: outs << "TOKEN(LCORCHETE, \"" << tok.text << "\")"; break;
        case Token::RCORCHETE: outs << "TOKEN(RCORCHETE, \"" << tok.text << "\")"; break;
        case Token::COMA:    outs << "TOKEN(COMA, \""    << tok.text << "\")"; break;
        case Token::DOSPUNTOS: outs << "TOKEN(DOSPUNTOS, \"" << tok.text << "\")"; break;
        case Token::SEMICOL: outs << "TOKEN(SEMICOL, \"" << tok.text << "\")"; break;
        case Token::PUNTO:   outs << "TOKEN(PUNTO, \""   << tok.text << "\")"; break;
        case Token::DOTQUESTION: outs << "TOKEN(DOTQUESTION, \"" << tok.text << "\")"; break;
        case Token::ENDFOR: outs << "TOKEN(ENDFOR, \"" << tok.text << "\")"; break;
        case Token::DO: outs << "TOKEN(DO, \"" << tok.text << "\")"; break;
         case Token::ENDWHILE: outs << "TOKEN(ENDWHILE, \"" << tok.text << "\")"; break;
        case Token::QUESTION: outs << "TOKEN(QUESTION, \"" << tok.text << "\")"; break;
        case Token::NOT:     outs << "TOKEN(NOT, \""     << tok.text << "\")"; break;
        case Token::AND:     outs << "TOKEN(AND, \""     << tok.text << "\")"; break;
        case Token::OR:      outs << "TOKEN(OR, \""      << tok.text << "\")"; break;
        case Token::ASSIGN:  outs << "TOKEN(ASSIGN, \""  << tok.text << "\")"; break;
        case Token::IGUALIGUAL: outs << "TOKEN(IGUALIGUAL, \"" << tok.text << "\")"; break;
        case Token::DIFERENTE: outs << "TOKEN(DIFERENTE, \"" << tok.text << "\")"; break;
        case Token::MENOR:   outs << "TOKEN(MENOR, \""   << tok.text << "\")"; break;
        case Token::MAYOR:   outs << "TOKEN(MAYOR, \""   << tok.text << "\")"; break;
        case Token::MENORIGUAL: outs << "TOKEN(MENORIGUAL, \"" << tok.text << "\")"; break;
        case Token::MAYORIGUAL: outs << "TOKEN(MAYORIGUAL, \"" << tok.text << "\")"; break;
        case Token::REFERENCIA: outs << "TOKEN(REFERENCIA, \"" << tok.text << "\")"; break;
        case Token::VAR:     outs << "TOKEN(VAR, \""     << tok.text << "\")"; break;
        case Token::CONST:   outs << "TOKEN(CONST, \""   << tok.text << "\")"; break;
        case Token::STRUCT:  outs << "TOKEN(STRUCT, \""  << tok.text << "\")"; break;
        case Token::TYPE:    outs << "TOKEN(TYPE, \""    << tok.text << "\")"; break;
        case Token::ENUM:    outs << "TOKEN(ENUM, \""    << tok.text << "\")"; break;
        case Token::UNION:   outs << "TOKEN(UNION, \""   << tok.text << "\")"; break;
        case Token::ERROR:   outs << "TOKEN(ERROR, \""   << tok.text << "\")"; break;
        case Token::SWITCH:  outs << "TOKEN(SWITCH, \""  << tok.text << "\")"; break;
        case Token::CASE:    outs << "TOKEN(CASE, \""    << tok.text << "\")"; break;
        case Token::TRY:     outs << "TOKEN(TRY, \""     << tok.text << "\")"; break;
        case Token::CATCH:   outs << "TOKEN(CATCH, \""   << tok.text << "\")"; break;
        case Token::DEFER:   outs << "TOKEN(DEFER, \""   << tok.text << "\")"; break;
        case Token::COMPTIME: outs << "TOKEN(COMPTIME, \"" << tok.text << "\")"; break;
        case Token::NULLTOK: outs << "TOKEN(NULLTOK, \"" << tok.text << "\")"; break;
        case Token::UNDEFINED: outs << "TOKEN(UNDEFINED, \"" << tok.text << "\")"; break;
        case Token::IF:      outs << "TOKEN(IF, \""      << tok.text << "\")"; break;
        case Token::ELSE:    outs << "TOKEN(ELSE, \""    << tok.text << "\")"; break;
        case Token::THEN:    outs << "TOKEN(THEN, \""    << tok.text << "\")"; break;
        case Token::WHILE:   outs << "TOKEN(WHILE, \""   << tok.text << "\")"; break;
        case Token::FOR:     outs << "TOKEN(FOR, \""     << tok.text << "\")"; break;
        case Token::BREAK:   outs << "TOKEN(BREAK, \""   << tok.text << "\")"; break;
        case Token::CONTINUE: outs << "TOKEN(CONTINUE, \"" << tok.text << "\")"; break;
        case Token::RETURN:  outs << "TOKEN(RETURN, \""  << tok.text << "\")"; break;
        case Token::NEW:     outs << "TOKEN(NEW, \""     << tok.text << "\")"; break;
        case Token::DELETE:  outs << "TOKEN(DELETE, \""  << tok.text << "\")"; break;
        case Token::PUB:     outs << "TOKEN(PUB, \""     << tok.text << "\")"; break;
        case Token::FN:      outs << "TOKEN(FN, \""      << tok.text << "\")"; break;
        case Token::NUMDECIMAL: outs << "TOKEN(NUMDECIMAL, \"" << tok.text << "\")"; break;
        case Token::NUMFLOTANTE: outs << "TOKEN(NUMFLOTANTE, \"" << tok.text << "\")"; break;
        case Token::NUMHEX:   outs << "TOKEN(NUMHEX, \""   << tok.text << "\")"; break;
        case Token::NUMBIN:   outs << "TOKEN(NUMBIN, \""   << tok.text << "\")"; break;
        case Token::COMILLASDOBLES: outs << "TOKEN(STRING, \"" << tok.text << "\")"; break;
        case Token::COMILLASSIMPLES: outs << "TOKEN(CHAR, \"" << tok.text << "\")"; break;
        case Token::END:      outs << "TOKEN(END)"; break;
    }
    return outs;
}

// Para Token puntero
ostream& operator<<(ostream& outs, const Token* tok) {
    if (!tok) return outs << "TOKEN(NULL)";
    return outs << *tok;  // delega al otro
}