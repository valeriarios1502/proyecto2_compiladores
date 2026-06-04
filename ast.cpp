#include "ast.h"
#include <iostream>

using namespace std;

// ==================== Exp ====================

Exp::~Exp() {}

string Exp::binopToChar(BinaryOp op) {
    switch (op) {
        case PLUS_OP:      return "+";
        case MINUS_OP:     return "-";
        case MUL_OP:       return "*";
        case DIV_OP:       return "/";
        case MODULO_OP:    return "%";
        case MENORI:       return "<=";
        case MENOR:        return "<";
        case MAYORI:       return ">=";
        case MAYOR:        return ">";
        case IGUALIGUAL:   return "==";
        case DIFERENTE_OP: return "!=";
        case REFERENCIA:   return "&";
        case NOT:          return "!";
        case AND:          return "&&";
        case OR:           return "||";
        default:           return "?";
    }
}

// ── BinaryExp ────────────────────────────────────────────────
BinaryExp::BinaryExp(Exp* l, Exp* r, BinaryOp o) : left(l), right(r), op(o) {}
BinaryExp::~BinaryExp() { delete left; delete right; }
int BinaryExp::accept(Visitor* visitor) { return 0; }

// ── NumberExpDecimal ─────────────────────────────────────────
NumberExpDecimal::NumberExpDecimal(int v) : value(v) {}
NumberExpDecimal::~NumberExpDecimal() {}
int NumberExpDecimal::accept(Visitor* visitor) { return 0; }

// ── NumberExpFlotante ────────────────────────────────────────
NumberExpFlotante::NumberExpFlotante(float v) : value(v) {}
NumberExpFlotante::~NumberExpFlotante() {}
int NumberExpFlotante::accept(Visitor* visitor) { return 0; }

// ── StringExp ────────────────────────────────────────────────
StringExp::StringExp(string v) : valor(v) {}
StringExp::~StringExp() {}
int StringExp::accept(Visitor* visitor) { return 0; }

// ── CharExp ──────────────────────────────────────────────────
CharExp::CharExp(char v) : valor(v) {}
CharExp::~CharExp() {}
int CharExp::accept(Visitor* visitor) { return 0; }   // FIX: era void

// ── IdExp ────────────────────────────────────────────────────
IdExp::IdExp(string v) : value(v) {}
IdExp::~IdExp() {}
int IdExp::accept(Visitor* visitor) { return 0; }

// ── BoolExp ──────────────────────────────────────────────────
BoolExp::BoolExp() {}
BoolExp::~BoolExp() {}
int BoolExp::accept(Visitor* visitor) { return 0; }   // FIX: era void

// ── NotExp ───────────────────────────────────────────────────
NotExp::NotExp() : exp(nullptr) {}
NotExp::~NotExp() { delete exp; }
int NotExp::accept(Visitor* visitor) { return 0; }    // FIX: faltaba

// ── FcallExp ─────────────────────────────────────────────────
FcallExp::FcallExp() {}
FcallExp::~FcallExp() {
    for (Exp* e : argumentos) delete e;
}
int FcallExp::accept(Visitor* visitor) { return 0; }  // FIX: era void

// ── UnaryExp ─────────────────────────────────────────────────
UnaryExp::UnaryExp(Op op, Exp* exp) : op(op), exp(exp) {}
UnaryExp::~UnaryExp() { delete exp; }
int UnaryExp::accept(Visitor* visitor) { return 0; }

// ── NewExp ───────────────────────────────────────────────────
NewExp::NewExp(Type* tipo) : tipo(tipo) {}
NewExp::~NewExp() { delete tipo; }
int NewExp::accept(Visitor* visitor) { return 0; }

// ── NullExp ──────────────────────────────────────────────────
NullExp::NullExp() {}
NullExp::~NullExp() {}
int NullExp::accept(Visitor* visitor) { return 0; }

// ── UndefinedExp ─────────────────────────────────────────────
UndefinedExp::UndefinedExp() {}
UndefinedExp::~UndefinedExp() {}
int UndefinedExp::accept(Visitor* visitor) { return 0; }

// ── ReferenceExp ─────────────────────────────────────────────
ReferenceExp::ReferenceExp() : exp(nullptr) {}
ReferenceExp::~ReferenceExp() { delete exp; }
int ReferenceExp::accept(Visitor* visitor) { return 0; }  // FIX: faltaba

// ── PunteroExp ───────────────────────────────────────────────
PunteroExp::PunteroExp() : exp(nullptr) {}
PunteroExp::~PunteroExp() { delete exp; }
int PunteroExp::accept(Visitor* visitor) { return 0; }    // FIX: faltaba

// ── AlgoconcorchetesylistaExp ─────────────────────────────────
AlgoconcorchetesylistaExp::AlgoconcorchetesylistaExp(Exp* nombre, vector<Exp*> argumentos)
    : nombre(nombre), argumentos(argumentos) {}
AlgoconcorchetesylistaExp::~AlgoconcorchetesylistaExp() {
    delete nombre;
    for (Exp* e : argumentos) delete e;
}
int AlgoconcorchetesylistaExp::accept(Visitor* visitor) { return 0; }  // FIX: faltaba

// ── AlgoconcorchetesExp ───────────────────────────────────────
AlgoconcorchetesExp::AlgoconcorchetesExp(Exp* nombre, Exp* dentroexp)
    : nombre(nombre), dentroexp(dentroexp) {}
AlgoconcorchetesExp::~AlgoconcorchetesExp() { delete nombre; delete dentroexp; }
int AlgoconcorchetesExp::accept(Visitor* visitor) { return 0; }        // FIX: faltaba

// ── PuntoExp ─────────────────────────────────────────────────
PuntoExp::PuntoExp(Exp* exp, string id) : exp(exp), id(id) {}
PuntoExp::~PuntoExp() { delete exp; }
int PuntoExp::accept(Visitor* visitor) { return 0; }                   // FIX: era void

// ── LambdaExp ────────────────────────────────────────────────
LambdaExp::LambdaExp(Type* tipo, Body* cuerpo,
                     vector<string> id_parametros,
                     vector<Type*>  tipo_parametros)
    : tipo(tipo), cuerpo(cuerpo),
      id_parametros(id_parametros),
      tipo_parametros(tipo_parametros),
      hayparametros(!id_parametros.empty()) {}
LambdaExp::~LambdaExp() {
    delete tipo;
    delete cuerpo;
    for (Type* t : tipo_parametros) delete t;
}
int LambdaExp::accept(Visitor* visitor) { return 0; }  // FIX: era void en ast.h


// ==================== Stmt ====================

Stmt::~Stmt() {}

// ── IfStmt ───────────────────────────────────────────────────
IfStmt::IfStmt(Exp* e, Body* ifcuerpo, Body* elsecuerpo, bool hayelse)
    : condicion(e), cuerpodelif(ifcuerpo), cuerpodelelse(elsecuerpo), hayelse(hayelse) {}
IfStmt::~IfStmt() {
    delete condicion;
    delete cuerpodelif;
    if (hayelse) delete cuerpodelelse;
}
void IfStmt::accept(Visitor* visitor) {}

// ── WhileStmt ────────────────────────────────────────────────
WhileStmt::WhileStmt(Exp* e, list<Stmt*> cuerpo)
    : condicion(e), cuerpodelwhile(cuerpo) {}
WhileStmt::~WhileStmt() {
    delete condicion;
    for (Stmt* s : cuerpodelwhile) delete s;
}
void WhileStmt::accept(Visitor* visitor) {}

// ── BodyStmt ─────────────────────────────────────────────────
BodyStmt::BodyStmt(Body* cuerpo) : cuerpo(cuerpo) {}
BodyStmt::~BodyStmt() { delete cuerpo; }
void BodyStmt::accept(Visitor* visitor) {}

// ── AsignStmt ────────────────────────────────────────────────
AsignStmt::AsignStmt(string texto, Exp* e) : variable(texto), exp(e) {}
AsignStmt::~AsignStmt() { delete exp; }
void AsignStmt::accept(Visitor* visitor) {}

// ── PrintStmt ────────────────────────────────────────────────
PrintStmt::PrintStmt(Exp* e) : exp(e) {}
PrintStmt::~PrintStmt() { delete exp; }
void PrintStmt::accept(Visitor* visitor) {}

// ── ReturnStm ────────────────────────────────────────────────
ReturnStm::ReturnStm(Exp* e) : exp(e) {}
ReturnStm::ReturnStm() : exp(nullptr) {}
ReturnStm::~ReturnStm() { delete exp; }
void ReturnStm::accept(Visitor* visitor) {}

// ── DeleteStm ────────────────────────────────────────────────
DeleteStm::DeleteStm(Exp* e) : exp(e) {}
DeleteStm::~DeleteStm() { delete exp; }
void DeleteStm::accept(Visitor* visitor) {}

// ── ContinueStm ──────────────────────────────────────────────
ContinueStm::ContinueStm() {}
ContinueStm::~ContinueStm() {}
void ContinueStm::accept(Visitor* visitor) {}

// ── BreakStmt ────────────────────────────────────────────────
BreakStmt::BreakStmt() : valor(nullptr), tiene_valor(false) {}
BreakStmt::BreakStmt(Exp* valor) : valor(valor), tiene_valor(true) {}
BreakStmt::~BreakStmt() { if (tiene_valor && valor) delete valor; }
void BreakStmt::accept(Visitor* visitor) {}

// ── SwitchStmt ───────────────────────────────────────────────
SwitchStmt::SwitchStmt(Exp* condicion) : condicion(condicion), default_caso(nullptr) {}
SwitchStmt::~SwitchStmt() {
    delete condicion;
    for (auto& p : casos) { delete p.first; delete p.second; }
    if (default_caso) delete default_caso;
}
void SwitchStmt::accept(Visitor* visitor) {}

// ── TryStmt ──────────────────────────────────────────────────
TryStmt::TryStmt(Exp* expr, Body* try_body, Body* catch_body, string error_var)
    : expr(expr), try_body(try_body), catch_body(catch_body), error_var(error_var) {}
TryStmt::~TryStmt() {
    delete expr;
    if (try_body)   delete try_body;
    if (catch_body) delete catch_body;
}
void TryStmt::accept(Visitor* visitor) {}

// ── DeferStmt ────────────────────────────────────────────────
DeferStmt::DeferStmt(Stmt* stmt) : stmt(stmt) {}
DeferStmt::~DeferStmt() { delete stmt; }
void DeferStmt::accept(Visitor* visitor) {}

// ── ForStmt ──────────────────────────────────────────────────
ForStmt::ForStmt(Stmt* asignacion, Exp* condicion, Stmt* incremento, Body* cuerpo)
    : asignacion(asignacion), condicion(condicion), incremento(incremento), cuerpo(cuerpo) {}
ForStmt::~ForStmt() {
    delete asignacion;
    delete condicion;
    delete incremento;
    delete cuerpo;
}
void ForStmt::accept(Visitor* visitor) {}


// ==================== Top_dec ====================

Top_dec::~Top_dec() {}

// ── Fundec ───────────────────────────────────────────────────
Fundec::Fundec() : tipo(nullptr), cuerpo(nullptr) {}
Fundec::~Fundec() {
    delete tipo;
    delete cuerpo;
    for (Type* t : tipo_parametros) delete t;
}
void Fundec::accept(Visitor* visitor) {}

// ── Structdec ────────────────────────────────────────────────
Structdec::Structdec() {}
Structdec::~Structdec() {
    for (Type* t : tipo_parametros) delete t;
}
void Structdec::accept(Visitor* visitor) {}

// ── VarDec ───────────────────────────────────────────────────
VarDec::VarDec() : tipo(nullptr), exp(nullptr), tienetipo(false) {}
VarDec::~VarDec() {
    if (tienetipo) delete tipo;
    delete exp;
}
void VarDec::accept(Visitor* visitor) {}

// ── ConstDec ─────────────────────────────────────────────────
ConstDec::ConstDec() : tipo(nullptr), exp(nullptr), tienetipo(false) {}
ConstDec::~ConstDec() {
    if (tienetipo) delete tipo;
    delete exp;
}
void ConstDec::accept(Visitor* visitor) {}

// ── Template ─────────────────────────────────────────────────
Template::Template() : tipo(nullptr), block(nullptr) {}
Template::~Template() {
    delete tipo;
    delete block;
    for (Type* t : tipo_parametros) delete t;
}
void Template::accept(Visitor* visitor) {}


// ==================== Type ====================

Type::~Type() {}

// ── IdType ───────────────────────────────────────────────────
IdType::IdType(string id) : id(id) {}
IdType::~IdType() {}
void IdType::accept(Visitor* visitor) {}

// ── PointerType ──────────────────────────────────────────────
PointerType::PointerType(Type* tipo) : tipo(tipo) {}
PointerType::~PointerType() { delete tipo; }
void PointerType::accept(Visitor* visitor) {}

// ── ArrayType ────────────────────────────────────────────────
ArrayType::ArrayType(Exp* exp1, Exp* exp2, Type* tipo, bool existe_exp2)
    : exp1(exp1), exp2(exp2), tipo(tipo), existe_exp2(existe_exp2) {}
ArrayType::~ArrayType() {
    delete exp1;
    if (existe_exp2) delete exp2;
    delete tipo;
}
void ArrayType::accept(Visitor* visitor) {}

// ── OptionalType ─────────────────────────────────────────────
OptionalType::OptionalType(Type* tipo) : tipo(tipo) {}
OptionalType::~OptionalType() { delete tipo; }
void OptionalType::accept(Visitor* visitor) {}

// ── ErrorType ────────────────────────────────────────────────
ErrorType::ErrorType(Type* tipo) : tipo(tipo) {}
ErrorType::~ErrorType() { delete tipo; }
void ErrorType::accept(Visitor* visitor) {}

// ── UnionType ────────────────────────────────────────────────
UnionType::UnionType(string nombre) : nombre(nombre) {}
UnionType::~UnionType() {
    for (Type* t : campo_tipos) delete t;
}
void UnionType::accept(Visitor* visitor) {}

// ── EnumType ─────────────────────────────────────────────────
EnumType::EnumType(string nombre) : nombre(nombre) {}
EnumType::~EnumType() {}
void EnumType::accept(Visitor* visitor) {}


// ==================== Body / Programa ====================

Body::Body() {}
Body::~Body() {
    for (Stmt* s : slist) delete s;
}
void Body::accept(Visitor* visitor) {}   // FIX: faltaba

Programa::Programa() {}
Programa::~Programa() {
    for (Top_dec* d : declist) delete d;
}
void Programa::accept(Visitor* visitor) {}  // FIX: faltaba