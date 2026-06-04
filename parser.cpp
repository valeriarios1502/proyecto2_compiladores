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
            match(Token::SEMICOL);
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
            // Distinguir template vs fn sin pub:
            // template: fn <id> < <id> > ...
            // fn sin pub también puede ser fn <id> ( ...
            // Usamos lookahead: después de fn id, si viene MENOR → template
            p->declist.push_back(parseFnOrTemplate());
        } else if (check(Token::COMPTIME)) {
            // comptime block a nivel top-level
            match(Token::COMPTIME);
            Body* b = parseBody();
            // Lo guardamos como un BodyStmt dentro de una Fundec "comptime"
            Fundec* f = new Fundec();
            f->nombre = "__comptime__";
            f->tipo   = new IdType("void");
            f->cuerpo = b;
            p->declist.push_back(f);
        } else {
            throw runtime_error("Error sintáctico: declaración inesperada");
        }
    }
    return p;
}

// =============================
// Decide fn normal vs template
// =============================

Top_dec* Parser::parseFnOrTemplate() {
    // Peek ahead: fn <id> < → template
    // Para hacerlo sin lookahead real consumimos fn, guardamos id, luego decidimos
    expect(Token::FN, "Se esperaba 'fn'");
    expect(Token::ID, "Se esperaba identificador después de 'fn'");
    string nombre = previous->text;

    if (check(Token::MENOR)) {
        // Es template: fn <id> < <id> > ( <param-list> ) : <type> <block>
        match(Token::MENOR);
        expect(Token::ID, "Se esperaba parámetro de tipo en template");
        string tipo_param = previous->text;
        expect(Token::MAYOR, "Se esperaba '>' en template");

        Template* t = new Template();
        t->id1 = nombre;
        t->id2 = tipo_param;

        expect(Token::LPAREN, "Se esperaba '(' en template");
        parseParamList(t->id_parametros, t->tipo_parametros);
        expect(Token::RPAREN, "Se esperaba ')' en template");
        expect(Token::DOSPUNTOS, "Se esperaba ':' en template");
        t->tipo  = parseType();
        t->block = parseBody();
        return t;
    } else {
        // fn sin pub (función interna sin pub)
        Fundec* f = new Fundec();
        f->nombre = nombre;

        expect(Token::LPAREN, "Se esperaba '(' en fn");
        parseParamList(f->id_parametros, f->tipo_parametros);
        expect(Token::RPAREN, "Se esperaba ')'");
        f->tipo   = parseType();
        f->cuerpo = parseBody();
        return f;
    }
}

// =============================
// <var-decl>
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
        var->exp = parseLogicExp();
    } else if (match(Token::ASSIGN)) {
        var->tienetipo = false;
        var->tipo = nullptr;
        var->exp = parseLogicExp();
    } else {
        throw runtime_error("Error sintáctico en var: se esperaba ':' o '='");
    }
    return var;
}

// =============================
// <const-decl>
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
        c->exp = parseLogicExp();
    } else if (match(Token::ASSIGN)) {
        c->tienetipo = false;
        c->tipo = nullptr;
        c->exp = parseLogicExp();
    } else {
        throw runtime_error("Error sintáctico en const: se esperaba ':' o '='");
    }
    return c;
}

// =============================
// <struct-decl> ::= struct <id> { (<id>:<type>;)* }
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
// <union-decl> ::= union(<id>) <id> { (<id>:<type>;)* }
// =============================

Top_dec* Parser::parseUnion_dec() {
    match(Token::UNION);
    expect(Token::LPAREN, "Se esperaba '(' en union");
    expect(Token::ID, "Se esperaba tag enum en union");
    string tag = previous->text;
    expect(Token::RPAREN, "Se esperaba ')' en union");

    expect(Token::ID, "Se esperaba nombre de union");
    string nombre = previous->text;

    // Reutilizamos Structdec para la union (AST no tiene UnionDec específico a nivel top)
    // Usamos VarDec con tipo UnionType
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

    // Lo envolvemos en una VarDec cuyo tipo es el UnionType
    VarDec* vd = new VarDec();
    vd->nombre    = nombre;
    vd->tipo      = ut;
    vd->tienetipo = true;
    vd->exp       = nullptr;
    return vd;
}

// =============================
// <fn-decl> ::= pub fn <id> ([<param-list>]) <type> { <block> }
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
    f->tipo   = parseType();
    f->cuerpo = parseBody();
    return f;
}

// =============================
// Helper: parsea lista de parámetros en vectores
// =============================

void Parser::parseParamList(vector<string>& ids, vector<Type*>& tipos) {
    if (check(Token::ID) || check(Token::COMPTIME)) {
        // comptime param
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
// Nota: la gramática original usaba [ ] para bloques,
// pero el AST y el switch sugieren { }. Se usa LBRACE/RBRACE.
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

    // --- var local ---
    if (check(Token::VAR)) {
        VarDec* vd = (VarDec*)parseVar_dec();
        match(Token::SEMICOL);
        // Wrapeamos VarDec en un AsignStmt con exp directa
        // (el AST no tiene VarDecStmt, pero podemos crear uno ad-hoc)
        // Se devuelve como BodyStmt con cuerpo vacío y la var colgada;
        // La forma más limpia es reutilizar AsignStmt:
        AsignStmt* a = new AsignStmt(vd->nombre, vd->exp);
        delete vd;   // liberamos el wrapper
        return a;
    }

    // --- const local ---
    if (check(Token::CONST)) {
        ConstDec* cd = (ConstDec*)parseConts_dec();
        match(Token::SEMICOL);
        AsignStmt* a = new AsignStmt(cd->nombre, cd->exp);
        delete cd;
        return a;
    }

    // --- if ---
    if (match(Token::IF)) {
        expect(Token::LPAREN, "Se esperaba '(' en if");
        Exp* condicion = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en if");
        expect(Token::THEN, "Se esperaba 'then' en if");
        Body* if_cuerpo = parseBody();

        if (match(Token::ELSE)) {
            match(Token::THEN);   // 'else then' según la gramática
            Body* else_cuerpo = parseBody();
            return new IfStmt(condicion, if_cuerpo, else_cuerpo, true);
        }
        return new IfStmt(condicion, if_cuerpo, nullptr, false);
    }

    // --- while ---
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

    // --- for ---
    if (match(Token::FOR)) {
        expect(Token::LPAREN, "Se esperaba '(' en for");
        Stmt* asignacion = parseStmt();          // assign-stmt (incluye ';')
        Exp*  condicion  = parseLogicExp();
        expect(Token::SEMICOL, "Se esperaba ';' en for");
        Stmt* incremento = parseStmt();          // assign-stmt (incluye ';')
        expect(Token::RPAREN, "Se esperaba ')' en for");
        Body* cuerpo = parseBody();
        return new ForStmt(asignacion, condicion, incremento, cuerpo);
    }

    // --- return ---
    if (match(Token::RETURN)) {
        if (!check(Token::SEMICOL)) {
            Exp* exp = parseLogicExp();
            match(Token::SEMICOL);
            return new ReturnStm(exp);
        }
        match(Token::SEMICOL);
        return new ReturnStm();
    }

    // --- print ---
    if (match(Token::PRINT)) {
        expect(Token::LPAREN, "Se esperaba '(' en print");
        Exp* exp = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en print");
        expect(Token::SEMICOL, "Se esperaba ';' después de print");
        return new PrintStmt(exp);
    }

    // --- delete ---
    if (match(Token::DELETE)) {
        Exp* exp = parseLogicExp();
        expect(Token::SEMICOL, "Se esperaba ';' después de delete");
        return new DeleteStm(exp);
    }

    // --- break ---
    if (match(Token::BREAK)) {
        // break : <ident> <expr> ;  (break con valor, extensión Zig)
        if (match(Token::DOSPUNTOS)) {
            expect(Token::ID, "Se esperaba label en break");
            // Ignoramos el label por ahora, parseamos el valor
            Exp* valor = parseLogicExp();
            match(Token::SEMICOL);
            return new BreakStmt(valor);
        }
        match(Token::SEMICOL);
        return new BreakStmt();
    }

    // --- continue ---
    if (match(Token::CONTINUE)) {
        match(Token::SEMICOL);
        return new ContinueStm();
    }

    // --- defer ---
    if (match(Token::DEFER)) {
        Stmt* inner = parseStmt();
        return new DeferStmt(inner);
    }

    // --- comptime block ---
    if (match(Token::COMPTIME)) {
        Body* b = parseBody();
        return new BodyStmt(b);
    }

    // --- switch ---
    if (match(Token::SWITCH)) {
        expect(Token::LPAREN, "Se esperaba '(' en switch");
        Exp* condicion = parseLogicExp();
        expect(Token::RPAREN, "Se esperaba ')' en switch");
        expect(Token::LBRACE, "Se esperaba '{' en switch");

        SwitchStmt* sw = new SwitchStmt(condicion);
        while (!check(Token::RBRACE) && !isAtEnd()) {
            if (match(Token::ELSE)) {
                // else => { <block> }
                // '=>' se escanea como ASSIGN ('=') + MAYOR ('>')  por separado
                expect(Token::ASSIGN, "Se esperaba '=' de '=>' en case else");
                expect(Token::MAYOR,  "Se esperaba '>' de '=>' en case else");
                sw->default_caso = parseBody();
            } else {
                // <switch-pattern> => <block> ,
                Exp* patron = parseLogicExp();
                expect(Token::ASSIGN, "Se esperaba '=' de '=>' en case");
                expect(Token::MAYOR,  "Se esperaba '>' de '=>' en case");
                Body* caso_body = parseBody();
                sw->casos.push_back({patron, caso_body});
            }
            match(Token::COMA);
        }
        expect(Token::RBRACE, "Se esperaba '}' al cerrar switch");
        return sw;
    }

    // --- try / catch ---
    // Sintaxis: try <expr> catch |<ident>| { <block> }
    // No hay ';' entre expr y catch; ';' solo si no hay catch.
    if (match(Token::TRY)) {
        Exp* expr = parseLogicExp();  // incluye llamadas f(a,b) via parsePostfix

        Body* try_body   = nullptr;
        Body* catch_body = nullptr;
        string error_var;

        if (match(Token::CATCH)) {
            // catch |<ident>| { <block> }
            // El scanner no tiene token para '|' solo; usa REFERENCIA para '&'.
            // En la práctica el input tiene |err| donde '|' produce Token::ERR
            // si no está mapeado. Intentamos consumir el id directamente:
            if (match(Token::PIPE)) {
                // '|' escaneado como PIPE (ver parche en scanner + token.h)
                if (check(Token::ID)) {
                    match(Token::ID);
                    error_var = previous->text;
                }
                match(Token::PIPE);        // cierre '|'
            } else if (check(Token::ID)) {
                // fallback: catch sin delimitadores
                match(Token::ID);
                error_var = previous->text;
            }
            catch_body = parseBody();
        } else {
            match(Token::SEMICOL);  // try sin catch termina en ';'
        }
        return new TryStmt(expr, try_body, catch_body, error_var);
    }

    // --- bloque anidado ---
    if (check(Token::LBRACE)) {
        Body* cuerpo = parseBody();
        return new BodyStmt(cuerpo);
    }

    // --- labeled block: <ident> : { ... } ---
    // Necesitamos distinguir  "ident :" (label) de "ident = " (assign)
    // Si el token es ID y el siguiente es DOSPUNTOS → labeled block
    if (check(Token::ID)) {
        // Miramos sin consumir: guardamos estado
        Token* saved_current  = current;
        // Consumimos el ID temporalmente
        advance();                      // ID consumido
        string nombre = previous->text;

        if (match(Token::DOSPUNTOS)) {
            // Puede ser labeled block o variable local con tipo
            if (check(Token::LBRACE)) {
                // Es labeled block
                Body* b = parseBody();
                return new BodyStmt(b);
            }
            // Si no, es declaración con tipo — no debería llegar aquí en stmt
            // pero lo manejamos como error
            throw runtime_error("Error sintáctico: ':' inesperado en stmt");
        }

        // Si no es ':', debe ser '=' → assign-stmt
        if (match(Token::ASSIGN)) {
            Exp* exp = parseLogicExp();
            match(Token::SEMICOL);
            return new AsignStmt(nombre, exp);
        }

        // Llamada a función como stmt: ident(...);
        if (match(Token::LPAREN)) {
            FcallExp* call = new FcallExp();
            call->nombre = nombre;
            if (!check(Token::RPAREN)) {
                call->argumentos.push_back(parseLogicExp());
                while (match(Token::COMA))
                    call->argumentos.push_back(parseLogicExp());
            }
            expect(Token::RPAREN, "Se esperaba ')' en llamada a función");
            match(Token::SEMICOL);
            // Envolvemos la llamada en AsignStmt con variable temporal "__call__"
            // ya que el AST no tiene ExprStmt. El visitor puede ignorar el nombre.
            return new AsignStmt("__call__", call);
        }

        // Si llegamos aquí, el token después del ID no es ':', '=' ni '('
        throw runtime_error("Error sintáctico: se esperaba '=', '(' o '.' después de '" + nombre + "'");
    }

    throw runtime_error("Error sintáctico: sentencia inesperada");
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

    // ! <type>  →  error union sin nombre
    if (match(Token::NOT)) {
        Type* inner = parseType();
        return new ErrorType(inner);
    }

    // * <type>  →  pointer
    if (match(Token::STAR)) {
        Type* inner = parseType();
        return new PointerType(inner);
    }

    // [ <expr> ] <type>  o  [ <expr> ][ <expr> ] <type>
    if (match(Token::LBRACE)) {
        // Realmente el array usa corchetes [ ], pero LBRACE es {
        // La gramática usa [expr] para arrays → LCORCHETE
        // (ver scanner: LCORCHETE = '[')
        // Este caso no debería dispararse aquí; lo manejamos abajo
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

    // error { <ident>, ... }  →  error set
    if (match(Token::ERROR)) {
        if (match(Token::LBRACE)) {
            // error set como tipo anónimo; lo representamos con ErrorType sobre IdType vacío
            // Consumimos los identificadores
            while (!check(Token::RBRACE) && !isAtEnd()) {
                match(Token::ID);
                match(Token::COMA);
            }
            expect(Token::RBRACE, "Se esperaba '}' en error set");
            return new ErrorType(new IdType("error"));
        }
        // error sin llaves: solo la palabra clave como tipo base
        return new IdType("error");
    }

    // <ident> [ ! <type> ]  →  MyError!T
    if (match(Token::ID)) {
        string id = previous->text;
        if (match(Token::NOT)) {
            // Es error-union: <ident>!<type>
            Type* inner = parseType();
            return new ErrorType(inner);  // ErrorType guarda el tipo ok; el nombre se pierde
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
        // TryExp no existe en el AST; lo envolvemos en un UnaryExp custom
        // Usamos FcallExp con nombre "__try__" como workaround
        FcallExp* f = new FcallExp();
        f->nombre = "__try__";
        f->argumentos.push_back(e);
        return f;
    }
    if (match(Token::COMPTIME)) {
        return parseLogicExp();  // comptime expr se evalúa igual
    }
    return parseLogicExp();
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
           check(Token::MAYOR)      || check(Token::MAYORIGUAL) ||
           check(Token::MENOR)      || check(Token::MENORIGUAL)) {

        BinaryOp op;
        if      (match(Token::IGUALIGUAL))  op = IGUALIGUAL;
        else if (match(Token::DIFERENTE))   op = DIFERENTE_OP;
        else if (match(Token::MAYOR))       op = MAYOR;
        else if (match(Token::MAYORIGUAL))  op = MAYORI;
        else if (match(Token::MENOR))       op = MENOR;
        else { match(Token::MENORIGUAL);    op = MENORI; }

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

// <mul-expr> ::= <unary-expr> { * | / | % <unary-expr> }*
Exp* Parser::parseMulExp() {
    Exp* l = parseUnaryExp();
    while (check(Token::STAR) || check(Token::DIV) || check(Token::MODULO)) {
        BinaryOp op;
        if      (match(Token::STAR))   op = MUL_OP;
        else if (match(Token::DIV))    op = DIV_OP;
        else { match(Token::MODULO);   op = MODULO_OP; }
        Exp* r = parseUnaryExp();
        l = new BinaryExp(l, r, op);
    }
    return l;
}

// <unary-expr> ::= { ! | & | * } <postfix-expr>
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
                vector<Exp*> args;
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
        } else if (match(Token::OR)) {
            // orelse (el scanner mapea orelse → OR)
            // Detectamos si el previous fue "orelse" revisando el texto
            // Como OR y && comparten token, verificamos el texto anterior
            // En este punto previous->text == "orelse" si vino de esa rama del scanner
            Exp* fallback = parseLogicExp();
            // Representamos orelse como BinaryExp con op OR
            e = new BinaryExp(e, fallback, OR);
        } else if (match(Token::LPAREN)) {
            // Llamada a función: f(args)
            FcallExp* call = new FcallExp();
            // Extraemos el nombre si e es IdExp
            if (IdExp* id = dynamic_cast<IdExp*>(e)) {
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

    // Números
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
        vector<Type*>  tipos;
        parseParamList(ids, tipos);
        expect(Token::RPAREN, "Se esperaba ')' en lambda");
        expect(Token::DOSPUNTOS, "Se esperaba ':' en lambda");
        Type* ret  = parseType();
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