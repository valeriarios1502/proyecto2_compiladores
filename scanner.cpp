#include <iostream>
#include <cstring>
#include <fstream>
#include "token.h"
#include "scanner.h"

using namespace std;

// -----------------------------
// Constructor
// -----------------------------
Scanner::Scanner(const char* s): input(s), first(0), current(0) { 
}

// -----------------------------
// Función auxiliar
// -----------------------------

bool is_white_space(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

bool is_identifier_start(char c) {
    return isalpha(c) || c == '_';
}

bool is_identifier_part(char c) {
    return isalnum(c) || c == '_';
}

// -----------------------------
// nextToken: obtiene el siguiente token
// -----------------------------

Token* Scanner::nextToken() {
    Token* token;

    while (current < input.length() && is_white_space(input[current]))
        current++;

    if (current >= input.length())
        return new Token(Token::END);

    char c = input[current];
    first = current;

    if (isdigit(c)) {
        current++;
        if (c == '0' && current < input.length() && (input[current] == 'x' || input[current] == 'X')) {
            current++;
            while (current < input.length() && isxdigit(input[current]))
                current++;
            token = new Token(Token::NUMHEX, input, first, current - first);
        } else if (c == '0' && current < input.length() && (input[current] == 'b' || input[current] == 'B')) {
            current++;
            while (current < input.length() && (input[current] == '0' || input[current] == '1'))
                current++;
            token = new Token(Token::NUMBIN, input, first, current - first);
        } else {
            while (current < input.length() && isdigit(input[current]))
                current++;
            if (current < input.length() && input[current] == '.') {
                current++;
                while (current < input.length() && isdigit(input[current]))
                    current++;
                token = new Token(Token::NUMFLOTANTE, input, first, current - first);
            } else {
                token = new Token(Token::NUMDECIMAL, input, first, current - first);
            }
        }
        return token;
    }

    if (is_identifier_start(c)) {
        current++;
        while (current < input.length() && is_identifier_part(input[current]))
            current++;
        string lexema = input.substr(first, current - first);
        if (lexema=="fn") return new Token(Token::FN, input, first, current - first);
        if (lexema=="endfn") return new Token(Token::ENDFN, input, first, current - first);
        if (lexema=="return") return new Token(Token::RETURN, input, first, current - first);
        if (lexema=="print") return new Token(Token::PRINT, input, first, current - first);
        if (lexema=="for") return new Token(Token::FOR, input, first, current - first);
        if (lexema=="endfor") return new Token(Token::ENDFOR, input, first, current - first);
        if (lexema=="true") return new Token(Token::TRUE, input, first, current - first);
        if (lexema=="break") return new Token(Token::BREAK, input, first, current - first);
        if (lexema=="false") return new Token(Token::FALSE, input, first, current - first);
        if (lexema=="const") return new Token(Token::CONST, input, first, current - first);
        if (lexema=="if") return new Token(Token::IF, input, first, current - first);
        if (lexema=="else") return new Token(Token::ELSE, input, first, current - first);
        if (lexema=="then") return new Token(Token::THEN, input, first, current - first);
        if (lexema=="endif") return new Token(Token::ENDIF, input, first, current - first);
        if (lexema=="do") return new Token(Token::DO, input, first, current - first);
        if (lexema=="while") return new Token(Token::WHILE, input, first, current - first);
        if (lexema=="endwhile") return new Token(Token::ENDWHILE, input, first, current - first);
        if (lexema=="var") return new Token(Token::VAR, input, first, current - first);
        if (lexema=="and") return new Token(Token::AND, input, first, current - first);
        if (lexema=="or") return new Token(Token::OR, input, first, current - first);
        if (lexema=="not") return new Token(Token::NOT, input, first, current - first);
        if (lexema=="struct") return new Token(Token::STRUCT, input, first, current - first);
        if (lexema=="new") return new Token(Token::NEW, input, first, current - first);
        if (lexema=="delete") return new Token(Token::DELETE, input, first, current - first);
        if (lexema=="type") return new Token(Token::TYPE, input, first, current - first);
        if (lexema=="pub") return new Token(Token::PUB, input, first, current - first);
        if (lexema=="continue") return new Token(Token::CONTINUE, input, first, current - first);
        if (lexema=="comptime") return new Token(Token::COMPTIME, input, first, current - first);
        if (lexema=="defer") return new Token(Token::DEFER, input, first, current - first);
        if (lexema=="switch") return new Token(Token::SWITCH, input, first, current - first);
        if (lexema=="case") return new Token(Token::CASE, input, first, current - first);
        if (lexema=="try") return new Token(Token::TRY, input, first, current - first);
        if (lexema=="catch") return new Token(Token::CATCH, input, first, current - first);
        if (lexema=="null") return new Token(Token::NULLTOK, input, first, current - first);
        if (lexema=="undefined") return new Token(Token::UNDEFINED, input, first, current - first);
        if (lexema=="enum") return new Token(Token::ENUM, input, first, current - first);
        if (lexema=="union") return new Token(Token::UNION, input, first, current - first);
        if (lexema=="error") return new Token(Token::ERROR, input, first, current - first);
        if (lexema=="orelse") return new Token(Token::OR, input, first, current - first);
        if (lexema=="free") return new Token(Token::FREE, input, first, current - first);
        return new Token(Token::ID, input, first, current - first);
    }

    switch (c) {
        case '"': {
            current++;
            while (current < input.length() && input[current] != '"') {
                if (input[current] == '\\' && current + 1 < input.length()) {
                    current += 2;
                } else {
                    current++;
                }
            }
            if (current >= input.length()) {
                token = new Token(Token::ERR, input, first, current - first);
                current = input.length();
                return token;
            }
            token = new Token(Token::COMILLASDOBLES, input, first + 1, current - first - 1);
            current++;
            return token;
        }
        case '\'': {
            current++;
            if (current < input.length() && input[current] != '\'') {
                if (input[current] == '\\' && current + 1 < input.length()) {
                    current += 2;
                } else {
                    current++;
                }
            }
            if (current >= input.length() || input[current] != '\'') {
                token = new Token(Token::ERR, input, first, current - first);
                current = input.length();
                return token;
            }
            token = new Token(Token::COMILLASSIMPLES, input, first + 1, current - first - 1);
            current++;
            return token;
        }
        case ',': token = new Token(Token::COMA, c); break;
        case ':': token = new Token(Token::DOSPUNTOS, c); break;
        case '.':
            if (current + 1 < input.length() && input[current + 1] == '?') {
                current++;
                token = new Token(Token::DOTQUESTION, input, first, 2);
            } else {
                token = new Token(Token::PUNTO, c);
            }
            break;
        case ';': token = new Token(Token::SEMICOL, c); break;
        case '!':
            if (current + 1 < input.length() && input[current+1] == '=') {
                current++;
                token = new Token(Token::DIFERENTE, input, first, current + 1 - first);
            } else {
                token = new Token(Token::NOT, c);
            }
            break;
        case '=':
            if (current + 1 < input.length() && input[current+1] == '=') {
                current++;
                token = new Token(Token::IGUALIGUAL, input, first, current + 1 - first);
            } else {
                token = new Token(Token::ASSIGN, c);
            }
            break;
        case '+': token = new Token(Token::PLUS, c); break;
        case '-': token = new Token(Token::MINUS, c); break;
        case '*': token = new Token(Token::STAR, c); break;
        case '/': token = new Token(Token::DIV, c); break;
        case '|':
            if (current + 1 < input.length() && input[current + 1] == '|') {
                current++;
                token = new Token(Token::OR, input, first, current + 1 - first);
            } else {
                token = new Token(Token::PIPE, c);
            }
            break;
        case '(': token = new Token(Token::LPAREN, c); break;
        case ')': token = new Token(Token::RPAREN, c); break;
        case '[': token = new Token(Token::LCORCHETE, c); break;
        case ']': token = new Token(Token::RCORCHETE, c); break;
        case '{': token = new Token(Token::LBRACE, c); break;
        case '}': token = new Token(Token::RBRACE, c); break;
        case '&':
            if (current + 1 < input.length() && input[current + 1] == '&') {
                current++;
                token = new Token(Token::AND, input, first, current + 1 - first);
            } else {
                token = new Token(Token::REFERENCIA, c);
            }
            break;
        case '%': token = new Token(Token::MODULO, c); break;
        case '>':
            if (current + 1 < input.length() && input[current+1] == '=') {
                current++;
                token = new Token(Token::MAYORIGUAL, input, first, current + 1 - first);
            } else {
                token = new Token(Token::MAYOR, c);
            }
            break;
        case '<':
            if (current + 1 < input.length() && input[current+1] == '=') {
                current++;
                token = new Token(Token::MENORIGUAL, input, first, current + 1 - first);
            } else {
                token = new Token(Token::MENOR, c);
            }
            break;
        case '?': token = new Token(Token::QUESTION, c); break;
        default: token = new Token(Token::ERR, c); break;
    }
    current++;
    return token;
}

// -----------------------------
// Destructor
// -----------------------------
Scanner::~Scanner() { }

// -----------------------------
// Función de prueba
// -----------------------------

void ejecutar_scanner(Scanner* scanner, const string& InputFile) {
    Token* tok;

    string OutputFileName = InputFile;
    size_t pos = OutputFileName.find_last_of(".");
    if (pos != string::npos) {
        OutputFileName = OutputFileName.substr(0, pos);
    }
    OutputFileName += "_tokens.txt";

    ofstream outFile(OutputFileName);
    if (!outFile.is_open()) {
        cerr << "Error: no se pudo abrir el archivo " << OutputFileName << endl;
        return;
    }

    outFile << "Scanner\n" << endl;

    while (true) {
        tok = scanner->nextToken();

        if (tok->type == Token::END) {
            outFile << *tok << endl;
            delete tok;
            outFile << "\nScanner exitoso" << endl << endl;
            outFile.close();
            return;
        }

        if (tok->type == Token::ERR) {
            outFile << *tok << endl;
            delete tok;
            outFile << "Caracter invalido" << endl << endl;
            outFile << "Scanner no exitoso" << endl << endl;
            outFile.close();
            return;
        }

        outFile << *tok << endl;
        delete tok;
    }
}
