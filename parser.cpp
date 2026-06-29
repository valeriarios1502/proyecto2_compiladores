#include <iostream>
#include <stdexcept>
#include "token.h"
#include "scanner.h"
#include "ast.h"
#include "parser.h"

using namespace std;

// =============================
// Constructor y utilidades
// =============================

Parser::Parser(Scanner* sc) : scanner(sc) {
    previous = nullptr;
    current = scanner->nextToken();
    if (current->type == Token::ERR)
        throw runtime_error("Error léxico");
}

bool Parser::match(Token::Type ttype) {
    if (check(ttype)) { advance(); return true; }
    return false;
}

bool Parser::check(Token::Type ttype) {
    if (isAtEnd()) return false;
    return current->type == ttype;
}

bool Parser::advance() {
    if (!isAtEnd()) {
        Token* temp = current;
        if (previous) delete previous;
        current = scanner->nextToken();
        previous = temp;
        if (check(Token::ERR))
            throw runtime_error("Error léxico");
        return true;
    }
    return false;
}

bool Parser::isAtEnd() {
    return current->type == Token::END;
}

// Helper: lanza error si el token esperado no está
void Parser::expect(Token::Type ttype, const string& msg) {
    if (!match(ttype))
        throw runtime_error(msg);
}

// =============================
// Punto de entrada
// =============================

Programa* Parser::parseProgram() {
    Programa* ast = parseP();
    if (!isAtEnd())
        throw runtime_error("Error sintáctico: tokens inesperados al final");
    cout << "Parseo exitoso" << endl;
    return ast;
}

// =============================
// <program> ::= <top-decl>*
// =============================

Programa* Parser::parseP() {
    Programa* p = new Programa();

    while (!isAtEnd()) {
        if (check(Token::VAR)) {
            p->declist.push_back(parseVar_dec());
        } else if (check(Token::CONST)) {
            p->declist.push_back(parseConts_dec());
            match(Token::SEMICOL);
        } else if (check(Token::STRUCT)) {
            p->declist.push_back(parseStruct_dec());
        } else if (check(Token::UNION)) {
            p->declist.push_back(parseUnion_dec());
        } else if (check(Token::PUB)) {
            p->declist.push_back(parsefn_dec());
        } else if (check(Token::FN)) {
            p->declist.push_back(parseFnOrTemplate());
        } else if (check(Token::COMPTIME)) {
            match(Token::COMPTIME);
            Body* b = parseBody();
            Fundec* f = new Fundec();
            f->nombre = "__comptime__";
            f->tipo = new IdType("void");
            f->cuerpo = b;
            p->declist.push_back(f);
        } else {
            throw runtime_error("Error sintáctico: declaración inesperada");
        }
    }
    return p;
}

// =============================
// <template-fn> ::= fn <ident> < type < <ident> > > ( <param-list>? ) : <type> <block>
// =============================

Top_dec* Parser::parseFnOrTemplate() {
    // Peek ahead: fn <id> < → template
    // Para hacerlo sin lookahead real consumimos fn, guardamos id, luego decidimos
    expect(Token::FN, "Se esperaba 'fn'");
    expect(Token::ID, "Se esperaba identificador después de 'fn'");
    string nombre = previous->text;
    expect(Token::MENOR, "Se esperaba '<' en template");
    expect(Token::TYPE,  "Se esperaba 'type' en template");
    expect(Token::MENOR, "Se esperaba '<' en template");
    expect(Token::ID,    "Se esperaba identificador de tipo en template");
    string tipo_param = previous->text;
    expect(Token::MAYOR, "Se esperaba '>' en template");
    expect(Token::MAYOR, "Se esperaba '>' en template");

    Template* t = new Template();
    t->id1 = nombre;
    t->id2 = tipo_param;

    expect(Token::LPAREN, "Se esperaba '(' en template");
    parseParamList(t->id_parametros, t->tipo_parametros);
    expect(Token::RPAREN, "Se esperaba ')' en template");
    expect(Token::DOSPUNTOS, "Se esperaba ':' en template");
    t->tipo = parseType();
    t->block = parseBody();

    return t;
    
        
}

// =============================
// <var-decl>   ::= var <ident> [ : <type> ] = <expr> ;
// =============================

Top_dec* Parser::parseVar_dec() {
    match(Token::VAR);
    VarDec* var = new VarDec();
    expect(Token::ID, "Se esperaba identificador en var");
    var->nombre = previous->text;

    if (match(Token::DOSPUNTOS)) {
        var->tienetipo = true;
        var->tipo = parseType();
        expect(Token::ASSIGN, "Se esperaba '=' en var");
        var->exp = parseExpr();       
        match(Token::SEMICOL);
    } else if (match(Token::ASSIGN)) {
        var->tienetipo = false;
        var->tipo = nullptr;
        var->exp = parseExpr();      
        match(Token::SEMICOL);
    } else {
        throw runtime_error("Error sintáctico en var: se esperaba ':' o '='");
    }
    return var;
}

// =============================
// <const-decl> ::= const <ident> [ : <type> ] = <expr> ;
// =============================

Top_dec* Parser::parseConts_dec() {
    match(Token::CONST);
    ConstDec* c = new ConstDec();
    expect(Token::ID, "Se esperaba identificador en const");
    c->nombre = previous->text;

    if (match(Token::DOSPUNTOS)) {
        c->tienetipo = true;
        c->tipo = parseType();
        expect(Token::ASSIGN, "Se esperaba '=' en const");
        c->exp = parseExpr();
        match(Token::SEMICOL);
    } 
    else if (match(Token::ASSIGN)) {
        c->tienetipo = false;
        c->exp = parseExpr();
        match(Token::SEMICOL);
    } 
    else {
        throw runtime_error("Error sintáctico en const: se esperaba ':' o '='");
    }
    return c;
}

// =============================
// <struct-decl> ::= struct <ident> { ( <ident> : <type> ; )* }
// =============================

Top_dec* Parser::parseStruct_dec() {
    match(Token::STRUCT);
    Structdec* s = new Structdec();
    expect(Token::ID, "Se esperaba identificador de struct");
    s->nombre = previous->text;

    expect(Token::LBRACE, "Se esperaba '{' en struct");
    while (!check(Token::RBRACE) && !isAtEnd()) {
        expect(Token::ID, "Se esperaba campo en struct");
        s->id_parametros.push_back(previous->text);
        expect(Token::DOSPUNTOS, "Se esperaba ':' en campo de struct");
        s->tipo_parametros.push_back(parseType());
        expect(Token::SEMICOL, "Se esperaba ';' después de campo de struct");
    }
    expect(Token::RBRACE, "Se esperaba '}' al cerrar struct");
    return s;
}

// =============================
// <union-decl> ::= union <ident> [ ( <ident> ) ] { ( <ident> : <type> ; )* }
// =============================

Top_dec* Parser::parseUnion_dec() {
    match(Token::UNION);

    // CORREGIDO: nombre va primero, luego el tag opcional
    expect(Token::ID, "Se esperaba nombre de union");
    string nombre = previous->text;

    string tag = "";
    if (match(Token::LPAREN)) {
        expect(Token::ID, "Se esperaba tag enum en union");
        tag = previous->text;
        expect(Token::RPAREN, "Se esperaba ')' en union");
    }

    UnionType* ut = new UnionType(nombre);

    expect(Token::LBRACE, "Se esperaba '{' en union");
    while (!check(Token::RBRACE) && !isAtEnd()) {
        expect(Token::ID, "Se esperaba campo en union");
        string campo = previous->text;
        expect(Token::DOSPUNTOS, "Se esperaba ':' en campo de union");
        Type* tipo = parseType();
        expect(Token::SEMICOL, "Se esperaba ';' en campo de union");
        ut->campo_nombres.push_back(campo);
        ut->campo_tipos.push_back(tipo);
    }
    expect(Token::RBRACE, "Se esperaba '}' al cerrar union");

    VarDec* vd = new VarDec();
    vd->nombre = nombre;
    vd->tipo = ut;
    vd->tienetipo = true;
    vd->exp = nullptr;
    return vd;
}

// =============================
// <fn-decl>  ::= pub fn <ident> ( <param-list>? ) <type> { <body> }
// =============================

Top_dec* Parser::parsefn_dec() {
    match(Token::PUB);
    expect(Token::FN, "Se esperaba 'fn' después de 'pub'");
    Fundec* f = new Fundec();
    expect(Token::ID, "Se esperaba nombre de función");
    f->nombre = previous->text;

    expect(Token::LPAREN, "Se esperaba '('");
    parseParamList(f->id_parametros, f->tipo_parametros);
    expect(Token::RPAREN, "Se esperaba ')'");
    f->tipo = parseType();
    f->cuerpo = parseBody();
    return f;
}

// =============================
// <param-list> ::= <param> { , <param> }*
// <param>      ::= [comptime] <ident> : <type>
// =============================

void Parser::parseParamList(vector<string>& ids, vector<Type*>& tipos) {
    if (check(Token::ID) || check(Token::COMPTIME)) {
        bool es_comptime = match(Token::COMPTIME);
        expect(Token::ID, "Se esperaba identificador de parámetro");
        ids.push_back(previous->text);
        expect(Token::DOSPUNTOS, "Se esperaba ':' en parámetro");
        tipos.push_back(parseType());

        while (match(Token::COMA)) {
            bool es_ct = match(Token::COMPTIME);
            expect(Token::ID, "Se esperaba identificador de parámetro");
            ids.push_back(previous->text);
            expect(Token::DOSPUNTOS, "Se esperaba ':' en parámetro");
            tipos.push_back(parseType());
        }
    }
}

// =============================
// <block> ::= { <stmt>* }
// =============================

Body* Parser::parseBody() {
    Body* b = new Body();
    expect(Token::LBRACE, "Se esperaba '{' para abrir bloque");
    while (!check(Token::RBRACE) && !isAtEnd()) {
        b->slist.push_back(parseStmt());
    }
    expect(Token::RBRACE, "Se esperaba '}' para cerrar bloque");
    return b;
}

// =============================
// <stmt>
// =============================

Stmt* Parser::parseStmt() {

    if (check(Token::VAR)) {
        VarDec* vd = (VarDec*)parseVar_dec();
        match(Token::SEMICOL);
        // Wrapeamos VarDec en un AsignStmt con exp directa
        // (el AST no tiene VarDecStmt, pero podemos crear uno ad-hoc)
        // Se devuelve como BodyStmt con cuerpo vacío y la var colgada;
        // La forma más limpia es reutilizar AsignStmt:
        AsignStmt* a = new AsignStmt(vd->nombre, vd->exp);
        vd->exp = nullptr;
        delete vd; // liberamos el wrapper
        return a;
    }

    // --- const local ---
    if (check(Token::CONST)) {
        ConstDec* cd = (ConstDec*)parseConts_dec();
        match(Token::SEMICOL);
        AsignStmt* a = new AsignStmt(cd->nombre, cd->exp);
        cd->exp = nullptr;
        delete cd;
        return a;
    }

    if (match(Token::RETURN)) {
        if (check(Token::SEMICOL) || check(Token::RBRACE)) {
            match(Token::SEMICOL);
            return new ReturnStm();
        }
        Exp* exp = parseExpr();
        match(Token::SEMICOL);
        return new ReturnStm(exp);
    }

    if (match(Token::PRINT)) {
        expect(Token::LPAREN, "Se esperaba '(' en print");
        Exp* exp = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en print");
        match(Token::SEMICOL);
        return new PrintStmt(exp);
    }

    if (match(Token::FREE)) {
        Exp* exp = parseLogicExp();
        match(Token::SEMICOL);
        return new DeleteStm(exp);
    }

    if (match(Token::DEFER)) {
        Stmt* s = parseStmt();
        return new DeferStmt(s);
    }

    if (match(Token::BREAK)) {
        if (match(Token::DOSPUNTOS)) {          // ← break : label expr ;
            match(Token::ID);
            Exp* val = parseExpr();
            match(Token::SEMICOL);
            return new BreakStmt(val);
        }                                        // ← llave que faltaba
        if (check(Token::SEMICOL) || check(Token::RBRACE)) {
            match(Token::SEMICOL);
            return new BreakStmt();
        }
    }

    if (match(Token::CONTINUE)) {
        match(Token::SEMICOL);
        return new ContinueStm();
    }

    if (match(Token::IF)) {
        expect(Token::LPAREN, "Se esperaba '(' en if");
        Exp* condicion = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en if");
        expect(Token::THEN, "Se esperaba 'then' en if");
        Body* if_cuerpo = parseBody();
        if (match(Token::ELSE)) {
            match(Token::THEN);
            Body* else_cuerpo = parseBody();
            return new IfStmt(condicion, if_cuerpo, else_cuerpo, true);
        }
        return new IfStmt(condicion, if_cuerpo, nullptr, false);
    }

    if (match(Token::WHILE)) {
        expect(Token::LPAREN, "Se esperaba '(' en while");
        Exp* condicion = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en while");
        expect(Token::LBRACE, "Se esperaba '{' en while");
        list<Stmt*> cuerpo;
        while (!check(Token::RBRACE) && !isAtEnd())
            cuerpo.push_back(parseStmt());
        expect(Token::RBRACE, "Se esperaba '}' en while");
        return new WhileStmt(condicion, cuerpo);
    }

    if (match(Token::FOR)) {
        expect(Token::LPAREN, "Se esperaba '(' en for");
        Exp* iterable = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en for");
        expect(Token::PIPE, "Se esperaba '|' en for");
        expect(Token::ID, "Se esperaba identificador en for");
        string var1 = previous->text;
        string var2 = "";
        if (match(Token::COMA)) {
            expect(Token::ID, "Se esperaba segundo identificador en for");
            var2 = previous->text;
        }
        expect(Token::PIPE, "Se esperaba '|' de cierre en for");
        Body* cuerpo = parseBody();
        return new ForStmt(
            new AsignStmt(var1, iterable),
            new NullExp(),
            new AsignStmt(var2.empty() ? "__idx__" : var2, new NullExp()),
            cuerpo
        );
    }

    if (match(Token::SWITCH)) {
        expect(Token::LPAREN, "Se esperaba '(' en switch");
        Exp* condicion = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en switch");
        expect(Token::LBRACE, "Se esperaba '{' en switch");
        SwitchStmt* sw = new SwitchStmt(condicion);
        while (!check(Token::RBRACE) && !isAtEnd()) {
            if (match(Token::ELSE)) {
                expect(Token::ASSIGN, "Se esperaba '=' de '=>' en case else");
                expect(Token::MAYOR, "Se esperaba '>' de '=>' en case else");
                sw->default_caso = parseBody();
            } else {
                Exp* patron = parseLogicExp();
                expect(Token::ASSIGN, "Se esperaba '=' de '=>' en case");
                expect(Token::MAYOR, "Se esperaba '>' de '=>' en case");
                Body* caso_body = parseBody();
                sw->casos.push_back({patron, caso_body});
            }
            match(Token::COMA);
        }
        expect(Token::RBRACE, "Se esperaba '}' al cerrar switch");
        return sw;
    }

    if (check(Token::ID)) {
        advance();
        string nombre = previous->text;

        // Construimos el lvalue como expresión postfix
        Exp* lval = new IdExp(nombre);

        // Seguimos consumiendo .campo, [idx], etc.
        while (true) {
            if (match(Token::PUNTO)) {
                if (match(Token::QUESTION)) {
                    lval = new PuntoExp(lval, "__unwrap__");
                } else {
                    expect(Token::ID, "Se esperaba identificador después de '.'");
                    lval = new PuntoExp(lval, previous->text);
                }
            } else if (match(Token::LCORCHETE)) {
                Exp* idx = parseLogicExp();
                expect(Token::RCORCHETE, "Se esperaba ']' en lvalue");
                lval = new AlgoconcorchetesExp(lval, idx);
            } else if (match(Token::LPAREN)) {
    FcallExp* call = new FcallExp();
    call->nombre = nombre;
    delete lval;
    lval = nullptr;
    if (!check(Token::RPAREN)) {
        call->argumentos.push_back(parseLogicExp());
        while (match(Token::COMA))
            call->argumentos.push_back(parseLogicExp());
    }
    expect(Token::RPAREN, "Se esperaba ')' en llamada");
    match(Token::SEMICOL);
    return new AsignStmt("__call__", call);  
        }else {
                break;
            }
        }

        // Ahora sí esperamos el =
        expect(Token::ASSIGN, "Se esperaba '=' en asignación");
        Exp* rval = parseExpr();
        match(Token::SEMICOL);

        // Si el lvalue es un IdExp simple, usamos AsignStmt normal
        if (IdExp* id = dynamic_cast<IdExp*>(lval)) {
            delete lval;
            return new AsignStmt(nombre, rval);
        }

        // Si es campo o índice, usamos DerefAssignStmt (o el nodo que tengas)
        return new DerefAssignStmt(lval, rval);
    }

    // Al inicio de parseStmt(), antes de los demás checks:
    if (match(Token::STAR)) {
    Exp* lval = parsePostfix();
    expect(Token::ASSIGN, "Se esperaba '=' después de expresión desreferenciada");
    Exp* rval = parseExpr();
    match(Token::SEMICOL);
    return new DerefAssignStmt(lval, rval);  // ← CORRECTO
}

    throw runtime_error("Error sintáctico: sentencia inesperada - token: '" 
    + current->text + "' tipo: " + to_string(current->type) 
    + " | previous: '" + (previous ? previous->text : "null") + "'");
}

// =============================
// <type>
// =============================

Type* Parser::parseType() {
    // ? <type>  →  optional
    if (match(Token::QUESTION)) {
        Type* inner = parseType();
        return new OptionalType(inner);
    }

    if (match(Token::TYPE)) {
        return new IdType("type");
    }

    // ! <type>  →  error union sin nombre
    if (match(Token::NOT)) {
        Type* inner = parseType();
        return new ErrorType(inner);
    }

    //*  <type>  →  pointer
    if (match(Token::STAR)) {
        Type* inner = parseType();
        return new PointerType(inner);
    }

    if (match(Token::LCORCHETE)) {
        Exp* exp1 = parseLogicExp();
        expect(Token::RCORCHETE, "Se esperaba ']' en tipo array");
        if (match(Token::LCORCHETE)) {
            Exp* exp2 = parseLogicExp();
            expect(Token::RCORCHETE, "Se esperaba ']' en tipo array 2D");
            Type* inner = parseType();
            return new ArrayType(exp1, exp2, inner, true);
        }
        Type* inner = parseType();
        return new ArrayType(exp1, nullptr, inner, false);
    }

    // <ident> [ ! <type> ]  →  MyError!T
    if (match(Token::ID)) {
        string id = previous->text;
        if (match(Token::NOT)) {
            // Es error-union: <ident>!<type>
            Type* inner = parseType();
            return new ErrorType(inner); // ErrorType guarda el tipo ok; el nombre se pierde
        }
        return new IdType(id);
    }

    throw runtime_error("Error sintáctico: tipo desconocido");
}

// =============================
// Expresiones (precedencia ascendente)
// =============================

// <expr> ::= try <expr> | comptime <expr> | <logic-expr>
Exp* Parser::parseExpr() {
    if (match(Token::TRY)) {
        Exp* e = parseLogicExp();
        FcallExp* f = new FcallExp();
        f->nombre = "__try__";
        f->argumentos.push_back(e);
        return f;
    }
    if (match(Token::COMPTIME)) {
        return parseLogicExp();
    }

    Exp* e = parseLogicExp();

    if (match(Token::CATCH)) {
        string error_var = "";
        if (match(Token::PIPE)) {
            if (check(Token::ID)) { 
                match(Token::ID); 
                error_var = previous->text; 
            }
            expect(Token::PIPE, "Se esperaba '|' de cierre en catch");  // ← falta este expect
        }
        Body* catch_body = parseBody();
        
        FcallExp* f = new FcallExp();
        f->nombre = "__catch__";
        f->argumentos.push_back(e);
        return f;
    }

    return e;
}

// <logic-expr> ::= <compare-expr> { && | || <compare-expr> }*
Exp* Parser::parseLogicExp() {
    Exp* l = parseCompareExp();
    while (check(Token::AND) || check(Token::OR)) {
        BinaryOp op = match(Token::AND) ? AND : (advance(), OR);
        Exp* r = parseCompareExp();
        l = new BinaryExp(l, r, op);
    }
    return l;
}

// <compare-expr> ::= <add-expr> { == | != | < | > | <= | >= <add-expr> }*
Exp* Parser::parseCompareExp() {
    Exp* l = parseAddExp();
    while (check(Token::IGUALIGUAL) || check(Token::DIFERENTE) ||
           check(Token::MAYOR) || check(Token::MAYORIGUAL) ||
           check(Token::MENOR) || check(Token::MENORIGUAL)) {

        BinaryOp op;
        if (match(Token::IGUALIGUAL)) op = IGUALIGUAL;
        else if (match(Token::DIFERENTE)) op = DIFERENTE_OP;
        else if (match(Token::MAYOR)) op = MAYOR;
        else if (match(Token::MAYORIGUAL)) op = MAYORI;
        else if (match(Token::MENOR)) op = MENOR;
        else { match(Token::MENORIGUAL); op = MENORI; }

        Exp* r = parseAddExp();
        l = new BinaryExp(l, r, op);
    }
    return l;
}

// <add-expr> ::= <mul-expr> { + | - <mul-expr> }*
Exp* Parser::parseAddExp() {
    Exp* l = parseMulExp();
    while (check(Token::PLUS) || check(Token::MINUS)) {
        BinaryOp op = match(Token::PLUS) ? PLUS_OP : (advance(), MINUS_OP);
        Exp* r = parseMulExp();
        l = new BinaryExp(l, r, op);
    }
    return l;
}

// <mul-expr> ::= <unary-expr> {*  | / | % <unary-expr> }*
Exp* Parser::parseMulExp() {
    Exp* l = parseUnaryExp();
    while (check(Token::STAR) || check(Token::DIV) || check(Token::MODULO)) {
        BinaryOp op;
        if (match(Token::STAR))
            op = MUL_OP;
        else if (match(Token::DIV))
            op = DIV_OP;
        else {
            match(Token::MODULO);
            op = MODULO_OP;
        }
        Exp* r = parseUnaryExp();
        l = new BinaryExp(l, r, op);
    }
    return l;
}

// <unary-expr> ::= { ! | & |*  } <postfix-expr>
Exp* Parser::parseUnaryExp() {
    if (match(Token::NOT)) {
        Exp* e = parsePostfix();
        return new UnaryExp(UnaryExp::NOT_OP, e);
    }
    if (match(Token::REFERENCIA)) {
        Exp* e = parsePostfix();
        return new UnaryExp(UnaryExp::ADDRESS, e);
    }
    if (match(Token::STAR)) {
        Exp* e = parsePostfix();
        return new UnaryExp(UnaryExp::DEREF, e);
    }
    if (match(Token::MINUS)) {
        Exp* e = parsePostfix();
        return new UnaryExp(UnaryExp::NEGATE, e);
    }
    return parsePostfix();
}

// <postfix-expr> ::= <primary-expr> ( '[' ... ']' | '.' <id> | '.?' | 'orelse' <expr> )*
Exp* Parser::parsePostfix() {
    Exp* e = parsePrimaryExp();

    while (true) {
        if (match(Token::LCORCHETE)) {
            // <postfix>[<expr>]  o  <postfix>[<arg-list>]
            Exp* first_arg = parseLogicExp();
            if (match(Token::COMA)) {
                // arg-list
                vector<Exp* > args;
                args.push_back(first_arg);
                args.push_back(parseLogicExp());
                while (match(Token::COMA))
                    args.push_back(parseLogicExp());
                expect(Token::RCORCHETE, "Se esperaba ']'");
                e = new AlgoconcorchetesylistaExp(e, args);
            } else {
                expect(Token::RCORCHETE, "Se esperaba ']'");
                e = new AlgoconcorchetesExp(e, first_arg);
            }
        } else if (match(Token::PUNTO)) {
            // .?  (unwrap opcional)
            if (match(Token::QUESTION)) {
                // Representamos .? como PuntoExp con id especial
                e = new PuntoExp(e, "__unwrap__");
            } else {
                expect(Token::ID, "Se esperaba identificador después de '.'");
                e = new PuntoExp(e, previous->text);
            }
        } else if (check(Token::DOTQUESTION)) {
            // Token .? combinado (el scanner lo produce)
            advance();
            e = new PuntoExp(e, "__unwrap__");
        }  else if (check(Token::OR) && current->text == "orelse") {
            match(Token::OR);
            Exp* fallback = parseLogicExp();
            e = new BinaryExp(e, fallback, OR);
        } else if (match(Token::LPAREN)) {            // Llamada a función: f(args)
            FcallExp* call = new FcallExp();
            // Extraemos el nombre si e es IdExp
            if (IdExp* id = dynamic_cast<IdExp* >(e)) {
                call->nombre = id->value;
                delete e;
            } else {
                call->nombre = "__expr_call__";
                call->argumentos.push_back(e);
            }
            if (!check(Token::RPAREN)) {
                call->argumentos.push_back(parseLogicExp());
                while (match(Token::COMA))
                    call->argumentos.push_back(parseLogicExp());
            }
            expect(Token::RPAREN, "Se esperaba ')' en llamada");
            e = call;
        } else {
            break;
        }
    }
    return e;
}

// <primary-expr>
Exp* Parser::parsePrimaryExp() {

    // ( <expr> )
    if (match(Token::LPAREN)) {
        Exp* e = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en expresión agrupada");
        return e;
    }

    // null
    if (match(Token::NULLTOK)) {
        return new NullExp();
    }

    // undefined
    if (match(Token::UNDEFINED)) {
        return new UndefinedExp();
    }

    // true / false
    if (match(Token::TRUE) || match(Token::FALSE)) {
        BoolExp* b = new BoolExp();
        b->booleano = previous->text;
        return b;
    }

    // Números if (match(Token::NUMDECIMAL))
    if (match(Token::NUMDECIMAL)) {
        return new NumberExpDecimal(stoi(previous->text));
    } 
    if (match(Token::NUMFLOTANTE)) {
        return new NumberExpFlotante(stof(previous->text));
    }
    if (match(Token::NUMHEX)) {
        return new NumberExpDecimal((int)stoul(previous->text, nullptr, 16));
    } 
    if (match(Token::NUMBIN)) {
        string s = previous->text.substr(2); // quitar 0b
        return new NumberExpDecimal((int)stoul(s, nullptr, 2));
    }

    // String literal
    if (match(Token::COMILLASDOBLES)) {
        return new StringExp(previous->text);
    }

    // Char literal
    if (match(Token::COMILLASSIMPLES)) {
        char ch = previous->text.empty() ? '\0' : previous->text[0];
        return new CharExp(ch);
    }

    // new <type>
    if (match(Token::NEW)) {
        Type* tipo = parseType();
        return new NewExp(tipo);
    }

    // Lambda: fn(<params>?):<type><block>
    if (match(Token::FN)) {
        expect(Token::LPAREN, "Se esperaba '(' en lambda");
        vector<string> ids;
        vector<Type*> tipos;
        parseParamList(ids, tipos);
        expect(Token::RPAREN, "Se esperaba ')' en lambda");
        expect(Token::DOSPUNTOS, "Se esperaba ':' en lambda");
        Type* ret = parseType();
        Body* body = parseBody();
        return new LambdaExp(ret, body, ids, tipos);
    }

    // Identificador (puede ser llamada a función o variable)
    if (match(Token::ID)) {
        string nombre = previous->text;
        // Llamada a función  f(...)  — el postfix lo manejará el nivel superior
        return new IdExp(nombre);
    }


    throw runtime_error("Error sintáctico: expresión primaria inesperada");
}