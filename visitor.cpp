#include <iostream>
#include <fstream>
#include <cmath>
#include "ast.h"
#include "visitor.h"

using namespace std;

Value BinaryExp::accept(Visitor* visitor)                  { return visitor->visit(this); }
Value NumberExpDecimal::accept(Visitor* visitor)           { return visitor->visit(this); }
Value NumberExpFlotante::accept(Visitor* visitor)          { return visitor->visit(this); }
Value StringExp::accept(Visitor* visitor)                  { return visitor->visit(this); }
Value CharExp::accept(Visitor* visitor)                    { return visitor->visit(this); }
Value IdExp::accept(Visitor* visitor)                      { return visitor->visit(this); }
Value BoolExp::accept(Visitor* visitor)                    { return visitor->visit(this); }
Value NotExp::accept(Visitor* visitor)                     { return visitor->visit(this); }
Value FcallExp::accept(Visitor* visitor)                   { return visitor->visit(this); }
Value UnaryExp::accept(Visitor* visitor)                   { return visitor->visit(this); }
Value NewExp::accept(Visitor* visitor)                     { return visitor->visit(this); }
Value NullExp::accept(Visitor* visitor)                    { return visitor->visit(this); }
Value UndefinedExp::accept(Visitor* visitor)               { return visitor->visit(this); }
Value ReferenceExp::accept(Visitor* visitor)               { return visitor->visit(this); }
Value PunteroExp::accept(Visitor* visitor)                 { return visitor->visit(this); }
Value AlgoconcorchetesylistaExp::accept(Visitor* visitor)  { return visitor->visit(this); }
Value AlgoconcorchetesExp::accept(Visitor* visitor)        { return visitor->visit(this); }
Value PuntoExp::accept(Visitor* visitor)                   { return visitor->visit(this); }
Value LambdaExp::accept(Visitor* visitor)                  { return visitor->visit(this); }

void IfStmt::accept(Visitor* visitor)     { visitor->visit(this); }
void WhileStmt::accept(Visitor* visitor)  { visitor->visit(this); }
void BodyStmt::accept(Visitor* visitor)   { visitor->visit(this); }
void AsignStmt::accept(Visitor* visitor)  { visitor->visit(this); }
void PrintStmt::accept(Visitor* visitor)  { visitor->visit(this); }
void ReturnStm::accept(Visitor* visitor)  { visitor->visit(this); }
void DeleteStm::accept(Visitor* visitor)  { visitor->visit(this); }
void ContinueStm::accept(Visitor* visitor){ visitor->visit(this); }
void BreakStmt::accept(Visitor* visitor)  { visitor->visit(this); }
void SwitchStmt::accept(Visitor* visitor) { visitor->visit(this); }
void TryStmt::accept(Visitor* visitor)    { visitor->visit(this); }
void DeferStmt::accept(Visitor* visitor)  { visitor->visit(this); }
void ForStmt::accept(Visitor* visitor)    { visitor->visit(this); }

void Fundec::accept(Visitor* visitor)    { visitor->visit(this); }
void Structdec::accept(Visitor* visitor) { visitor->visit(this); }
void VarDec::accept(Visitor* visitor)    { visitor->visit(this); }
void ConstDec::accept(Visitor* visitor)  { visitor->visit(this); }
void Template::accept(Visitor* visitor)  { visitor->visit(this); }

void IdType::accept(Visitor* visitor)       { visitor->visit(this); }
void PointerType::accept(Visitor* visitor)  { visitor->visit(this); }
void ArrayType::accept(Visitor* visitor)    { visitor->visit(this); }
void OptionalType::accept(Visitor* visitor) { visitor->visit(this); }
void ErrorType::accept(Visitor* visitor)    { visitor->visit(this); }
void UnionType::accept(Visitor* visitor)    { visitor->visit(this); }
void EnumType::accept(Visitor* visitor)     { visitor->visit(this); }

void Body::accept(Visitor* visitor)     { visitor->visit(this); }
void Programa::accept(Visitor* visitor) { visitor->visit(this); }


///////////////////////////////////////////////////////////////////////////////////
//                    SECCIÓN 3: IMPLEMENTACIÓN DE EVALVisitor
///////////////////////////////////////////////////////////////////////////////////


Value EVALVisitor::visit(BinaryExp* exp) {
    Value v1 = exp->left->accept(this);
    Value v2 = exp->right->accept(this);

    switch (exp->op) {
        case PLUS_OP:
            if (v1.isNumeric() && v2.isNumeric()) {
                if (v1.kind == Value::VAL_FLOAT || v2.kind == Value::VAL_FLOAT) {
                    return Value::makeFloat((v1.kind == Value::VAL_FLOAT ? v1.f : v1.i) + 
                                            (v2.kind == Value::VAL_FLOAT ? v2.f : v2.i));
                } else {
                    return Value::makeInt(v1.i + v2.i);
                }
            }
        case MINUS_OP:
            if (v1.isNumeric() && v2.isNumeric()) {
                if (v1.kind == Value::VAL_FLOAT || v2.kind == Value::VAL_FLOAT) {
                    return Value::makeFloat((v1.kind == Value::VAL_FLOAT ? v1.f : v1.i) - 
                                            (v2.kind == Value::VAL_FLOAT ? v2.f : v2.i));
                } else {
                    return Value::makeInt(v1.i - v2.i);
                }
            }
        case MODULO_OP:
            if (v1.isNumeric() && v2.isNumeric()) {
                return Value::makeInt(v1.i % v2.i);
            }
        case MUL_OP:
            if (v1.isNumeric() && v2.isNumeric()) {
                if (v1.kind == Value::VAL_FLOAT || v2.kind == Value::VAL_FLOAT) {
                    return Value::makeFloat((v1.kind == Value::VAL_FLOAT ? v1.f : v1.i) * 
                                            (v2.kind == Value::VAL_FLOAT ? v2.f : v2.i));
                } else {
                    return Value::makeInt(v1.i * v2.i);
                }
            }
        case DIV_OP:
            if (v1.isNumeric() && v2.isNumeric()) {
                return Value::makeFloat((v1.kind == Value::VAL_FLOAT ? v1.f : v1.i) / 
                                        (v2.kind == Value::VAL_FLOAT ? v2.f : v2.i));
                } 
        case DIFERENTE_OP:
                if (v1.isNumeric() && v2.isNumeric()) {
                    return Value::makeBool(v1.i != v2.i);
                }
                else if (v1.isBool() && v2.isBool()) {
                    return Value::makeBool(v1.i != v2.i);
                }
        case MENORI:
                if (v1.isNumeric() && v2.isNumeric()) {
                    return Value::makeBool(v1.i <= v2.i);
                }
        case MENOR:
                if (v1.isNumeric() && v2.isNumeric()) {
                    return Value::makeBool(v1.i < v2.i);
                }
        case MAYORI:
                if (v1.isNumeric() && v2.isNumeric()) {
                    return Value::makeBool(v1.i >= v2.i);
                }
        case MAYOR:
                if (v1.isNumeric() && v2.isNumeric()) {
                    return Value::makeBool(v1.i > v2.i);
                }
        case IGUALIGUAL:
                if (v1.isNumeric() && v2.isNumeric()) {
                    return Value::makeBool(v1.i == v2.i);
                }
                else if (v1.isBool() && v2.isBool()) {
                    return Value::makeBool(v1.i == v2.i);
                }
        case AND:
                if (v1.isBool() && v2.isBool()) {
                    return Value::makeBool(v1.i && v2.i);
                }
        case OR:
                if (v1.isBool() && v2.isBool()) {
                    return Value::makeBool(v1.i || v2.i);
                }
        default:
            cout << "Operador desconocido" << endl;
            return Value::makeInt(0);
    }
}

Value EVALVisitor::visit(NumberExpDecimal* exp) {
    return Value::makeInt(exp->value);
}

Value EVALVisitor::visit(NumberExpFlotante* exp) {
    return Value::makeFloat(exp->value);
}

Value EVALVisitor::visit(StringExp* exp) {
    return Value::makeString(exp->valor);
}

Value EVALVisitor::visit(CharExp* exp) {
    return Value::makeChar(exp->valor);
}

//revisarlo bn
Value EVALVisitor::visit(IdExp* exp) {
    return env.lookup(exp->value);
}
//revisarlo bn
Value EVALVisitor::visit(BoolExp* exp) {
    return Value::makeBool(exp->booleano == "true");
}

Value EVALVisitor::visit(NotExp* exp) {
    return Value::makeBool(!exp->exp->accept(this).b);
}

Value EVALVisitor::visit(FcallExp* fcall) {
    retcall = false;
    vector<Value> arg;
    for (auto i : fcall->argumentos) {
        arg.push_back(i->accept(this));
    }
    Fundec* fd = envfun[fcall->nombre];
    env.add_level();

    // Cargar los parámetros con sus valores
    for (size_t i = 0; i < arg.size(); ++i) {
        env.add_var(fd->id_parametros[i], arg[i]);
    }

    fd->cuerpo->accept(this);
    env.remove_level();

    if (retcall) {
        return retval;
    } else {
        cout << "Error: la función '" << fcall->nombre << "' no tiene retorno" << endl;
        exit(0);
    }
}




