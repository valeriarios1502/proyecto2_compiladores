#include "Typechecker.h"
#include <iostream>
#include <stdexcept>

using namespace std;

// ============================================================
// Punto de entrada público
// ============================================================

void TypeCheckerVisitor::typecheck(Programa* programa) {
    env.clear();      
    funEnv.clear();
    structEnv.clear();
    registerStructs(programa);
    registerFunctions(programa);
    programa->accept(this);
}

// ============================================================
// Pre-registro de structs y funciones (para forward references)
// ============================================================

void TypeCheckerVisitor::registerStructs(Programa* p) {
    for (Top_dec* d : p->declist) {
        if (Structdec* sd = dynamic_cast<Structdec*>(d)) {
            unordered_map<string, string> fields;
            for (size_t i = 0; i < sd->id_parametros.size(); ++i) {
                sd->tipo_parametros[i]->accept(this);
                fields[sd->id_parametros[i]] = currentType;
            }
            structEnv[sd->nombre] = fields;
        }
        // Unions también las registramos como si fueran structs
        if (VarDec* vd = dynamic_cast<VarDec*>(d)) {
            if (UnionType* ut = dynamic_cast<UnionType*>(vd->tipo)) {
                unordered_map<string, string> fields;
                for (size_t i = 0; i < ut->campo_nombres.size(); ++i) {
                    ut->campo_tipos[i]->accept(this);
                    fields[ut->campo_nombres[i]] = currentType;
                }
                structEnv[ut->nombre] = fields;
            }
        }
    }
}

void TypeCheckerVisitor::registerFunctions(Programa* p) {
    for (Top_dec* d : p->declist) {
        if (Fundec* fd = dynamic_cast<Fundec*>(d)) {
            FunInfo info;
            fd->tipo->accept(this);
            info.returnType = currentType;
            for (size_t i = 0; i < fd->id_parametros.size(); ++i) {
                fd->tipo_parametros[i]->accept(this);
                info.paramTypes.push_back(currentType);
                info.paramNames.push_back(fd->id_parametros[i]);
            }
            funEnv[fd->nombre] = info;
        }
        if (Template* t = dynamic_cast<Template*>(d)) {
            FunInfo info;
            t->tipo->accept(this);
            info.returnType = currentType;
            for (size_t i = 0; i < t->id_parametros.size(); ++i) {
                t->tipo_parametros[i]->accept(this);
                info.paramTypes.push_back(currentType);
                info.paramNames.push_back(t->id_parametros[i]);
            }
            funEnv[t->id1] = info;
        }
    }
}

// ============================================================
// Programa y Body
// ============================================================

void TypeCheckerVisitor::visit(Programa* p) {
    env.add_level();
    for (Top_dec* d : p->declist)
        d->accept(this);
    env.remove_level();
}

void TypeCheckerVisitor::visit(Body* b) {
    env.add_level();
    for (Stmt* s : b->slist)
        s->accept(this);
    env.remove_level();
}

// ============================================================
// Declaraciones
// ============================================================

void TypeCheckerVisitor::visit(Fundec* fd) {
    if (fd->nombre == "__comptime__") {
        // bloque comptime top-level
        fd->cuerpo->accept(this);
        return;
    }

    string prevFunction = currentFunction;
    string prevReturn   = expectedReturnType;
    bool   prevHasRet   = hasReturn;

    fd->tipo->accept(this);
    expectedReturnType = currentType;
    currentFunction    = fd->nombre;
    hasReturn          = false;

    env.add_level();
    for (size_t i = 0; i < fd->id_parametros.size(); ++i) {
        fd->tipo_parametros[i]->accept(this);
        env.add_var(fd->id_parametros[i], currentType);
    }

    if (fd->cuerpo)
        fd->cuerpo->accept(this);

    if (expectedReturnType != "void" && !hasReturn)
        throw runtime_error("TypeChecker: función '" + fd->nombre +
                            "' no tiene return en todos los caminos");

    env.remove_level();

    currentFunction    = prevFunction;
    expectedReturnType = prevReturn;
    hasReturn          = prevHasRet;
}

void TypeCheckerVisitor::visit(Structdec* sd) {
    // Ya fue registrado en registerStructs; nada más que hacer
}

void TypeCheckerVisitor::visit(VarDec* vd) {
    string declaredType;
    if (vd->tienetipo) {
        vd->tipo->accept(this);
        declaredType = currentType;
    }

    if (vd->exp) {
        string exprType = inferType(vd->exp);

        if (vd->tienetipo) {
            if (!soncompatibles(declaredType, exprType))
                throw runtime_error("TypeChecker: tipo incompatible en var '" +
                                    vd->nombre + "': se esperaba '" + declaredType +
                                    "' pero se obtuvo '" + exprType + "'");
        } else {
            declaredType = exprType;
        }
    } else if (!vd->tienetipo) {
        throw runtime_error("TypeChecker: var '" + vd->nombre +
                            "' no tiene tipo ni expresión");
    }

    env.add_var(vd->nombre, declaredType);
}

void TypeCheckerVisitor::visit(ConstDec* cd) {
    string declaredType;
    if (cd->tienetipo) {
        cd->tipo->accept(this);
        declaredType = currentType;
    }

    if (cd->exp) {
        string exprType = inferType(cd->exp);

        if (cd->tienetipo) {
            if (!soncompatibles(declaredType, exprType))
                throw runtime_error("TypeChecker: tipo incompatible en const '" +
                                    cd->nombre + "': se esperaba '" + declaredType +
                                    "' pero se obtuvo '" + exprType + "'");
        } else {
            declaredType = exprType;
        }
    } else if (!cd->tienetipo) {
        throw runtime_error("TypeChecker: const '" + cd->nombre +
                            "' no tiene tipo ni expresión");
    }

    env.add_var(cd->nombre, declaredType);
}

void TypeCheckerVisitor::visit(Template* t) {
    // Tratamos el parámetro genérico como un tipo opaco
    string prevFunction = currentFunction;
    string prevReturn   = expectedReturnType;
    bool   prevHasRet   = hasReturn;

    t->tipo->accept(this);
    expectedReturnType = currentType;
    currentFunction    = t->id1;
    hasReturn          = false;

    env.add_level();
    // Registramos el parámetro de tipo genérico como tipo "any"
    env.add_var(t->id2, "type");

    for (size_t i = 0; i < t->id_parametros.size(); ++i) {
        t->tipo_parametros[i]->accept(this);
        env.add_var(t->id_parametros[i], currentType);
    }

    if (t->block)
        t->block->accept(this);

    env.remove_level();

    currentFunction    = prevFunction;
    expectedReturnType = prevReturn;
    hasReturn          = prevHasRet;
}

// ============================================================
// Statements
// ============================================================

void TypeCheckerVisitor::visit(IfStmt* stm) {
    string condType = inferType(stm->condicion);
    if (!isBool(condType))
        throw runtime_error("TypeChecker: condición de if debe ser bool, se obtuvo '" +
                            condType + "'");

    stm->cuerpodelif->accept(this);
    if (stm->hayelse && stm->cuerpodelelse)
        stm->cuerpodelelse->accept(this);
}

void TypeCheckerVisitor::visit(WhileStmt* stm) {
    string condType = inferType(stm->condicion);
    if (!isBool(condType))
        throw runtime_error("TypeChecker: condición de while debe ser bool, se obtuvo '" +
                            condType + "'");

    env.add_level();
    for (Stmt* s : stm->cuerpodelwhile)
        s->accept(this);
    env.remove_level();
}

void TypeCheckerVisitor::visit(ForStmt* stm) {
    env.add_level();

    if (stm->asignacion)
        stm->asignacion->accept(this);

    if (stm->condicion) {
        string condType = inferType(stm->condicion);
        if (!isBool(condType))
            throw runtime_error("TypeChecker: condición de for debe ser bool, se obtuvo '" +
                                condType + "'");
    }

    if (stm->incremento)
        stm->incremento->accept(this);

    if (stm->cuerpo)
        stm->cuerpo->accept(this);

    env.remove_level();
}

void TypeCheckerVisitor::visit(BodyStmt* stm) {
    if (stm->cuerpo)
        stm->cuerpo->accept(this);
}

void TypeCheckerVisitor::visit(AsignStmt* stm) {
    if (stm->variable == "__call__") {
        if (stm->exp) inferType(stm->exp);
        return;
    }

    string exprType = inferType(stm->exp);

    if (env.check(stm->variable)) {
        string existing = env.lookup(stm->variable);
        if (!soncompatibles(existing, exprType))
            throw runtime_error("TypeChecker: asignación inválida a '" + stm->variable +
                                "': se esperaba '" + existing +
                                "' pero se obtuvo '" + exprType + "'");
    } else {
        env.add_var(stm->variable, exprType);
    }
}

void TypeCheckerVisitor::visit(PrintStmt* stm) {
    // print acepta cualquier tipo
    if (stm->exp) inferType(stm->exp);
}

void TypeCheckerVisitor::visit(ReturnStm* stm) {
    hasReturn = true;

    if (stm->exp) {
        string retType = inferType(stm->exp);
        if (!soncompatibles(expectedReturnType, retType))
            throw runtime_error("TypeChecker: return de tipo '" + retType +
                                "' incompatible con tipo de retorno '" +
                                expectedReturnType + "' en función '" +
                                currentFunction + "'");
    } else {
        // return vacío
        if (expectedReturnType != "void")
            throw runtime_error("TypeChecker: return sin valor en función '" +
                                currentFunction +
                                "' que retorna '" + expectedReturnType + "'");
    }
}

void TypeCheckerVisitor::visit(DeleteStm* stm) {
    string t = inferType(stm->exp);
    if (!isPointer(t) && t != "null")
        throw runtime_error("TypeChecker: delete requiere un puntero, se obtuvo '" +
                            t + "'");
}

void TypeCheckerVisitor::visit(ContinueStm* stm) {
    // No requiere verificación de tipos
}

void TypeCheckerVisitor::visit(BreakStmt* stm) {
    if (stm->tiene_valor && stm->valor)
        inferType(stm->valor); // El tipo de break-with-value lo verifica quien lo captura
}

void TypeCheckerVisitor::visit(SwitchStmt* stm) {
    string condType = inferType(stm->condicion);

    for (auto& [patron, body] : stm->casos) {
        string patronType = inferType(patron);
        if (!soncompatibles(condType, patronType))
            throw runtime_error("TypeChecker: patrón de switch de tipo '" + patronType +
                                "' incompatible con condición de tipo '" + condType + "'");
        body->accept(this);
    }

    if (stm->default_caso)
        stm->default_caso->accept(this);
}

void TypeCheckerVisitor::visit(TryStmt* stm) {
    if (stm->expr) inferType(stm->expr);

    if (stm->try_body)  stm->try_body->accept(this);

    if (stm->catch_body) {
        env.add_level();
        if (!stm->error_var.empty())
            env.add_var(stm->error_var, "error");
        stm->catch_body->accept(this);
        env.remove_level();
    }
}

void TypeCheckerVisitor::visit(DeferStmt* stm) {
    if (stm->stmt) stm->stmt->accept(this);
}

// ============================================================
// Expresiones
// ============================================================

Value TypeCheckerVisitor::visit(NumberExpDecimal* exp) {
    currentType = "int";
    return Value();
}

Value TypeCheckerVisitor::visit(NumberExpFlotante* exp) {
    currentType = "float";
    return Value();
}

Value TypeCheckerVisitor::visit(StringExp* exp) {
    currentType = "string";
    return Value();
}

Value TypeCheckerVisitor::visit(CharExp* exp) {
    currentType = "char";
    return Value();
}

Value TypeCheckerVisitor::visit(BoolExp* exp) {
    currentType = "bool";
    return Value();
}

Value TypeCheckerVisitor::visit(NullExp* exp) {
    currentType = "null";
    return Value();
}

Value TypeCheckerVisitor::visit(UndefinedExp* exp) {
    currentType = "undefined";
    return Value();
}

Value TypeCheckerVisitor::visit(IdExp* exp) {
    if (!env.check(exp->value))
        throw runtime_error("TypeChecker: variable no declarada '" + exp->value + "'");
    currentType = env.lookup(exp->value);
    return Value();
}

Value TypeCheckerVisitor::visit(BinaryExp* exp) {
    string leftType  = inferType(exp->left);
    string rightType = inferType(exp->right);

    switch (exp->op) {
        case PLUS_OP:
            if (leftType == "string" && rightType == "string") {
                currentType = "string";
                return Value();
            }
            if (!isNumeric(leftType) || !isNumeric(rightType))
                throw runtime_error("'+' requiere numéricos o strings, se obtuvo '" +
                                    leftType + "' y '" + rightType + "'");
            currentType = (leftType == "float" || rightType == "float") ? "float" : "int";
            break;

        case MINUS_OP:
        case MUL_OP:
        case MODULO_OP:
            if (!isNumeric(leftType) || !isNumeric(rightType))
                throw runtime_error("Operación aritmética requiere numéricos, se obtuvo '" +
                                    leftType + "' y '" + rightType + "'");
            currentType = (leftType == "float" || rightType == "float") ? "float" : "int";
            break;

        case DIV_OP:
            if (!isNumeric(leftType) || !isNumeric(rightType))
                throw runtime_error("'/' requiere numéricos");
            currentType = "float";  
            break;

        case MENORI:   // <=
        case MENOR:    // <
        case MAYORI:   // >=
        case MAYOR:    // >
            if (!isNumeric(leftType) || !isNumeric(rightType))
                throw runtime_error("TypeChecker: comparación relacional requiere tipos numéricos");
            currentType = "bool";
            break;

        case IGUALIGUAL:   // ==
        case DIFERENTE_OP: // !=
            if (!soncompatibles(leftType, rightType) && !soncompatibles(rightType, leftType))
                throw runtime_error("TypeChecker: comparación de tipos incompatibles '" +
                                    leftType + "' y '" + rightType + "'");
            currentType = "bool";
            break;

        case AND:
        case OR:
            if (!isBool(leftType) || !isBool(rightType))
                throw runtime_error("TypeChecker: operadores && / || requieren bool");
            currentType = "bool";
            break;

        case REFERENCIA: // & (bitwise and o address — tratamos como int)
            if (!isNumeric(leftType) || !isNumeric(rightType))
                throw runtime_error("TypeChecker: operador & requiere tipos numéricos");
            currentType = "int";
            break;

        default:
            currentType = "unknown";
            break;
    }
    return Value();
}

Value TypeCheckerVisitor::visit(NotExp* exp) {
    if (exp->exp) {
        string t = inferType(exp->exp);
        if (!isBool(t))
            throw runtime_error("TypeChecker: operador '!' requiere bool, se obtuvo '" + t + "'");
    }
    currentType = "bool";
    return Value();
}

Value TypeCheckerVisitor::visit(UnaryExp* exp) {
    string t = inferType(exp->exp);
    switch (exp->op) {
        case UnaryExp::NOT_OP:
            if (!isBool(t))
                throw runtime_error("TypeChecker: '!' requiere bool");
            currentType = "bool";
            break;
        case UnaryExp::NEGATE:
            if (!isNumeric(t))
                throw runtime_error("TypeChecker: '-' unario requiere numérico");
            currentType = t;
            break;
        case UnaryExp::ADDRESS:
            currentType = "*" + t;
            break;
        case UnaryExp::DEREF:
            if (!isPointer(t))
                throw runtime_error("TypeChecker: '*' desreferencia requiere puntero, se obtuvo '" + t + "'");
            currentType = t.substr(1); // quitar el '*'
            break;
        default:
            currentType = t;
            break;
    }
    return Value();
}

Value TypeCheckerVisitor::visit(FcallExp* exp) {
    // Funciones internas del parser
    if (exp->nombre == "__try__") {
        if (!exp->argumentos.empty())
            inferType(exp->argumentos[0]);
        // __try__ propaga el tipo interno quitando el prefijo de error
        if (!currentType.empty() && currentType[0] == '!')
            currentType = currentType.substr(1);
        return Value();
    }

    auto it = funEnv.find(exp->nombre);
    if (it == funEnv.end())
        throw runtime_error("TypeChecker: función no declarada '" + exp->nombre + "'");

    const FunInfo& info = it->second;

    if (exp->argumentos.size() != info.paramTypes.size())
        throw runtime_error("TypeChecker: función '" + exp->nombre +
                            "' espera " + to_string(info.paramTypes.size()) +
                            " argumentos pero se pasaron " +
                            to_string(exp->argumentos.size()));

    for (size_t i = 0; i < exp->argumentos.size(); ++i) {
        string argType = inferType(exp->argumentos[i]);
        if (!soncompatibles(info.paramTypes[i], argType))
            throw runtime_error("TypeChecker: argumento " + to_string(i + 1) +
                                " de '" + exp->nombre + "': se esperaba '" +
                                info.paramTypes[i] + "' pero se obtuvo '" +
                                argType + "'");
    }

    currentType = info.returnType;
    return Value();
}

Value TypeCheckerVisitor::visit(NewExp* exp) {
    exp->tipo->accept(this);
    currentType = "*" + currentType; // new devuelve puntero
    return Value();
}

Value TypeCheckerVisitor::visit(ReferenceExp* exp) {
    if (exp->exp) {
        string t = inferType(exp->exp);
        currentType = "*" + t;
    } else {
        currentType = "*unknown";
    }
    return Value();
}

Value TypeCheckerVisitor::visit(PunteroExp* exp) {
    if (exp->exp) {
        string t = inferType(exp->exp);
        if (!isPointer(t))
            throw runtime_error("TypeChecker: desreferencia de no-puntero '" + t + "'");
        currentType = t.substr(1);
    } else {
        currentType = "unknown";
    }
    return Value();
}

Value TypeCheckerVisitor::visit(AlgoconcorchetesExp* exp) {
    // expr[index]
    string baseType = inferType(exp->nombre);
    string indexType = inferType(exp->dentroexp);

    if (!isNumeric(indexType) && indexType != "int")
        throw runtime_error("TypeChecker: índice de array debe ser int, se obtuvo '" +
                            indexType + "'");

    // Si es array tipo, extraemos el tipo elemento
    // Arrays se representan como "[N]T" o simplemente T[]
    // Aquí hacemos una heurística simple
    if (!baseType.empty() && baseType.back() == ']') {
        // quitar [N] del frente
        auto pos = baseType.rfind(']');
        if (pos != string::npos)
            currentType = baseType.substr(pos + 1);
        else
            currentType = baseType;
    } else if (isPointer(baseType)) {
        currentType = baseType.substr(1); // puntero indexado → elemento
    } else {
        // tipo genérico: devolvemos mismo tipo (puede ser string → char)
        if (baseType == "string")
            currentType = "char";
        else
            currentType = baseType;
    }
    return Value();
}

Value TypeCheckerVisitor::visit(AlgoconcorchetesylistaExp* exp) {
    // expr[a, b, ...]  (multi-índice)
    string baseType = inferType(exp->nombre);
    for (Exp* e : exp->argumentos)
        inferType(e);
    // Por simplicidad devolvemos el mismo tipo base
    currentType = baseType;
    return Value();
}

Value TypeCheckerVisitor::visit(PuntoExp* exp) {
    string baseType = inferType(exp->exp);

    // Unwrap opcional: expr.?
    if (exp->id == "__unwrap__") {
        if (!isOptional(baseType))
            throw runtime_error("TypeChecker: .? sobre tipo no opcional '" + baseType + "'");
        currentType = baseType.substr(1); // quitar '?'
        return Value();
    }

    // Acceso a campo de struct
    auto it = structEnv.find(baseType);
    if (it == structEnv.end()) {
        // Puede ser un optional de struct: ?NombreStruct
        string innerType = isOptional(baseType) ? baseType.substr(1) : baseType;
        it = structEnv.find(innerType);
        if (it == structEnv.end()) {
            // Tipo nativo con propiedades (e.g. string.len)
            if (baseType == "string" && exp->id == "len") {
                currentType = "int";
                return Value();
            }
            // Desconocido: lo dejamos pasar como unknown
            currentType = "unknown";
            return Value();
        }
    }

    const auto& fields = it->second;
    auto fit = fields.find(exp->id);
    if (fit == fields.end())
        throw runtime_error("TypeChecker: campo '" + exp->id +
                            "' no existe en struct '" + baseType + "'");

    currentType = fit->second;
    return Value();
}

Value TypeCheckerVisitor::visit(LambdaExp* exp) {
    // Construimos el tipo funcional como string "fn(T1,T2)->Ret"
    string prevFunction = currentFunction;
    string prevReturn   = expectedReturnType;
    bool   prevHasRet   = hasReturn;

    exp->tipo->accept(this);
    string retType = currentType;
    expectedReturnType = retType;
    currentFunction    = "__lambda__";
    hasReturn          = false;

    env.add_level();
    for (size_t i = 0; i < exp->id_parametros.size(); ++i) {
        exp->tipo_parametros[i]->accept(this);
        env.add_var(exp->id_parametros[i], currentType);
    }

    if (exp->cuerpo)
        exp->cuerpo->accept(this);

    env.remove_level();

    currentFunction    = prevFunction;
    expectedReturnType = prevReturn;
    hasReturn          = prevHasRet;

    currentType = "fn->" + retType;
    return Value();
}

// ============================================================
// Tipos
// ============================================================

void TypeCheckerVisitor::visit(IdType* tipo) {
    // Mapeo de nombres de tipo a representación interna
    const string& id = tipo->id;
    if (id == "i32" || id == "i64" || id == "u32" || id == "u64" ||
        id == "i8"  || id == "u8"  || id == "isize" || id == "usize" ||
        id == "comptime_int")
        currentType = "int";
    else if (id == "f32" || id == "f64" || id == "comptime_float")
        currentType = "float";
    else if (id == "bool")
        currentType = "bool";
    else if (id == "void")
        currentType = "void";
    else if (id == "str"  || id == "[]u8" || id == "[]const u8")
        currentType = "string";
    else if (id == "char" || id == "u8")
        currentType = "char";
    else if (id == "error")
        currentType = "error";
    else if (id == "type")
        currentType = "type";
    else if (id == "anytype" || id == "anyopaque")
        currentType = "any";
    else
        currentType = id; // nombre de struct, enum, etc.
}

void TypeCheckerVisitor::visit(PointerType* tipo) {
    tipo->tipo->accept(this);
    currentType = "*" + currentType;
}

void TypeCheckerVisitor::visit(ArrayType* tipo) {
    tipo->tipo->accept(this);
    string inner = currentType;
    // Representamos [N]T como "[N]T" string (opaco para el checker)
    if (tipo->exp1) {
        tipo->exp1->accept(this);
    }
    currentType = "[]" + inner;
}

void TypeCheckerVisitor::visit(OptionalType* tipo) {
    tipo->tipo->accept(this);
    currentType = "?" + currentType;
}

void TypeCheckerVisitor::visit(ErrorType* tipo) {
    tipo->tipo->accept(this);
    currentType = "!" + currentType;
}

void TypeCheckerVisitor::visit(UnionType* tipo) {
    currentType = tipo->nombre;
}

void TypeCheckerVisitor::visit(EnumType* tipo) {
    currentType = tipo->nombre;
}