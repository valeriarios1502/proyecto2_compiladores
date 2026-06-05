#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cmath>
#include "ast.h"
#include "visitor.h"

using namespace std;



static void runtimeError(const string& message) {
    cerr << "[Error runtime] " << message << endl;
    exit(EXIT_FAILURE);
}

static bool isTruthy(const Value& value) {
    if (!value.isBool()) {
        runtimeError("se esperaba una expresion booleana");
    }
    return value.b;
}

static double numberValue(const Value& value) {
    return value.kind == Value::VAL_FLOAT ? value.f : value.i;
}

static bool valuesEqual(const Value& v1, const Value& v2) {
    if (v1.isNumeric() && v2.isNumeric()) {
        return numberValue(v1) == numberValue(v2);
    }

    if (v1.kind != v2.kind) {
        return false;
    }

    switch (v1.kind) {
        case Value::VAL_BOOL:      return v1.b == v2.b;
        case Value::VAL_STRING:    return v1.s == v2.s;
        case Value::VAL_CHAR:      return v1.c == v2.c;
        case Value::VAL_NULL:
        case Value::VAL_UNDEFINED:
        case Value::VAL_VOID:      return true;
        case Value::VAL_INT:       return v1.i == v2.i;
        case Value::VAL_FLOAT:     return v1.f == v2.f;
    }

    return false;
}

static Value numericBinary(const Value& v1, const Value& v2, BinaryOp op) {
    if (!v1.isNumeric() || !v2.isNumeric()) {
        runtimeError("operador numerico aplicado a tipos incompatibles");
    }

    if (op == MODULO_OP) {
        if (v1.kind != Value::VAL_INT || v2.kind != Value::VAL_INT) {
            runtimeError("el operador % solo acepta enteros");
        }
        if (v2.i == 0) {
            runtimeError("division por cero en operador %");
        }
        return Value::makeInt(v1.i % v2.i);
    }

    bool useFloat = op == DIV_OP || v1.kind == Value::VAL_FLOAT || v2.kind == Value::VAL_FLOAT;
    double n1 = numberValue(v1);
    double n2 = numberValue(v2);

    if (op == DIV_OP && n2 == 0) {
        runtimeError("division por cero");
    }

    switch (op) {
        case PLUS_OP:
            return useFloat ? Value::makeFloat(n1 + n2) : Value::makeInt(v1.i + v2.i);
        case MINUS_OP:
            return useFloat ? Value::makeFloat(n1 - n2) : Value::makeInt(v1.i - v2.i);
        case MUL_OP:
            return useFloat ? Value::makeFloat(n1 * n2) : Value::makeInt(v1.i * v2.i);
        case DIV_OP:
            return Value::makeFloat(n1 / n2);
        default:
            runtimeError("operador numerico no soportado");
    }

    return Value();
}


///////////////////////////////////////////////////////////////////////////////////
//                    SECCION 3: IMPLEMENTACION DE EVALVisitor
///////////////////////////////////////////////////////////////////////////////////


Value EVALVisitor::visit(BinaryExp* exp) {
    if (exp->op == AND) {
        Value v1 = exp->left->accept(this);
        return Value::makeBool(isTruthy(v1) && isTruthy(exp->right->accept(this)));
    }

    if (exp->op == OR) {
        Value v1 = exp->left->accept(this);
        return Value::makeBool(isTruthy(v1) || isTruthy(exp->right->accept(this)));
    }

    Value v1 = exp->left->accept(this);
    Value v2 = exp->right->accept(this);

    switch (exp->op) {
        case PLUS_OP:
            if (v1.kind == Value::VAL_STRING || v2.kind == Value::VAL_STRING) {
                return Value::makeString(v1.toString() + v2.toString());
            }
            return numericBinary(v1, v2, exp->op);
        case MINUS_OP:
        case MODULO_OP:
        case MUL_OP:
        case DIV_OP:
            return numericBinary(v1, v2, exp->op);
        case DIFERENTE_OP:
            return Value::makeBool(!valuesEqual(v1, v2));
        case MENORI:
            if (v1.isNumeric() && v2.isNumeric()) {
                return Value::makeBool(numberValue(v1) <= numberValue(v2));
            }
            runtimeError("<= requiere numeros");
        case MENOR:
            if (v1.isNumeric() && v2.isNumeric()) {
                return Value::makeBool(numberValue(v1) < numberValue(v2));
            }
            runtimeError("< requiere numeros");
        case MAYORI:
            if (v1.isNumeric() && v2.isNumeric()) {
                return Value::makeBool(numberValue(v1) >= numberValue(v2));
            }
            runtimeError(">= requiere numeros");
        case MAYOR:
            if (v1.isNumeric() && v2.isNumeric()) {
                return Value::makeBool(numberValue(v1) > numberValue(v2));
            }
            runtimeError("> requiere numeros");
        case IGUALIGUAL:
            return Value::makeBool(valuesEqual(v1, v2));
        default:
            runtimeError("operador binario no soportado");
    }

    return Value();
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

Value EVALVisitor::visit(IdExp* exp) {
    if (!env.check(exp->value)) {
        runtimeError("variable no definida: " + exp->value);
    }
    return env.lookup(exp->value);
}

Value EVALVisitor::visit(BoolExp* exp) {
    return Value::makeBool(exp->booleano == "true");
}

Value EVALVisitor::visit(NotExp* exp) {
    return Value::makeBool(!isTruthy(exp->exp->accept(this)));
}

Value EVALVisitor::visit(FcallExp* fcall) {
    auto it = envfun.find(fcall->nombre);
    if (it == envfun.end() || it->second == nullptr) {
        runtimeError("funcion no definida: " + fcall->nombre);
    }

    Fundec* fd = it->second;
    if (fd->id_parametros.size() != fcall->argumentos.size()) {
        runtimeError("numero incorrecto de argumentos en llamada a " + fcall->nombre);
    }

    vector<Value> arg;
    for (Exp* i : fcall->argumentos) {
        arg.push_back(i->accept(this));
    }

    bool retcallAnterior = retcall;
    Value retvalAnterior = retval;
    retcall = false;
    retval = Value();

    env.add_level();

    // Cargar los parametros con sus valores
    for (size_t i = 0; i < arg.size(); ++i) {
        env.add_var(fd->id_parametros[i], arg[i]);
    }

    fd->cuerpo->accept(this);
    env.remove_level();

    Value result = retcall ? retval : Value();
    retcall = retcallAnterior;
    retval = retvalAnterior;
    return result;
}

Value EVALVisitor::visit(UnaryExp* exp) {
    Value value = exp->exp->accept(this);

    switch (exp->op) {
        case UnaryExp::NEGATE:
            if (!value.isNumeric()) {
                runtimeError("el operador - requiere un numero");
            }
            return value.kind == Value::VAL_FLOAT ? Value::makeFloat(-value.f) : Value::makeInt(-value.i);
        case UnaryExp::NOT_OP:
            return Value::makeBool(!isTruthy(value));
        case UnaryExp::ADDRESS:
        case UnaryExp::DEREF:
            return value;
    }

    return Value();
}

Value EVALVisitor::visit(NewExp* exp) {
    return Value::makeUndefined();
}

Value EVALVisitor::visit(NullExp* exp) {
    return Value::makeNull();
}

Value EVALVisitor::visit(UndefinedExp* exp) {
    return Value::makeUndefined();
}

Value EVALVisitor::visit(ReferenceExp* exp) {
    return exp->exp ? exp->exp->accept(this) : Value::makeUndefined();
}

Value EVALVisitor::visit(PunteroExp* exp) {
    return exp->exp ? exp->exp->accept(this) : Value::makeUndefined();
}

Value EVALVisitor::visit(AlgoconcorchetesylistaExp* exp) {
    runtimeError("indexacion con lista no implementada en EVALVisitor");
    return Value();
}

Value EVALVisitor::visit(AlgoconcorchetesExp* exp) {
    runtimeError("indexacion no implementada en EVALVisitor");
    return Value();
}

Value EVALVisitor::visit(PuntoExp* exp) {
    if (exp->id == "__unwrap__") {
        Value value = exp->exp->accept(this);
        if (value.kind == Value::VAL_NULL || value.kind == Value::VAL_UNDEFINED) {
            runtimeError("no se puede desempaquetar null/undefined");
        }
        return value;
    }

    runtimeError("acceso por punto no implementado en EVALVisitor");
    return Value();
}

Value EVALVisitor::visit(LambdaExp* exp) {
    return Value::makeUndefined();
}

void EVALVisitor::visit(IfStmt* stm) {
    if (isTruthy(stm->condicion->accept(this))) {
        stm->cuerpodelif->accept(this);
    } else if (stm->hayelse && stm->cuerpodelelse != nullptr) {
        stm->cuerpodelelse->accept(this);
    }
}

void EVALVisitor::visit(WhileStmt* stm) {
    while (isTruthy(stm->condicion->accept(this))) {
        env.add_level();
        for (Stmt* stmt : stm->cuerpodelwhile) {
            stmt->accept(this);
            if (retcall || breakcall || continuecall) {
                break;
            }
        }
        env.remove_level();

        if (retcall) {
            return;
        }
        if (breakcall) {
            breakcall = false;
            return;
        }
        if (continuecall) {
            continuecall = false;
        }
    }
}

void EVALVisitor::visit(BodyStmt* stm) {
    stm->cuerpo->accept(this);
}

void EVALVisitor::visit(AsignStmt* stm) {
    Value value = stm->exp ? stm->exp->accept(this) : Value();
    if (env.check(stm->variable)) {
        env.update(stm->variable, value);
    } else {
        env.add_var(stm->variable, value);
    }
}

void EVALVisitor::visit(PrintStmt* stm) {
    cout << stm->exp->accept(this).toString() << endl;
}

void EVALVisitor::visit(ReturnStm* stm) {
    retval = stm->exp ? stm->exp->accept(this) : Value();
    retcall = true;
}

void EVALVisitor::visit(DeleteStm* stm) {
}

void EVALVisitor::visit(ContinueStm* stm) {
    continuecall = true;
}

void EVALVisitor::visit(BreakStmt* stm) {
    breakcall = true;
}

void EVALVisitor::visit(SwitchStmt* stm) {
    Value condition = stm->condicion->accept(this);

    for (auto& caso : stm->casos) {
        if (valuesEqual(condition, caso.first->accept(this))) {
            caso.second->accept(this);
            if (breakcall) {
                breakcall = false;
            }
            return;
        }
    }

    if (stm->default_caso != nullptr) {
        stm->default_caso->accept(this);
        if (breakcall) {
            breakcall = false;
        }
    }
}

void EVALVisitor::visit(TryStmt* stm) {
    if (stm->expr != nullptr) {
        stm->expr->accept(this);
    }
    if (stm->try_body != nullptr) {
        stm->try_body->accept(this);
    }
}

void EVALVisitor::visit(DeferStmt* stm) {
}

void EVALVisitor::visit(ForStmt* stm) {
    env.add_level();
    if (stm->asignacion != nullptr) {
        stm->asignacion->accept(this);
    }

    while (stm->condicion == nullptr || isTruthy(stm->condicion->accept(this))) {
        stm->cuerpo->accept(this);
        if (retcall) {
            break;
        }
        if (breakcall) {
            breakcall = false;
            break;
        }
        if (continuecall) {
            continuecall = false;
        }
        if (stm->incremento != nullptr) {
            stm->incremento->accept(this);
        }
    }

    env.remove_level();
}

void EVALVisitor::visit(Fundec* fd) {
    envfun[fd->nombre] = fd;
}

void EVALVisitor::visit(Structdec* sd) {
}

void EVALVisitor::visit(VarDec* vd) {
    Value value = vd->exp ? vd->exp->accept(this) : Value();
    env.add_var(vd->nombre, value);
}

void EVALVisitor::visit(ConstDec* cd) {
    Value value = cd->exp ? cd->exp->accept(this) : Value();
    env.add_var(cd->nombre, value);
}

void EVALVisitor::visit(Template* t) {
}

void EVALVisitor::visit(IdType* tipo) {
}

void EVALVisitor::visit(PointerType* tipo) {
}

void EVALVisitor::visit(ArrayType* tipo) {
}

void EVALVisitor::visit(OptionalType* tipo) {
}

void EVALVisitor::visit(ErrorType* tipo) {
}

void EVALVisitor::visit(UnionType* tipo) {
}

void EVALVisitor::visit(EnumType* tipo) {
}

void EVALVisitor::visit(Body* body) {
    env.add_level();
    for (Stmt* stmt : body->slist) {
        stmt->accept(this);
        if (retcall || breakcall || continuecall) {
            break;
        }
    }
    env.remove_level();
}

void EVALVisitor::visit(Programa* programa) {
    env.clear();
    envfun.clear();
    retcall = false;
    breakcall = false;
    continuecall = false;

    env.add_level();

    for (Top_dec* dec : programa->declist) {
        if (Fundec* fd = dynamic_cast<Fundec*>(dec)) {
            visit(fd);
        }
    }

    for (Top_dec* dec : programa->declist) {
        if (dynamic_cast<Fundec*>(dec) == nullptr) {
            dec->accept(this);
        }
    }

    auto mainIt = envfun.find("main");
    if (mainIt != envfun.end()) {
        FcallExp mainCall;
        mainCall.nombre = "main";
        visit(&mainCall);
    }

    env.remove_level();
}

void EVALVisitor::interprete(Programa* programa) {
    programa->accept(this);
}
