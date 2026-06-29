#include "ast.h"
#include "visitor.h"
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
Value BinaryExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── NumberExpDecimal ─────────────────────────────────────────
NumberExpDecimal::NumberExpDecimal(int v) : value(v) {}
NumberExpDecimal::~NumberExpDecimal() {}
Value NumberExpDecimal::accept(Visitor* visitor) { return visitor->visit(this); }

// ── NumberExpFlotante ────────────────────────────────────────
NumberExpFlotante::NumberExpFlotante(float v) : value(v) {}
NumberExpFlotante::~NumberExpFlotante() {}
Value NumberExpFlotante::accept(Visitor* visitor) { return visitor->visit(this); }

// ── StringExp ────────────────────────────────────────────────
StringExp::StringExp(string v) : valor(v) {}
StringExp::~StringExp() {}
Value StringExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── CharExp ──────────────────────────────────────────────────
CharExp::CharExp(char v) : valor(v) {}
CharExp::~CharExp() {}
Value CharExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── IdExp ────────────────────────────────────────────────────
IdExp::IdExp(string v) : value(v) {}
IdExp::~IdExp() {}
Value IdExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── BoolExp ──────────────────────────────────────────────────
BoolExp::BoolExp() {}
BoolExp::~BoolExp() {}
Value BoolExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── NotExp ───────────────────────────────────────────────────
NotExp::NotExp() : exp(nullptr) {}
NotExp::~NotExp() { delete exp; }
Value NotExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── FcallExp ─────────────────────────────────────────────────
FcallExp::FcallExp() {}
FcallExp::~FcallExp() {
    for (Exp* e : argumentos) delete e;
}
Value FcallExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── UnaryExp ─────────────────────────────────────────────────
UnaryExp::UnaryExp(Op op, Exp *exp) : op(op), exp(exp) {}
UnaryExp::~UnaryExp() { delete exp; }
Value UnaryExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── NewExp ───────────────────────────────────────────────────
NewExp::NewExp(Type *tipo) : tipo(tipo) {}
NewExp::~NewExp() { delete tipo; }
Value NewExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── NullExp ──────────────────────────────────────────────────
NullExp::NullExp() {}
NullExp::~NullExp() {}
Value NullExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── UndefinedExp ─────────────────────────────────────────────
UndefinedExp::UndefinedExp() {}
UndefinedExp::~UndefinedExp() {}
Value UndefinedExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── ReferenceExp ─────────────────────────────────────────────
ReferenceExp::ReferenceExp() : exp(nullptr) {}
ReferenceExp::~ReferenceExp() { delete exp; }
Value ReferenceExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── PunteroExp ───────────────────────────────────────────────
PunteroExp::PunteroExp() : exp(nullptr) {}
PunteroExp::~PunteroExp() { delete exp; }
Value PunteroExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── AlgoconcorchetesylistaExp ─────────────────────────────────
AlgoconcorchetesylistaExp::AlgoconcorchetesylistaExp(Exp* nombre, vector<Exp*> argumentos)
    : nombre(nombre), argumentos(argumentos) {}
AlgoconcorchetesylistaExp::~AlgoconcorchetesylistaExp() {
    delete nombre;
    for (Exp* e : argumentos) delete e;
}
Value AlgoconcorchetesylistaExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── AlgoconcorchetesExp ───────────────────────────────────────
AlgoconcorchetesExp::AlgoconcorchetesExp(Exp* nombre, Exp* dentroexp)
    : nombre(nombre), dentroexp(dentroexp) {}
AlgoconcorchetesExp::~AlgoconcorchetesExp() { delete nombre; delete dentroexp; }
Value AlgoconcorchetesExp::accept(Visitor* visitor) { return visitor->visit(this); }

// ── PuntoExp ─────────────────────────────────────────────────
PuntoExp::PuntoExp(Exp* exp, string id) : exp(exp), id(id) {}
PuntoExp::~PuntoExp() { delete exp; }
Value PuntoExp::accept(Visitor* visitor) { return visitor->visit(this); }

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
Value LambdaExp::accept(Visitor* visitor) { return visitor->visit(this); }

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
void IfStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── WhileStmt ────────────────────────────────────────────────
WhileStmt::WhileStmt(Exp* e, list<Stmt*> cuerpo)
    : condicion(e), cuerpodelwhile(cuerpo) {}
WhileStmt::~WhileStmt() {
    delete condicion;
    for (Stmt* s : cuerpodelwhile) delete s;
}
void WhileStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── BodyStmt ─────────────────────────────────────────────────
BodyStmt::BodyStmt(Body* cuerpo) : cuerpo(cuerpo) {}
BodyStmt::~BodyStmt() { delete cuerpo; }
void BodyStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── AsignStmt ────────────────────────────────────────────────
AsignStmt::AsignStmt(string texto, Exp* e) : variable(texto), exp(e) {}
AsignStmt::~AsignStmt() { delete exp; }
void AsignStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── PrintStmt ────────────────────────────────────────────────
PrintStmt::PrintStmt(Exp* e) : exp(e) {}
PrintStmt::~PrintStmt() { delete exp; }
void PrintStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── ReturnStm ────────────────────────────────────────────────
ReturnStm::ReturnStm(Exp* e) : exp(e) {}
ReturnStm::ReturnStm() : exp(nullptr) {}
ReturnStm::~ReturnStm() { delete exp; }
void ReturnStm::accept(Visitor* visitor) { visitor->visit(this); }

// ── DeleteStm ────────────────────────────────────────────────
DeleteStm::DeleteStm(Exp* e) : exp(e) {}
DeleteStm::~DeleteStm() { delete exp; }
void DeleteStm::accept(Visitor* visitor) { visitor->visit(this); }

// ── ContinueStm ──────────────────────────────────────────────
ContinueStm::ContinueStm() {}
ContinueStm::~ContinueStm() {}
void ContinueStm::accept(Visitor* visitor) { visitor->visit(this); }

// ── BreakStmt ────────────────────────────────────────────────
BreakStmt::BreakStmt() : valor(nullptr), tiene_valor(false) {}
BreakStmt::BreakStmt(Exp* valor) : valor(valor), tiene_valor(true) {}
BreakStmt::~BreakStmt() { if (tiene_valor && valor) delete valor; }
void BreakStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── SwitchStmt ───────────────────────────────────────────────
SwitchStmt::SwitchStmt(Exp* condicion) : condicion(condicion), default_caso(nullptr) {}
SwitchStmt::~SwitchStmt() {
    delete condicion;
    for (auto& p : casos) { delete p.first; delete p.second; }
    if (default_caso) delete default_caso;
}
void SwitchStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── TryStmt ──────────────────────────────────────────────────
TryStmt::TryStmt(Exp* expr, Body* try_body, Body* catch_body, string error_var)
    : expr(expr), try_body(try_body), catch_body(catch_body), error_var(error_var) {}
TryStmt::~TryStmt() {
    delete expr;
    if (try_body)   delete try_body;
    if (catch_body) delete catch_body;
}
void TryStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── DeferStmt ────────────────────────────────────────────────
DeferStmt::DeferStmt(Stmt* stmt) : stmt(stmt) {}
DeferStmt::~DeferStmt() { delete stmt; }
void DeferStmt::accept(Visitor* visitor) { visitor->visit(this); }

// ── ForStmt ──────────────────────────────────────────────────
ForStmt::ForStmt(Stmt* asignacion, Exp* condicion, Stmt* incremento, Body* cuerpo)
    : asignacion(asignacion), condicion(condicion), incremento(incremento), cuerpo(cuerpo) {}
ForStmt::~ForStmt() {
    delete asignacion;
    delete condicion;
    delete incremento;
    delete cuerpo;
}
void ForStmt::accept(Visitor* visitor) { visitor->visit(this); }

void DerefAssignStmt::accept(Visitor* v) {
    v->visit(this);
}

// ==================== Top_dec ====================

Top_dec::~Top_dec() {}

// ── Fundec ───────────────────────────────────────────────────
Fundec::Fundec() : tipo(nullptr), cuerpo(nullptr) {}
Fundec::~Fundec() {
    delete tipo;
    delete cuerpo;
    for (Type* t : tipo_parametros) delete t;
}
void Fundec::accept(Visitor* visitor) { visitor->visit(this); }

// ── Structdec ────────────────────────────────────────────────
Structdec::Structdec() {}
Structdec::~Structdec() {
    for (Type* t : tipo_parametros) delete t;
}
void Structdec::accept(Visitor* visitor) { visitor->visit(this); }

// ── VarDec ───────────────────────────────────────────────────
VarDec::VarDec() : tipo(nullptr), exp(nullptr), tienetipo(false) {}
VarDec::~VarDec() {
    if (tienetipo) delete tipo;
    delete exp;
}
void VarDec::accept(Visitor* visitor) { visitor->visit(this); }

// ── ConstDec ─────────────────────────────────────────────────
ConstDec::ConstDec() : tipo(nullptr), exp(nullptr), tienetipo(false) {}
ConstDec::~ConstDec() {
    if (tienetipo) delete tipo;
    delete exp;
}
void ConstDec::accept(Visitor* visitor) { visitor->visit(this); }

// ── Template ─────────────────────────────────────────────────
Template::Template() : tipo(nullptr), block(nullptr) {}
Template::~Template() {
    delete tipo;
    delete block;
    for (Type* t : tipo_parametros) delete t;
}
void Template::accept(Visitor* visitor) { visitor->visit(this); }

// ==================== Type ====================

Type::~Type() {}

// ── IdType ───────────────────────────────────────────────────
IdType::IdType(string id) : id(id) {}
IdType::~IdType() {}
void IdType::accept(Visitor* visitor) { visitor->visit(this); }

// ── PointerType ──────────────────────────────────────────────
PointerType::PointerType(Type* tipo) : tipo(tipo) {}
PointerType::~PointerType() { delete tipo; }
void PointerType::accept(Visitor* visitor) { visitor->visit(this); }

// ── ArrayType ────────────────────────────────────────────────
ArrayType::ArrayType(Exp* exp1, Exp* exp2, Type* tipo, bool existe_exp2)
    : exp1(exp1), exp2(exp2), tipo(tipo), existe_exp2(existe_exp2) {}
ArrayType::~ArrayType() {
    delete exp1;
    if (existe_exp2) delete exp2;
    delete tipo;
}
void ArrayType::accept(Visitor* visitor) { visitor->visit(this); }

// ── OptionalType ─────────────────────────────────────────────
OptionalType::OptionalType(Type* tipo) : tipo(tipo) {}
OptionalType::~OptionalType() { delete tipo; }
void OptionalType::accept(Visitor* visitor) { visitor->visit(this); }

// ── ErrorType ────────────────────────────────────────────────
ErrorType::ErrorType(Type* tipo) : tipo(tipo) {}
ErrorType::~ErrorType() { delete tipo; }
void ErrorType::accept(Visitor* visitor) { visitor->visit(this); }

// ── UnionType ────────────────────────────────────────────────
UnionType::UnionType(string nombre) : nombre(nombre) {}
UnionType::~UnionType() {
    for (Type* t : campo_tipos) delete t;
}
void UnionType::accept(Visitor* visitor) { visitor->visit(this); }

// ── EnumType ─────────────────────────────────────────────────
EnumType::EnumType(string nombre) : nombre(nombre) {}
EnumType::~EnumType() {}
void EnumType::accept(Visitor* visitor) { visitor->visit(this); }

// ==================== Body / Programa ====================

Body::Body() {}
Body::~Body() {
    for (Stmt* s : slist) delete s;
}
void Body::accept(Visitor* visitor) { visitor->visit(this); }

Programa::Programa() {}
Programa::~Programa() {
    for (Top_dec* d : declist) delete d;
}
void Programa::accept(Visitor* visitor) { visitor->visit(this); }