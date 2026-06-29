#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "ast.h"
#include "visitor.h"
#include <cstdint>

using namespace std;

// Registros de argumentos
static const char* argRegs[] = {
    "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
};
static const int MAX_REG_ARGS = 6;

// =============================================================================
//  Helpers de SethiUlman — deben ir ANTES de cualquier visit() que los use
// =============================================================================

static int suOf(Value v) { return v.i > 0 ? v.i : 1; }

static int suBinary(int nL, int nR) {
    return (nL == nR) ? nL + 1 : max(nL, nR);
}

static Value suResult(int su, int& maxReg) {
    if (su > maxReg) maxReg = su;
    return Value::makeInt(su);
}

// =============================================================================
//  Helpers de ConstantFolding — también antes de su uso
// =============================================================================

static CFValue toCF(const Value& v) {
    switch (v.kind) {
        case Value::VAL_INT:       return CFValue((long long)v.i);
        case Value::VAL_FLOAT:     return CFValue(v.f);
        case Value::VAL_BOOL:      return CFValue(v.b ? 1LL : 0LL);
        case Value::VAL_CHAR:      return CFValue((long long)(unsigned char)v.c);
        case Value::VAL_NULL:
        case Value::VAL_UNDEFINED: return CFValue(0LL);
        default:                   return CFValue();
    }
}

static Value fromCF(const CFValue& cf) {
    if (!cf.is_const) return Value();
    if (cf.dbl_val == (double)(long long)cf.dbl_val)
        return Value::makeInt((int)cf.int_val);
    return Value::makeFloat(cf.dbl_val);
}

// =============================================================================
//  Helpers de GenCode
// =============================================================================

static string escapeString(const string& s) {
    string out;
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            default:   out += c;      break;
        }
    }
    return out;
}

static std::string inferGlobalType(Exp* exp) {
    if (!exp)                                  return "int";
    if (dynamic_cast<NumberExpFlotante*>(exp)) return "float";
    if (dynamic_cast<StringExp*>(exp))         return "str";
    if (dynamic_cast<CharExp*>(exp))           return "char";
    if (dynamic_cast<BoolExp*>(exp))           return "bool";
    return "int";
}

// =============================================================================
//  GenCodeVisitor — helpers miembro
// =============================================================================

void GenCodeVisitor::emitAlignedCall(const string& target) {
    cout << "    pushq %r12"       << endl;
    cout << "    movq  %rsp, %r12" << endl;
    cout << "    andq  $-16, %rsp" << endl;
    cout << "    xorq  %rax, %rax" << endl;
    cout << "    call  " << target << endl;
    cout << "    movq  %r12, %rsp" << endl;
    cout << "    popq  %r12"       << endl;
}

void GenCodeVisitor::emitStructBaseAddress(Exp* expr) {
    if (IdExp* id = dynamic_cast<IdExp*>(expr)) {
        if (!posicion.count(id->value)) {
            cerr << "[GenCode] Advertencia: struct base '" << id->value << "' no declarado\n";
            cout << "    xorq  %rax, %rax" << endl;
            return;
        }
        cout << "    movq  " << offset(id->value) << ", %rax" << endl;
    } else {
        expr->accept(this);
    }
}

int GenCodeVisitor::getFieldOffset(Exp* base, const string& fieldName) {
    for (auto& [sname, fields] : structFieldOffsets) {
        auto it = fields.find(fieldName);
        if (it != fields.end())
            return it->second;
    }
    cerr << "[GenCode] Advertencia: campo '" << fieldName << "' no encontrado → offset 0\n";
    return 0;
}

void GenCodeVisitor::emitGlobalVarDec(VarDec* vd) {
    globalNames.insert(vd->nombre);
    string gtype = inferGlobalType(vd->exp);
    globalTypes[vd->nombre] = gtype;

    cout << ".globl " << vd->nombre << endl;

    if (gtype == "float") {
        double val = 0.0;
        if (NumberExpFlotante* ne = dynamic_cast<NumberExpFlotante*>(vd->exp))
            val = ne->value;
        int64_t bits = 0;
        memcpy(&bits, &val, sizeof(bits));
        cout << vd->nombre << ": .quad " << bits << endl;
    } else if (gtype == "str") {
        string lbl = internString(dynamic_cast<StringExp*>(vd->exp)->valor);
        cout << vd->nombre << ": .quad " << lbl << endl;
    } else if (gtype == "char") {
        int val = 0;
        if (CharExp* ce = dynamic_cast<CharExp*>(vd->exp))
            val = (unsigned char)ce->valor;
        cout << vd->nombre << ": .quad " << val << endl;
    } else if (gtype == "bool") {
        int val = 0;
        if (BoolExp* be = dynamic_cast<BoolExp*>(vd->exp))
            val = (be->booleano == "true") ? 1 : 0;
        cout << vd->nombre << ": .quad " << val << endl;
    } else {
        int64_t val = 0;
        if (NumberExpDecimal* ne = dynamic_cast<NumberExpDecimal*>(vd->exp))
            val = ne->value;
        cout << vd->nombre << ": .quad " << val << endl;
    }
}

void GenCodeVisitor::emitGlobalConstDec(ConstDec* cd) {
    globalNames.insert(cd->nombre);
    string gtype = inferGlobalType(cd->exp);
    globalTypes[cd->nombre] = gtype;

    cout << ".globl " << cd->nombre << endl;

    if (gtype == "float") {
        double val = 0.0;
        if (NumberExpFlotante* ne = dynamic_cast<NumberExpFlotante*>(cd->exp))
            val = ne->value;
        int64_t bits = 0;
        memcpy(&bits, &val, sizeof(bits));
        cout << cd->nombre << ": .quad " << bits << endl;
    } else {
        int64_t val = 0;
        if (NumberExpDecimal* ne = dynamic_cast<NumberExpDecimal*>(cd->exp))
            val = ne->value;
        cout << cd->nombre << ": .quad " << val << endl;
    }
}

// =============================================================================
//  GenCodeVisitor — gencode / Programa / Fundec / Template
// =============================================================================

void GenCodeVisitor::gencode(Programa* programa) {
    for (Top_dec* d : programa->declist) {
        if (Structdec* sd = dynamic_cast<Structdec*>(d)) {
            for (size_t i = 0; i < sd->id_parametros.size(); ++i)
                structFieldOffsets[sd->nombre][sd->id_parametros[i]] = (int)i * 8;
            structFieldCount[sd->nombre] = (int)sd->id_parametros.size();
        }
        if (VarDec* vd = dynamic_cast<VarDec*>(d)) {
            if (UnionType* ut = dynamic_cast<UnionType*>(vd->tipo)) {
                for (size_t i = 0; i < ut->campo_nombres.size(); ++i)
                    structFieldOffsets[ut->nombre][ut->campo_nombres[i]] = (int)i * 8;
                structFieldCount[ut->nombre] = (int)ut->campo_nombres.size();
            }
        }
    }

    for (Top_dec* d : programa->declist)
        if (Fundec* fd = dynamic_cast<Fundec*>(d))
            funEnv[fd->nombre] = fd;

    cout << ".section .data"                           << endl;
    cout << "print_int_fmt:   .string \"%ld\\n\""     << endl;
    cout << "print_uint_fmt:  .string \"%lu\\n\""     << endl;
    cout << "print_float_fmt: .string \"%g\\n\""      << endl;
    cout << "print_str_fmt:   .string \"%s\\n\""      << endl;
    cout << "print_char_fmt:  .string \"%c\\n\""      << endl;
    cout << "print_bool_true: .string \"true\\n\""    << endl;
    cout << "print_bool_false:.string \"false\\n\""   << endl;
    cout << endl;

    for (Top_dec* d : programa->declist) {
        if (VarDec* vd = dynamic_cast<VarDec*>(d))
            emitGlobalVarDec(vd);
        else if (ConstDec* cd = dynamic_cast<ConstDec*>(d))
            emitGlobalConstDec(cd);
    }

    cout << ".section .text" << endl;
    cout << ".globl main"    << endl;

    programa->accept(this);

    if (!stringLiterals.empty()) {
        cout << endl;
        cout << ".section .rodata" << endl;
        for (auto& p : stringLiterals)
            cout << p.first << ": .string \"" << escapeString(p.second) << "\"" << endl;
    }
}

void GenCodeVisitor::visit(Programa* p) {
    for (Top_dec* d : p->declist)
        if (Fundec* fd = dynamic_cast<Fundec*>(d))
            if (fd->nombre == "__comptime__")
                hayComptimeGlobal = true;

    for (Top_dec* d : p->declist)
        d->accept(this);
}

void GenCodeVisitor::visit(Fundec* fd) {
    string asmName = (fd->nombre == "__comptime__") ? "_comptime_init" : fd->nombre;

    string prevFunction  = currentFunction;
    string prevLoopEnd   = currentLoopEnd;
    string prevLoopStart = currentLoopStart;
    auto   prevPosicion  = posicion;
    int    prevContador  = varContador;

    currentFunction  = asmName;
    currentLoopEnd   = "";
    currentLoopStart = "";
    posicion.clear();
    varContador = 1;

    for (size_t i = 0; i < fd->id_parametros.size(); ++i)
        posicion[fd->id_parametros[i]] = varContador++;

    const int FRAME_SLOTS = 128;
    int frameBytes = alignFrame(FRAME_SLOTS);

    cout << endl;
    cout << asmName << ":" << endl;
    cout << "    pushq %rbp"                        << endl;
    cout << "    movq  %rsp, %rbp"                  << endl;
    cout << "    subq  $" << frameBytes << ", %rsp" << endl;

    for (size_t i = 0; i < fd->id_parametros.size() && i < (size_t)MAX_REG_ARGS; ++i)
        cout << "    movq  " << argRegs[i] << ", " << offset(fd->id_parametros[i]) << endl;

    if (fd->nombre == "main" && hayComptimeGlobal)
        emitAlignedCall("_comptime_init");

    if (fd->cuerpo) fd->cuerpo->accept(this);

    cout << "    xorq  %rax, %rax" << endl;
    cout << "    leave"            << endl;
    cout << "    ret"              << endl;

    currentFunction  = prevFunction;
    currentLoopEnd   = prevLoopEnd;
    currentLoopStart = prevLoopStart;
    posicion         = prevPosicion;
    varContador      = prevContador;
}

void GenCodeVisitor::visit(Template* t) {
    string prevFunction  = currentFunction;
    string prevLoopEnd   = currentLoopEnd;
    string prevLoopStart = currentLoopStart;
    auto   prevPosicion  = posicion;
    int    prevContador  = varContador;

    currentFunction  = t->id1;
    currentLoopEnd   = "";
    currentLoopStart = "";
    posicion.clear();
    varContador = 1;

    for (size_t i = 0; i < t->id_parametros.size(); ++i)
        posicion[t->id_parametros[i]] = varContador++;

    const int FRAME_SLOTS = 128;
    int frameBytes = alignFrame(FRAME_SLOTS);

    cout << endl;
    cout << t->id1 << ":" << endl;
    cout << "    pushq %rbp"                        << endl;
    cout << "    movq  %rsp, %rbp"                  << endl;
    cout << "    subq  $" << frameBytes << ", %rsp" << endl;

    for (size_t i = 0; i < t->id_parametros.size() && i < (size_t)MAX_REG_ARGS; ++i)
        cout << "    movq  " << argRegs[i] << ", " << offset(t->id_parametros[i]) << endl;

    if (t->block) t->block->accept(this);

    cout << "    xorq  %rax, %rax" << endl;
    cout << "    leave"            << endl;
    cout << "    ret"              << endl;

    currentFunction  = prevFunction;
    currentLoopEnd   = prevLoopEnd;
    currentLoopStart = prevLoopStart;
    posicion         = prevPosicion;
    varContador      = prevContador;
}

void GenCodeVisitor::visit(Structdec* sd) {}

void GenCodeVisitor::visit(Body* b) {
    for (Stmt* s : b->slist)
        s->accept(this);
}

// =============================================================================
//  VarDec / ConstDec
// =============================================================================

void GenCodeVisitor::visit(VarDec* vd) {
    if (currentFunction.empty()) return;

    if (vd->tienetipo && vd->tipo) {
        if (IdType* it = dynamic_cast<IdType*>(vd->tipo)) {
            auto sit = structFieldOffsets.find(it->id);
            if (sit != structFieldOffsets.end()) {
                int nFields  = structFieldCount[it->id];
                int baseSlot = varContador;
                posicion[vd->nombre] = baseSlot;
                varContador += nFields;
                return;
            }
        }
        if (ArrayType* at = dynamic_cast<ArrayType*>(vd->tipo)) {
            int arraySize = 8;
            if (NumberExpDecimal* ne = dynamic_cast<NumberExpDecimal*>(at->exp1))
                arraySize = ne->value;

            int baseSlot = varContador;
            posicion[vd->nombre] = baseSlot;
            varContador += arraySize;

            if (vd->exp) {
                vd->exp->accept(this);
                cout << "    movq  %rax, " << offset(vd->nombre) << endl;
            } else {
                cout << "    leaq  " << (baseSlot * -8) << "(%rbp), %rax" << endl;
                cout << "    movq  %rax, " << offset(vd->nombre) << endl;
            }
            return;
        }
    }

    if (posicion.find(vd->nombre) == posicion.end())
        posicion[vd->nombre] = varContador++;

    if (vd->exp) {
        vd->exp->accept(this);
        cout << "    movq  %rax, " << offset(vd->nombre) << endl;
    }
}

void GenCodeVisitor::visit(ConstDec* cd) {
    if (currentFunction.empty()) return;

    if (posicion.find(cd->nombre) == posicion.end())
        posicion[cd->nombre] = varContador++;

    if (cd->exp) {
        cd->exp->accept(this);
        cout << "    movq  %rax, " << offset(cd->nombre) << endl;
    }
}

// =============================================================================
//  Statements
// =============================================================================

void GenCodeVisitor::visit(AsignStmt* stm) {
    if (stm->variable == "__call__") {
        if (stm->exp) stm->exp->accept(this);
        return;
    }
    if (posicion.find(stm->variable) == posicion.end())
        posicion[stm->variable] = varContador++;

    stm->exp->accept(this);
    cout << "    movq  %rax, " << offset(stm->variable) << endl;
}

void GenCodeVisitor::visit(DerefAssignStmt* stm) {
    stm->rval->accept(this);
    cout << "    pushq %rax" << endl;

    if (AlgoconcorchetesExp* arr = dynamic_cast<AlgoconcorchetesExp*>(stm->lval)) {
        arr->nombre->accept(this);
        cout << "    pushq %rax"       << endl;
        arr->dentroexp->accept(this);
        cout << "    imulq $8, %rax"   << endl;
        cout << "    popq  %rcx"       << endl;
        cout << "    addq  %rcx, %rax" << endl;
    } else if (PuntoExp* pe = dynamic_cast<PuntoExp*>(stm->lval)) {
        emitStructBaseAddress(pe->exp);
        int fieldOff = getFieldOffset(pe->exp, pe->id);
        if (fieldOff != 0)
            cout << "    addq  $" << fieldOff << ", %rax" << endl;
    } else if (UnaryExp* ue = dynamic_cast<UnaryExp*>(stm->lval)) {
        ue->exp->accept(this);
    } else if (IdExp* id = dynamic_cast<IdExp*>(stm->lval)) {
        if (posicion.find(id->value) == posicion.end())
            posicion[id->value] = varContador++;
        cout << "    movq  " << offset(id->value) << ", %rax" << endl;
    } else {
        stm->lval->accept(this);
    }

    cout << "    movq  %rax, %rcx"   << endl;
    cout << "    popq  %rdx"         << endl;
    cout << "    movq  %rdx, (%rcx)" << endl;
}

void GenCodeVisitor::visit(PrintStmt* stm) {
    Exp* e = stm->exp;
    string knownType = "";
    if (IdExp* id = dynamic_cast<IdExp*>(e)) {
        auto it = globalTypes.find(id->value);
        if (it != globalTypes.end())
            knownType = it->second;
    }

    if (knownType == "bool") {
        e->accept(this);
        string labelTrue = newLabel("bool_true");
        string labelDone = newLabel("bool_done");
        cout << "    cmpq  $0, %rax"                      << endl;
        cout << "    jne   " << labelTrue                  << endl;
        cout << "    leaq  print_bool_false(%rip), %rdi"  << endl;
        cout << "    jmp   " << labelDone                  << endl;
        cout << labelTrue << ":"                           << endl;
        cout << "    leaq  print_bool_true(%rip), %rdi"   << endl;
        cout << labelDone << ":"                           << endl;
        cout << "    pushq %r12"                           << endl;
        cout << "    movq  %rsp, %r12"                     << endl;
        cout << "    andq  $-16, %rsp"                     << endl;
        cout << "    xorq  %rax, %rax"                     << endl;
        cout << "    call  puts@PLT"                       << endl;
        cout << "    movq  %r12, %rsp"                     << endl;
        cout << "    popq  %r12"                           << endl;
        return;
    }

    if (BoolExp* be = dynamic_cast<BoolExp*>(e)) {
        string lbl = (be->booleano == "true") ? "print_bool_true" : "print_bool_false";
        cout << "    leaq  " << lbl << "(%rip), %rdi" << endl;
        cout << "    pushq %r12"                       << endl;
        cout << "    movq  %rsp, %r12"                 << endl;
        cout << "    andq  $-16, %rsp"                 << endl;
        cout << "    xorq  %rax, %rax"                 << endl;
        cout << "    call  puts@PLT"                   << endl;
        cout << "    movq  %r12, %rsp"                 << endl;
        cout << "    popq  %r12"                       << endl;
        return;
    }

    e->accept(this);
    cout << "    pushq %r12"       << endl;
    cout << "    movq  %rsp, %r12" << endl;
    cout << "    andq  $-16, %rsp" << endl;
    cout << "    movq  %rax, %rsi" << endl;

    if (knownType == "float" || dynamic_cast<NumberExpFlotante*>(e)) {
        cout << "    movq  %rax, %xmm0"                 << endl;
        cout << "    leaq  print_float_fmt(%rip), %rdi" << endl;
        cout << "    movq  $1, %rax"                     << endl;
        cout << "    call  printf@PLT"                   << endl;
    } else if (knownType == "str" || dynamic_cast<StringExp*>(e)) {
        cout << "    leaq  print_str_fmt(%rip), %rdi"   << endl;
        cout << "    xorq  %rax, %rax"                   << endl;
        cout << "    call  printf@PLT"                   << endl;
    } else if (knownType == "char" || dynamic_cast<CharExp*>(e)) {
        cout << "    leaq  print_char_fmt(%rip), %rdi"  << endl;
        cout << "    xorq  %rax, %rax"                   << endl;
        cout << "    call  printf@PLT"                   << endl;
    } else {
        cout << "    leaq  print_int_fmt(%rip), %rdi"   << endl;
        cout << "    xorq  %rax, %rax"                   << endl;
        cout << "    call  printf@PLT"                   << endl;
    }

    cout << "    movq  %r12, %rsp" << endl;
    cout << "    popq  %r12"       << endl;
}

void GenCodeVisitor::visit(ReturnStm* stm) {
    if (stm->exp)
        stm->exp->accept(this);
    else
        cout << "    xorq  %rax, %rax" << endl;
    cout << "    leave" << endl;
    cout << "    ret"   << endl;
}

void GenCodeVisitor::visit(IfStmt* stm) {
    string labelElse = newLabel("else");
    string labelEnd  = newLabel("endif");

    stm->condicion->accept(this);
    cout << "    cmpq  $0, %rax"      << endl;
    cout << "    je    " << labelElse << endl;

    stm->cuerpodelif->accept(this);
    cout << "    jmp   " << labelEnd  << endl;

    cout << labelElse << ":" << endl;
    if (stm->hayelse && stm->cuerpodelelse)
        stm->cuerpodelelse->accept(this);
    cout << labelEnd << ":" << endl;
}

void GenCodeVisitor::visit(WhileStmt* stm) {
    string labelStart = newLabel("while_start");
    string labelEnd   = newLabel("while_end");

    string prevStart = currentLoopStart;
    string prevEnd   = currentLoopEnd;
    currentLoopStart = labelStart;
    currentLoopEnd   = labelEnd;

    cout << labelStart << ":" << endl;
    stm->condicion->accept(this);
    cout << "    cmpq  $0, %rax"     << endl;
    cout << "    je    " << labelEnd << endl;

    for (Stmt* s : stm->cuerpodelwhile)
        s->accept(this);

    cout << "    jmp   " << labelStart << endl;
    cout << labelEnd << ":"            << endl;

    currentLoopStart = prevStart;
    currentLoopEnd   = prevEnd;
}

void GenCodeVisitor::visit(ForStmt* stm) {
    bool esForEach = (dynamic_cast<NullExp*>(stm->condicion) != nullptr);

    string labelStart = newLabel("for_start");
    string labelEnd   = newLabel("for_end");

    string prevStart = currentLoopStart;
    string prevEnd   = currentLoopEnd;
    currentLoopStart = labelStart;
    currentLoopEnd   = labelEnd;

    if (esForEach) {
        AsignStmt* initStmt = dynamic_cast<AsignStmt*>(stm->asignacion);
        AsignStmt* idxStmt  = dynamic_cast<AsignStmt*>(stm->incremento);

        string elemVar = initStmt ? initStmt->variable : "__elem__";
        string idxVar  = idxStmt  ? idxStmt->variable  : "__idx__";

        if (posicion.find(elemVar) == posicion.end())
            posicion[elemVar] = varContador++;
        if (posicion.find(idxVar) == posicion.end())
            posicion[idxVar] = varContador++;

        string baseVar = "__arr_base_" + to_string(labelCounter) + "__";
        posicion[baseVar] = varContador++;

        if (initStmt && initStmt->exp) {
            if (IdExp* id = dynamic_cast<IdExp*>(initStmt->exp)) {
                if (posicion.count(id->value))
                    cout << "    movq  " << offset(id->value) << ", %rax" << endl;
                else {
                    cerr << "[GenCode] Advertencia: iterable '" << id->value << "' no declarado\n";
                    cout << "    xorq  %rax, %rax" << endl;
                }
            } else {
                initStmt->exp->accept(this);
            }
        } else {
            cout << "    xorq  %rax, %rax" << endl;
        }
        cout << "    movq  %rax, " << offset(baseVar) << endl;
        cout << "    movq  $0, %rax"                   << endl;
        cout << "    movq  %rax, " << offset(idxVar)  << endl;

        cout << labelStart << ":" << endl;

        cout << "    movq  " << offset(idxVar)  << ", %rax"     << endl;
        cout << "    movq  " << offset(baseVar) << ", %rcx"     << endl;
        cout << "    movq  (%rcx,%rax,8), %rax"                 << endl;
        cout << "    movq  %rax, " << offset(elemVar)           << endl;

        if (stm->cuerpo) stm->cuerpo->accept(this);

        cout << "    movq  " << offset(idxVar) << ", %rax" << endl;
        cout << "    addq  $1, %rax"                        << endl;
        cout << "    movq  %rax, " << offset(idxVar)        << endl;

        cout << "    jmp   " << labelStart << endl;
        cout << labelEnd << ":"            << endl;

    } else {
        if (stm->asignacion) stm->asignacion->accept(this);
        cout << labelStart << ":" << endl;
        stm->condicion->accept(this);
        cout << "    cmpq  $0, %rax"     << endl;
        cout << "    je    " << labelEnd << endl;
        if (stm->cuerpo) stm->cuerpo->accept(this);
        if (stm->incremento) stm->incremento->accept(this);
        cout << "    jmp   " << labelStart << endl;
        cout << labelEnd << ":"            << endl;
    }

    currentLoopStart = prevStart;
    currentLoopEnd   = prevEnd;
}

void GenCodeVisitor::visit(BreakStmt* stm) {
    if (stm->tiene_valor && stm->valor)
        stm->valor->accept(this);
    if (!currentLoopEnd.empty())
        cout << "    jmp   " << currentLoopEnd << endl;
}

void GenCodeVisitor::visit(ContinueStm* stm) {
    if (!currentLoopStart.empty())
        cout << "    jmp   " << currentLoopStart << endl;
}

void GenCodeVisitor::visit(SwitchStmt* stm) {
    string labelEnd = newLabel("switch_end");

    stm->condicion->accept(this);
    cout << "    pushq %rax" << endl;

    for (auto& caso : stm->casos) {
        string labelNext = newLabel("switch_next");
        caso.first->accept(this);
        cout << "    movq  %rax, %rcx"    << endl;
        cout << "    movq  0(%rsp), %rax" << endl;
        cout << "    cmpq  %rcx, %rax"    << endl;
        cout << "    jne   " << labelNext << endl;
        caso.second->accept(this);
        cout << "    addq  $8, %rsp"     << endl;
        cout << "    jmp   " << labelEnd << endl;
        cout << labelNext << ":"         << endl;
    }

    if (stm->default_caso)
        stm->default_caso->accept(this);

    cout << "    addq  $8, %rsp" << endl;
    cout << labelEnd << ":"      << endl;
}

void GenCodeVisitor::visit(BodyStmt*  stm) { if (stm->cuerpo) stm->cuerpo->accept(this); }
void GenCodeVisitor::visit(DeleteStm* stm) {
    stm->exp->accept(this);
    cout << "    movq  %rax, %rdi" << endl;
    emitAlignedCall("free@PLT");
}
void GenCodeVisitor::visit(DeferStmt* stm) { if (stm->stmt) stm->stmt->accept(this); }
void GenCodeVisitor::visit(TryStmt*   stm) { if (stm->try_body) stm->try_body->accept(this); }

// =============================================================================
//  Expresiones — GenCode
// =============================================================================

Value GenCodeVisitor::visit(NumberExpDecimal* exp) {
    cout << "    movq  $" << exp->value << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(NumberExpFlotante* exp) {
    double  f64  = (double)exp->value;
    int64_t bits = 0;
    memcpy(&bits, &f64, sizeof(bits));
    cout << "    movabsq $" << bits << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(BoolExp* exp) {
    cout << "    movq  $" << (exp->booleano == "true" ? 1 : 0) << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(NullExp*      exp) { cout << "    xorq  %rax, %rax" << endl; return Value(); }
Value GenCodeVisitor::visit(UndefinedExp* exp) { cout << "    xorq  %rax, %rax" << endl; return Value(); }

Value GenCodeVisitor::visit(StringExp* exp) {
    string lbl = internString(exp->valor);
    cout << "    leaq  " << lbl << "(%rip), %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(CharExp* exp) {
    cout << "    movq  $" << (int)(unsigned char)exp->valor << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(IdExp* exp) {
    if (exp->value.size() >= 6 && exp->value.substr(0, 6) == "error.") {
        if (!errorCodes.count(exp->value))
            errorCodes[exp->value] = (int)errorCodes.size() + 1;
        cout << "    movq  $" << errorCodes[exp->value] << ", %rax" << endl;
        return Value();
    }
    if (globalNames.count(exp->value)) {
        cout << "    movq  " << exp->value << "(%rip), %rax" << endl;
        return Value();
    }
    if (!posicion.count(exp->value)) {
        cerr << "[GenCode] Advertencia: variable no declarada '" << exp->value << "' → 0\n";
        cout << "    xorq  %rax, %rax" << endl;
        return Value();
    }
    cout << "    movq  " << offset(exp->value) << ", %rax" << endl;
    return Value();
}

// ── BinaryExp — BUG FIJO: right se evalúa y apila; left queda en %rax ───────
Value GenCodeVisitor::visit(BinaryExp* exp) {
    // 1. Evaluar right → apilar
    exp->right->accept(this);
    cout << "    pushq %rax" << endl;      // right en stack

    // 2. Evaluar left → %rax
    exp->left->accept(this);              // left  en %rax

    // 3. Recuperar right → %rcx
    cout << "    popq  %rcx" << endl;     // right en %rcx

    // 4. Operar: %rax = left,  %rcx = right
    switch (exp->op) {
        case PLUS_OP:
            cout << "    addq  %rcx, %rax"  << endl; break;
        case MINUS_OP:
            cout << "    subq  %rcx, %rax"  << endl; break;
        case MUL_OP:
            cout << "    imulq %rcx, %rax"  << endl; break;
        case DIV_OP:
            cout << "    cqto"              << endl;
            cout << "    idivq %rcx"        << endl; break;
        case MODULO_OP:
            cout << "    cqto"              << endl;
            cout << "    idivq %rcx"        << endl;
            cout << "    movq  %rdx, %rax"  << endl; break;
        case MENOR:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setl  %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl; break;
        case MENORI:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setle %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl; break;
        case MAYOR:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setg  %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl; break;
        case MAYORI:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setge %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl; break;
        case IGUALIGUAL:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    sete  %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl; break;
        case DIFERENTE_OP:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setne %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl; break;
        case AND:
            cout << "    testq %rax, %rax"   << endl;
            cout << "    setne %r10b"         << endl;
            cout << "    testq %rcx, %rcx"   << endl;
            cout << "    setne %r11b"         << endl;
            cout << "    andb  %r11b, %r10b"  << endl;
            cout << "    movzbq %r10b, %rax"  << endl; break;
        case OR:
            cout << "    orq   %rcx, %rax"   << endl;
            cout << "    setne %al"           << endl;
            cout << "    movzbq %al, %rax"    << endl; break;
        case REFERENCIA:
            cout << "    andq  %rcx, %rax"   << endl; break;
        default:
            cerr << "[GenCode] Operador binario no soportado (op=" << exp->op << ")\n"; break;
    }
    return Value();
}

Value GenCodeVisitor::visit(NotExp* exp) {
    if (exp->exp) exp->exp->accept(this);
    cout << "    testq %rax, %rax"  << endl;
    cout << "    sete  %al"          << endl;
    cout << "    movzbq %al, %rax"   << endl;
    return Value();
}

Value GenCodeVisitor::visit(UnaryExp* exp) {
    switch (exp->op) {
        case UnaryExp::NEGATE:
            exp->exp->accept(this);
            cout << "    negq  %rax" << endl;
            break;
        case UnaryExp::NOT_OP:
            exp->exp->accept(this);
            cout << "    testq %rax, %rax"  << endl;
            cout << "    sete  %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl;
            break;
        case UnaryExp::ADDRESS:
            if (IdExp* id = dynamic_cast<IdExp*>(exp->exp)) {
                if (!posicion.count(id->value))
                    posicion[id->value] = varContador++;
                cout << "    leaq  " << offset(id->value) << ", %rax" << endl;
            } else {
                exp->exp->accept(this);
            }
            break;
        case UnaryExp::DEREF:
            exp->exp->accept(this);
            cout << "    movq  (%rax), %rax" << endl;
            break;
    }
    return Value();
}

Value GenCodeVisitor::visit(FcallExp* exp) {
    if (exp->nombre == "__try__" || exp->nombre == "__catch__") {
        if (!exp->argumentos.empty())
            exp->argumentos[0]->accept(this);
        return Value();
    }

    int nArgs      = (int)exp->argumentos.size();
    int nRegArgs   = min(nArgs, MAX_REG_ARGS);
    int nStackArgs = max(0, nArgs - MAX_REG_ARGS);

    for (int i = nArgs - 1; i >= 0; --i) {
        exp->argumentos[i]->accept(this);
        cout << "    pushq %rax" << endl;
    }
    for (int i = 0; i < nRegArgs; ++i)
        cout << "    popq  " << argRegs[i] << endl;

    emitAlignedCall(exp->nombre);

    if (nStackArgs > 0)
        cout << "    addq  $" << (nStackArgs * 8) << ", %rsp" << endl;

    return Value();
}

Value GenCodeVisitor::visit(NewExp* exp) {
    int bytes = 8;
    if (exp->tipo) {
        if (ArrayType* at = dynamic_cast<ArrayType*>(exp->tipo)) {
            if (NumberExpDecimal* ne = dynamic_cast<NumberExpDecimal*>(at->exp1))
                bytes = ne->value * 8;
            else {
                at->exp1->accept(this);
                cout << "    imulq $8, %rax"  << endl;
                cout << "    movq  %rax, %rdi" << endl;
                emitAlignedCall("malloc@PLT");
                return Value();
            }
        } else if (IdType* it = dynamic_cast<IdType*>(exp->tipo)) {
            auto sit = structFieldCount.find(it->id);
            if (sit != structFieldCount.end())
                bytes = sit->second * 8;
            else
                bytes = 64 * 8;
        }
    }
    cout << "    movq  $" << bytes << ", %rdi" << endl;
    emitAlignedCall("malloc@PLT");
    return Value();
}

Value GenCodeVisitor::visit(ReferenceExp* exp) {
    if (!exp->exp) { cout << "    xorq  %rax, %rax" << endl; return Value(); }
    if (IdExp* id = dynamic_cast<IdExp*>(exp->exp)) {
        if (!posicion.count(id->value))
            posicion[id->value] = varContador++;
        cout << "    leaq  " << offset(id->value) << ", %rax" << endl;
    } else {
        exp->exp->accept(this);
    }
    return Value();
}

Value GenCodeVisitor::visit(PunteroExp* exp) {
    if (!exp->exp) { cout << "    xorq  %rax, %rax" << endl; return Value(); }
    exp->exp->accept(this);
    cout << "    movq  (%rax), %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(PuntoExp* exp) {
    if (exp->id == "__unwrap__") {
        exp->exp->accept(this);
        return Value();
    }
    emitStructBaseAddress(exp->exp);
    int fieldOff = getFieldOffset(exp->exp, exp->id);
    cout << "    movq  " << fieldOff << "(%rax), %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(AlgoconcorchetesExp* exp) {
    exp->nombre->accept(this);
    cout << "    pushq %rax"                 << endl;
    exp->dentroexp->accept(this);
    cout << "    popq  %rcx"                 << endl;
    cout << "    movq  (%rcx,%rax,8), %rax"  << endl;
    return Value();
}

Value GenCodeVisitor::visit(AlgoconcorchetesylistaExp* exp) {
    if (exp->argumentos.empty()) {
        exp->nombre->accept(this);
        return Value();
    }
    exp->nombre->accept(this);
    cout << "    pushq %rax" << endl;
    exp->argumentos[0]->accept(this);
    if (exp->argumentos.size() >= 2) {
        cout << "    pushq %rax"        << endl;
        exp->argumentos[1]->accept(this);
        cout << "    movq  %rax, %rcx"  << endl;
        cout << "    popq  %rax"        << endl;
        cout << "    addq  %rcx, %rax"  << endl;
    }
    cout << "    popq  %rcx"                << endl;
    cout << "    movq  (%rcx,%rax,8), %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(LambdaExp* exp) {
    cout << "    xorq  %rax, %rax" << endl;
    return Value();
}

void GenCodeVisitor::visit(IdType*       t) {}
void GenCodeVisitor::visit(PointerType*  t) {}
void GenCodeVisitor::visit(ArrayType*    t) {}
void GenCodeVisitor::visit(OptionalType* t) {}
void GenCodeVisitor::visit(ErrorType*    t) {}
void GenCodeVisitor::visit(UnionType*    t) {}
void GenCodeVisitor::visit(EnumType*     t) {}

// =============================================================================
//  ConstantFolding
// =============================================================================

CFValue ConstantFolding::fold(Exp* e) {
    if (!e) return CFValue();
    return toCF(e->accept(this));
}

Value ConstantFolding::visit(NumberExpDecimal*  exp) { return Value::makeInt(exp->value); }
Value ConstantFolding::visit(NumberExpFlotante* exp) { return Value::makeFloat((double)exp->value); }
Value ConstantFolding::visit(BoolExp*           exp) { return Value::makeBool(exp->booleano == "true"); }
Value ConstantFolding::visit(CharExp*           exp) { return Value::makeChar(exp->valor); }
Value ConstantFolding::visit(StringExp*         exp) { return Value::makeString(exp->valor); }
Value ConstantFolding::visit(NullExp*           exp) { return Value::makeNull(); }
Value ConstantFolding::visit(UndefinedExp*      exp) { return Value::makeUndefined(); }

Value ConstantFolding::visit(IdExp* exp) {
    auto it = cfEnv.find(exp->value);
    return (it != cfEnv.end()) ? fromCF(it->second) : Value();
}

Value ConstantFolding::visit(BinaryExp* exp) {
    CFValue lv = fold(exp->left);
    CFValue rv = fold(exp->right);
    if (!lv.is_const || !rv.is_const) return Value();

    long long L = lv.int_val, R = rv.int_val;
    switch (exp->op) {
        case PLUS_OP:      return fromCF(CFValue(L + R));
        case MINUS_OP:     return fromCF(CFValue(L - R));
        case MUL_OP:       return fromCF(CFValue(L * R));
        case DIV_OP:       return R == 0 ? Value() : fromCF(CFValue(L / R));
        case MODULO_OP:    return R == 0 ? Value() : fromCF(CFValue(L % R));
        case MENOR:        return Value::makeBool(L <  R);
        case MENORI:       return Value::makeBool(L <= R);
        case MAYOR:        return Value::makeBool(L >  R);
        case MAYORI:       return Value::makeBool(L >= R);
        case IGUALIGUAL:   return Value::makeBool(L == R);
        case DIFERENTE_OP: return Value::makeBool(L != R);
        case AND:          return Value::makeBool(L != 0 && R != 0);
        case OR:           return Value::makeBool(L != 0 || R != 0);
        case REFERENCIA:   return fromCF(CFValue(L & R));
        default:           return Value();
    }
}

Value ConstantFolding::visit(NotExp* exp) {
    CFValue v = fold(exp->exp);
    return v.is_const ? Value::makeBool(v.int_val == 0) : Value();
}

Value ConstantFolding::visit(UnaryExp* exp) {
    CFValue v = fold(exp->exp);
    if (!v.is_const) return Value();
    switch (exp->op) {
        case UnaryExp::NEGATE:  return fromCF(CFValue(-v.int_val));
        case UnaryExp::NOT_OP:  return Value::makeBool(v.int_val == 0);
        default:                return Value();
    }
}

Value ConstantFolding::visit(FcallExp*                  exp) { for (Exp* a : exp->argumentos) a->accept(this); return Value(); }
Value ConstantFolding::visit(NewExp*                    exp) { return Value(); }
Value ConstantFolding::visit(ReferenceExp*              exp) { return Value(); }
Value ConstantFolding::visit(PunteroExp*                exp) { if (exp->exp) exp->exp->accept(this); return Value(); }
Value ConstantFolding::visit(PuntoExp*                  exp) { if (exp->exp) exp->exp->accept(this); return Value(); }
Value ConstantFolding::visit(AlgoconcorchetesExp*       exp) { if (exp->nombre) exp->nombre->accept(this); if (exp->dentroexp) exp->dentroexp->accept(this); return Value(); }
Value ConstantFolding::visit(AlgoconcorchetesylistaExp* exp) { if (exp->nombre) exp->nombre->accept(this); for (Exp* a : exp->argumentos) a->accept(this); return Value(); }
Value ConstantFolding::visit(LambdaExp*                 exp) { return Value(); }

void ConstantFolding::visit(IfStmt* stm) {
    if (stm->condicion)   stm->condicion->accept(this);
    if (stm->cuerpodelif) stm->cuerpodelif->accept(this);
    if (stm->hayelse && stm->cuerpodelelse) stm->cuerpodelelse->accept(this);
}
void ConstantFolding::visit(WhileStmt* stm) { if (stm->condicion) stm->condicion->accept(this); for (Stmt* s : stm->cuerpodelwhile) s->accept(this); }
void ConstantFolding::visit(ForStmt*   stm) { if (stm->asignacion) stm->asignacion->accept(this); if (stm->condicion) stm->condicion->accept(this); if (stm->incremento) stm->incremento->accept(this); if (stm->cuerpo) stm->cuerpo->accept(this); }
void ConstantFolding::visit(BodyStmt*  stm) { if (stm->cuerpo) stm->cuerpo->accept(this); }
void ConstantFolding::visit(AsignStmt* stm) { if (!stm->exp) return; CFValue v = fold(stm->exp); if (v.is_const) cfEnv[stm->variable] = v; }
void ConstantFolding::visit(PrintStmt* stm) { if (stm->exp)  stm->exp->accept(this); }
void ConstantFolding::visit(ReturnStm* stm) { if (stm->exp)  stm->exp->accept(this); }
void ConstantFolding::visit(DeleteStm* stm) { if (stm->exp)  stm->exp->accept(this); }
void ConstantFolding::visit(ContinueStm* stm) {}
void ConstantFolding::visit(BreakStmt* stm)   { if (stm->tiene_valor && stm->valor) stm->valor->accept(this); }
void ConstantFolding::visit(SwitchStmt* stm) {
    if (stm->condicion) stm->condicion->accept(this);
    for (auto& c : stm->casos) { c.first->accept(this); c.second->accept(this); }
    if (stm->default_caso) stm->default_caso->accept(this);
}
void ConstantFolding::visit(TryStmt*   stm) { if (stm->try_body) stm->try_body->accept(this); }
void ConstantFolding::visit(DeferStmt* stm) { if (stm->stmt) stm->stmt->accept(this); }
void ConstantFolding::visit(DerefAssignStmt* stm) { if (stm->lval) stm->lval->accept(this); if (stm->rval) stm->rval->accept(this); }

void ConstantFolding::visit(VarDec*  vd) { if (!vd->exp) return; CFValue v = fold(vd->exp); if (v.is_const) cfEnv[vd->nombre] = v; }
void ConstantFolding::visit(ConstDec* cd){ if (!cd->exp) return; CFValue v = fold(cd->exp); if (v.is_const) cfEnv[cd->nombre] = v; }
void ConstantFolding::visit(Structdec* sd) {}
void ConstantFolding::visit(Fundec* fd)   { auto s = cfEnv; cfEnv.clear(); if (fd->cuerpo) fd->cuerpo->accept(this); cfEnv = s; }
void ConstantFolding::visit(Template* t)  { auto s = cfEnv; cfEnv.clear(); if (t->block)   t->block->accept(this);   cfEnv = s; }

void ConstantFolding::visit(IdType*       t) {}
void ConstantFolding::visit(PointerType*  t) {}
void ConstantFolding::visit(ArrayType*    t) {}
void ConstantFolding::visit(OptionalType* t) {}
void ConstantFolding::visit(ErrorType*    t) {}
void ConstantFolding::visit(UnionType*    t) {}
void ConstantFolding::visit(EnumType*     t) {}

void ConstantFolding::visit(Body*     b) { for (Stmt* s : b->slist) s->accept(this); }
void ConstantFolding::visit(Programa* p) { for (Top_dec* d : p->declist) d->accept(this); }

// =============================================================================
//  SethiUlman
// =============================================================================

Value SethiUlman::visit(NumberExpDecimal*  exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(NumberExpFlotante* exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(BoolExp*           exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(CharExp*           exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(StringExp*         exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(NullExp*           exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(UndefinedExp*      exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(LambdaExp*         exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(NewExp*            exp) { return suResult(1, maxRegisters); }
Value SethiUlman::visit(IdExp*             exp) { if (verbose) cerr << "[SU] Id(" << exp->value << ")=1\n"; return suResult(1, maxRegisters); }

Value SethiUlman::visit(BinaryExp* exp) {
    int nL = suOf(exp->left->accept(this));
    int nR = suOf(exp->right->accept(this));
    int su = suBinary(nL, nR);
    if (verbose) cerr << "[SU] Binary op=" << exp->op << " L=" << nL << " R=" << nR << " → " << su << "\n";
    return suResult(su, maxRegisters);
}

Value SethiUlman::visit(NotExp*  exp) { int n = exp->exp ? suOf(exp->exp->accept(this)) : 1; return suResult(n, maxRegisters); }
Value SethiUlman::visit(UnaryExp* exp) {
    int n  = exp->exp ? suOf(exp->exp->accept(this)) : 1;
    int su = (exp->op == UnaryExp::DEREF) ? n + 1 : n;
    return suResult(su, maxRegisters);
}
Value SethiUlman::visit(ReferenceExp* exp) { int n = exp->exp ? suOf(exp->exp->accept(this)) : 1; return suResult(n, maxRegisters); }
Value SethiUlman::visit(PunteroExp*   exp) { int n = exp->exp ? suOf(exp->exp->accept(this)) : 1; return suResult(n + 1, maxRegisters); }
Value SethiUlman::visit(PuntoExp*     exp) { int n = exp->exp ? suOf(exp->exp->accept(this)) : 1; return suResult(n + 1, maxRegisters); }

Value SethiUlman::visit(AlgoconcorchetesExp* exp) {
    int nB = exp->nombre    ? suOf(exp->nombre->accept(this))    : 1;
    int nI = exp->dentroexp ? suOf(exp->dentroexp->accept(this)) : 1;
    return suResult(suBinary(nB, nI) + 1, maxRegisters);
}
Value SethiUlman::visit(AlgoconcorchetesylistaExp* exp) {
    int su = exp->nombre ? suOf(exp->nombre->accept(this)) : 1;
    for (Exp* a : exp->argumentos) su = suBinary(su, suOf(a->accept(this))) + 1;
    return suResult(su, maxRegisters);
}
Value SethiUlman::visit(FcallExp* exp) {
    int su = 1;
    for (Exp* a : exp->argumentos) su = max(su, suOf(a->accept(this)));
    return suResult(su + 1, maxRegisters);
}

void SethiUlman::visit(IfStmt* stm) { if (stm->condicion) stm->condicion->accept(this); if (stm->cuerpodelif) stm->cuerpodelif->accept(this); if (stm->hayelse && stm->cuerpodelelse) stm->cuerpodelelse->accept(this); }
void SethiUlman::visit(WhileStmt* stm) { if (stm->condicion) stm->condicion->accept(this); for (Stmt* s : stm->cuerpodelwhile) s->accept(this); }
void SethiUlman::visit(ForStmt*   stm) { if (stm->asignacion) stm->asignacion->accept(this); if (stm->condicion) stm->condicion->accept(this); if (stm->incremento) stm->incremento->accept(this); if (stm->cuerpo) stm->cuerpo->accept(this); }
void SethiUlman::visit(BodyStmt*  stm) { if (stm->cuerpo) stm->cuerpo->accept(this); }
void SethiUlman::visit(AsignStmt* stm) { if (stm->exp) stm->exp->accept(this); }
void SethiUlman::visit(PrintStmt* stm) { if (stm->exp) stm->exp->accept(this); }
void SethiUlman::visit(ReturnStm* stm) { if (stm->exp) stm->exp->accept(this); }
void SethiUlman::visit(DeleteStm* stm) { if (stm->exp) stm->exp->accept(this); }
void SethiUlman::visit(ContinueStm* stm) {}
void SethiUlman::visit(BreakStmt* stm) { if (stm->tiene_valor && stm->valor) stm->valor->accept(this); }
void SethiUlman::visit(SwitchStmt* stm) {
    if (stm->condicion) stm->condicion->accept(this);
    for (auto& c : stm->casos) { c.first->accept(this); c.second->accept(this); }
    if (stm->default_caso) stm->default_caso->accept(this);
}
void SethiUlman::visit(TryStmt*   stm) { if (stm->try_body) stm->try_body->accept(this); }
void SethiUlman::visit(DeferStmt* stm) { if (stm->stmt) stm->stmt->accept(this); }
void SethiUlman::visit(DerefAssignStmt* stm) { if (stm->lval) stm->lval->accept(this); if (stm->rval) stm->rval->accept(this); }

void SethiUlman::visit(VarDec*  vd) { if (vd->exp)  vd->exp->accept(this); }
void SethiUlman::visit(ConstDec* cd){ if (cd->exp)  cd->exp->accept(this); }
void SethiUlman::visit(Structdec* sd) {}
void SethiUlman::visit(Fundec* fd) {
    int s = maxRegisters; maxRegisters = 0;
    if (fd->cuerpo) fd->cuerpo->accept(this);
    if (verbose) cerr << "[SU] Función '" << fd->nombre << "' → " << maxRegisters << " registros\n";
    if (maxRegisters < s) maxRegisters = s;
}
void SethiUlman::visit(Template* t) {
    int s = maxRegisters; maxRegisters = 0;
    if (t->block) t->block->accept(this);
    if (verbose) cerr << "[SU] Template '" << t->id1 << "' → " << maxRegisters << " registros\n";
    if (maxRegisters < s) maxRegisters = s;
}

void SethiUlman::visit(IdType*       t) {}
void SethiUlman::visit(PointerType*  t) {}
void SethiUlman::visit(ArrayType*    t) {}
void SethiUlman::visit(OptionalType* t) {}
void SethiUlman::visit(ErrorType*    t) {}
void SethiUlman::visit(UnionType*    t) {}
void SethiUlman::visit(EnumType*     t) {}

void SethiUlman::visit(Body*     b) { for (Stmt* s : b->slist) s->accept(this); }
void SethiUlman::visit(Programa* p) {
    for (Top_dec* d : p->declist) d->accept(this);
    if (verbose) cerr << "[SU] Programa → max registros globales = " << maxRegisters << "\n";
}

// =============================================================================
//  Cascada — helpers
// =============================================================================

static CFValue toCF_C(const Value& v) { return toCF(v); }   // reutiliza el helper existente
static Value   fromCF_C(const CFValue& cf) { return fromCF(cf); }

bool Cascada::isIntConst(Exp* e, long long k) {
    if (!e) return false;
    if (auto* n = dynamic_cast<NumberExpDecimal*>(e))  return n->value == k;
    if (auto* b = dynamic_cast<BoolExp*>(e))           return (b->booleano=="true"?1:0) == k;
    return false;
}

CFValue Cascada::fold(Exp* e) {
    if (!e) return CFValue();
    return toCF_C(e->accept(this));
}

void Cascada::optimize(Programa* p) { p->accept(this); }

// =============================================================================
//  Cascada — expresiones
// =============================================================================

Value Cascada::visit(NumberExpDecimal*  exp) { return Value::makeInt(exp->value); }
Value Cascada::visit(NumberExpFlotante* exp) { return Value::makeFloat((double)exp->value); }
Value Cascada::visit(BoolExp*           exp) { return Value::makeBool(exp->booleano=="true"); }
Value Cascada::visit(CharExp*           exp) { return Value::makeChar(exp->valor); }
Value Cascada::visit(StringExp*         exp) { return Value::makeString(exp->valor); }
Value Cascada::visit(NullExp*           exp) { return Value::makeNull(); }
Value Cascada::visit(UndefinedExp*      exp) { return Value::makeUndefined(); }
Value Cascada::visit(LambdaExp*         exp) { return Value(); }
Value Cascada::visit(NewExp*            exp) { return Value(); }
Value Cascada::visit(ReferenceExp*      exp) { if (exp->exp) exp->exp->accept(this); return Value(); }
Value Cascada::visit(PuntoExp*          exp) { if (exp->exp) exp->exp->accept(this); return Value(); }
Value Cascada::visit(PunteroExp*        exp) { if (exp->exp) exp->exp->accept(this); return Value(); }
Value Cascada::visit(AlgoconcorchetesExp* exp) {
    if (exp->nombre)    exp->nombre->accept(this);
    if (exp->dentroexp) exp->dentroexp->accept(this);
    return Value();
}
Value Cascada::visit(AlgoconcorchetesylistaExp* exp) {
    if (exp->nombre) exp->nombre->accept(this);
    for (Exp* a : exp->argumentos) a->accept(this);
    return Value();
}
Value Cascada::visit(FcallExp* exp) {
    for (Exp* a : exp->argumentos) a->accept(this);
    return Value();
}

Value Cascada::visit(IdExp* exp) {
    used.insert(exp->value);
    auto it = env.find(exp->value);
    if (it == env.end() || !it->second.is_const) return Value();
    // Devuelve el valor plegado para que el padre lo use
    return fromCF_C(it->second);
}


// =============================================================================
//  Cascada — expresiones (versión segura, sin modificar el AST)
// =============================================================================

Value Cascada::visit(BinaryExp* exp) {
    CFValue lv = fold(exp->left);
    CFValue rv = fold(exp->right);

    // Simplificaciones algebraicas: registrar en env si aplica,
    // pero NO tocar los punteros del AST.

    if (!lv.is_const || !rv.is_const) return Value();

    long long L = lv.int_val, R = rv.int_val;
    switch (exp->op) {
        case PLUS_OP:      return fromCF_C(CFValue(L + R));
        case MINUS_OP:     return fromCF_C(CFValue(L - R));
        case MUL_OP:       return fromCF_C(CFValue(L * R));
        case DIV_OP:       return R == 0 ? Value() : fromCF_C(CFValue(L / R));
        case MODULO_OP:    return R == 0 ? Value() : fromCF_C(CFValue(L % R));
        case MENOR:        return Value::makeBool(L <  R);
        case MENORI:       return Value::makeBool(L <= R);
        case MAYOR:        return Value::makeBool(L >  R);
        case MAYORI:       return Value::makeBool(L >= R);
        case IGUALIGUAL:   return Value::makeBool(L == R);
        case DIFERENTE_OP: return Value::makeBool(L != R);
        case AND:          return Value::makeBool(L != 0 && R != 0);
        case OR:           return Value::makeBool(L != 0 || R != 0);
        case REFERENCIA:   return fromCF_C(CFValue(L & R));
        default:           return Value();
    }
}

Value Cascada::visit(NotExp* exp) {
    if (!exp->exp) return Value();
    CFValue v = fold(exp->exp);
    if (!v.is_const) return Value();
    return Value::makeBool(v.int_val == 0);
}

Value Cascada::visit(UnaryExp* exp) {
    if (!exp->exp) return Value();
    CFValue v = fold(exp->exp);
    if (!v.is_const) return Value();
    switch (exp->op) {
        case UnaryExp::NEGATE: return fromCF_C(CFValue(-v.int_val));
        case UnaryExp::NOT_OP: return Value::makeBool(v.int_val == 0);
        default:               return Value();
    }
}
// =============================================================================
//  Cascada — statements
// =============================================================================

void Cascada::visit(AsignStmt* stm) {
    if (!stm->exp) return;
    CFValue v = fold(stm->exp);
    if (v.is_const) env[stm->variable] = v;
    else            env.erase(stm->variable);
}

void Cascada::visit(VarDec* vd) {
    if (!vd->exp) return;
    CFValue v = fold(vd->exp);
    if (v.is_const) env[vd->nombre] = v;
}

void Cascada::visit(ConstDec* cd) {
    if (!cd->exp) return;
    CFValue v = fold(cd->exp);
    if (v.is_const) env[cd->nombre] = v;
}

void Cascada::visit(IfStmt* stm) {
    if (!stm->condicion) return;
    CFValue cond = fold(stm->condicion);
    if (cond.is_const) {
        if (cond.int_val != 0) {
            if (stm->cuerpodelif) stm->cuerpodelif->accept(this);
        } else {
            if (stm->hayelse && stm->cuerpodelelse) stm->cuerpodelelse->accept(this);
        }
        return;
    }
    if (stm->cuerpodelif) stm->cuerpodelif->accept(this);
    if (stm->hayelse && stm->cuerpodelelse) stm->cuerpodelelse->accept(this);
}

void Cascada::visit(WhileStmt* stm) {
    if (stm->condicion) {
        CFValue cond = fold(stm->condicion);
        // while(false) → eliminar cuerpo completo
        if (cond.is_const && cond.int_val == 0) return;
    }
    // Conservador: invalida todo lo modificado dentro del bucle
    auto saved = env;
    for (Stmt* s : stm->cuerpodelwhile) s->accept(this);
    env = saved; // las asignaciones dentro del loop no son estables
}

void Cascada::visit(ForStmt* stm) {
    if (stm->asignacion) stm->asignacion->accept(this);
    if (stm->condicion) {
        CFValue cond = fold(stm->condicion);
        if (cond.is_const && cond.int_val == 0) return;
    }
    auto saved = env;
    if (stm->cuerpo)     stm->cuerpo->accept(this);
    if (stm->incremento) stm->incremento->accept(this);
    env = saved;
}

void Cascada::visit(PrintStmt*      stm) { if (stm->exp)  stm->exp->accept(this); }
void Cascada::visit(ReturnStm*      stm) { if (stm->exp)  stm->exp->accept(this); }
void Cascada::visit(DeleteStm*      stm) { if (stm->exp)  stm->exp->accept(this); }
void Cascada::visit(BodyStmt*       stm) { if (stm->cuerpo) stm->cuerpo->accept(this); }
void Cascada::visit(ContinueStm*    stm) {}
void Cascada::visit(BreakStmt*      stm) { if (stm->tiene_valor && stm->valor) stm->valor->accept(this); }
void Cascada::visit(DeferStmt*      stm) { if (stm->stmt) stm->stmt->accept(this); }
void Cascada::visit(TryStmt*        stm) { if (stm->try_body) stm->try_body->accept(this); }
void Cascada::visit(DerefAssignStmt* stm){ if (stm->lval) stm->lval->accept(this); if (stm->rval) stm->rval->accept(this); }

void Cascada::visit(SwitchStmt* stm) {
    if (stm->condicion) stm->condicion->accept(this);
    for (auto& c : stm->casos) { c.first->accept(this); c.second->accept(this); }
    if (stm->default_caso) stm->default_caso->accept(this);
}

void Cascada::visit(Structdec* sd) {}

void Cascada::visit(Fundec* fd) {
    auto saved = env; env.clear();
    if (fd->cuerpo) fd->cuerpo->accept(this);
    env = saved;
}

void Cascada::visit(Template* t) {
    auto saved = env; env.clear();
    if (t->block) t->block->accept(this);
    env = saved;
}

void Cascada::visit(IdType*       t) {}
void Cascada::visit(PointerType*  t) {}
void Cascada::visit(ArrayType*    t) {}
void Cascada::visit(OptionalType* t) {}
void Cascada::visit(ErrorType*    t) {}
void Cascada::visit(UnionType*    t) {}
void Cascada::visit(EnumType*     t) {}

void Cascada::visit(Body*     b) { for (Stmt* s : b->slist) s->accept(this); }
void Cascada::visit(Programa* p) { for (Top_dec* d : p->declist) d->accept(this); }

// =============================================================================
//  Peephole — utilidades de texto
// =============================================================================

std::string Peephole::trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool Peephole::isLabel(const std::string& s) {
    std::string t = trim(s);
    return !t.empty() && t.back() == ':' && t.find(' ') == std::string::npos;
}

std::string Peephole::getLabel(const std::string& s) {
    // "    jmp   label_3" → "label_3"
    std::string t = trim(s);
    size_t sp = t.find_last_of(" \t");
    return (sp != std::string::npos) ? trim(t.substr(sp + 1)) : "";
}

bool Peephole::isPow2(long long n, int& shift) {
    if (n <= 0 || (n & (n - 1)) != 0) return false;
    shift = 0;
    while (n > 1) { n >>= 1; ++shift; }
    return true;
}

// =============================================================================
//  Peephole — reglas
// =============================================================================

// mov %rax, %rax  →  eliminar
bool Peephole::ruleRedundantMov(std::vector<std::string>& w, int i) {
    std::string t = trim(w[i]);
    // movq %X, %X
    if (t.rfind("movq", 0) != 0) return false;
    size_t comma = t.find(',');
    if (comma == std::string::npos) return false;
    std::string src = trim(t.substr(5, comma - 5));
    std::string dst = trim(t.substr(comma + 1));
    if (src == dst && !src.empty()) { w.erase(w.begin() + i); return true; }
    return false;
}

// pushq %rax  +  popq %rax  →  eliminar ambas
bool Peephole::rulePushPop(std::vector<std::string>& w, int i) {
    if (i + 1 >= (int)w.size()) return false;
    std::string a = trim(w[i]), b = trim(w[i+1]);
    if (a.rfind("pushq", 0) != 0 || b.rfind("popq", 0) != 0) return false;
    std::string ra = trim(a.substr(6));
    std::string rb = trim(b.substr(5));
    if (ra == rb) { w.erase(w.begin()+i, w.begin()+i+2); return true; }
    // pushq %rax + popq %rcx → movq %rax, %rcx
    if (!ra.empty() && !rb.empty()) {
        w[i] = "    movq  " + ra + ", " + rb;
        w.erase(w.begin()+i+1);
        return true;
    }
    return false;
}

// movq A, %rax  +  movq A, %rax  →  eliminar duplicado
bool Peephole::ruleMovThenMov(std::vector<std::string>& w, int i) {
    if (i + 1 >= (int)w.size()) return false;
    std::string a = trim(w[i]), b = trim(w[i+1]);
    if (a == b && a.rfind("movq", 0) == 0) { w.erase(w.begin()+i+1); return true; }
    return false;
}

// addq $0, %rax → eliminar
bool Peephole::ruleAddZero(std::vector<std::string>& w, int i) {
    std::string t = trim(w[i]);
    if (t == "addq  $0, %rax" || t == "addq $0, %rax") { w.erase(w.begin()+i); return true; }
    return false;
}

// subq $0, %rax → eliminar
bool Peephole::ruleSubZero(std::vector<std::string>& w, int i) {
    std::string t = trim(w[i]);
    if (t == "subq  $0, %rax" || t == "subq $0, %rax") { w.erase(w.begin()+i); return true; }
    return false;
}

// imulq $1, %rax → eliminar
bool Peephole::ruleMulOne(std::vector<std::string>& w, int i) {
    std::string t = trim(w[i]);
    if (t == "imulq $1, %rax" || t == "imulq  $1, %rax") { w.erase(w.begin()+i); return true; }
    return false;
}

// jmp Lx  seguido inmediatamente de "Lx:"  →  eliminar el jmp
bool Peephole::ruleJmpToNext(std::vector<std::string>& w, int i) {
    if (i + 1 >= (int)w.size()) return false;
    std::string a = trim(w[i]);
    if (a.rfind("jmp", 0) != 0) return false;
    std::string lbl = getLabel(w[i]);
    std::string next = trim(w[i+1]);
    if (next == lbl + ":") { w.erase(w.begin()+i); return true; }
    return false;
}

// jmp L1  + L1: jmp L2  →  jmp L2
bool Peephole::ruleJmpToJmp(std::vector<std::string>& w, int i) {
    if (i + 2 >= (int)w.size()) return false;
    std::string a = trim(w[i]);
    if (a.rfind("jmp", 0) != 0) return false;
    std::string lbl1 = getLabel(w[i]);
    std::string mid  = trim(w[i+1]);
    std::string next = trim(w[i+2]);
    if (mid == lbl1 + ":" && next.rfind("jmp", 0) == 0) {
        std::string lbl2 = getLabel(w[i+2]);
        w[i] = "    jmp   " + lbl2;
        return true;
    }
    return false;
}

// xorq %rax,%rax  seguido de  movq $k,%rax  →  solo movq
bool Peephole::ruleXorThenMov(std::vector<std::string>& w, int i) {
    if (i + 1 >= (int)w.size()) return false;
    std::string a = trim(w[i]), b = trim(w[i+1]);
    if ((a == "xorq  %rax, %rax" || a == "xorq %rax, %rax") &&
        b.rfind("movq", 0) == 0 && b.find("%rax") != std::string::npos) {
        w.erase(w.begin()+i);
        return true;
    }
    return false;
}

// leave + ret duplicados (dos leave+ret consecutivos) → uno solo
bool Peephole::ruleLeaveRet(std::vector<std::string>& w, int i) {
    if (i + 3 >= (int)w.size()) return false;
    if (trim(w[i])=="leave" && trim(w[i+1])=="ret" &&
        trim(w[i+2])=="leave" && trim(w[i+3])=="ret") {
        w.erase(w.begin()+i+2, w.begin()+i+4);
        return true;
    }
    return false;
}

// imulq $2^n, %rax  →  shlq $n, %rax
bool Peephole::ruleMulPow2(std::vector<std::string>& w, int i) {
    std::string t = trim(w[i]);
    if (t.rfind("imulq", 0) != 0) return false;
    size_t dollar = t.find('$');
    size_t comma  = t.find(',');
    if (dollar == std::string::npos || comma == std::string::npos) return false;
    std::string numStr = trim(t.substr(dollar+1, comma-dollar-1));
    long long n = 0;
    try { n = std::stoll(numStr); } catch(...) { return false; }
    int shift = 0;
    if (!isPow2(n, shift)) return false;
    std::string dst = trim(t.substr(comma+1));
    w[i] = "    shlq  $" + std::to_string(shift) + ", " + dst;
    return true;
}

// idivq $2^n  (precedido de cqto) → sarq $n, %rax
// Patrón: cqto / idivq $k → sarq $n, %rax (sólo enteros positivos garantizados)
bool Peephole::ruleDivPow2(std::vector<std::string>& w, int i) {
    if (i + 1 >= (int)w.size()) return false;
    std::string a = trim(w[i]), b = trim(w[i+1]);
    if (a != "cqto") return false;
    if (b.rfind("idivq", 0) != 0) return false;
    size_t dollar = b.find('$');
    if (dollar == std::string::npos) return false;
    std::string numStr = trim(b.substr(dollar+1));
    long long n = 0;
    try { n = std::stoll(numStr); } catch(...) { return false; }
    int shift = 0;
    if (!isPow2(n, shift)) return false;
    // Reemplaza cqto + idivq $2^n con sarq $n, %rax
    w[i]   = "    sarq  $" + std::to_string(shift) + ", %rax";
    w.erase(w.begin()+i+1);
    return true;
}

// =============================================================================
//  Peephole — optimize (loop principal)
// =============================================================================

std::vector<std::string> Peephole::optimize(const std::vector<std::string>& input) {
    std::vector<std::string> w = input;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < (int)w.size(); ) {
            bool hit =
                ruleRedundantMov(w, i) ||
                rulePushPop     (w, i) ||
                ruleMovThenMov  (w, i) ||
                ruleAddZero     (w, i) ||
                ruleSubZero     (w, i) ||
                ruleMulOne      (w, i) ||
                ruleJmpToJmp    (w, i) ||
                ruleJmpToNext   (w, i) ||
                ruleXorThenMov  (w, i) ||
                ruleLeaveRet    (w, i) ||
                ruleDivPow2     (w, i) ||
                ruleMulPow2     (w, i);
            if (hit) changed = true;
            else     ++i;
        }
    }
    return w;
}

// =============================================================================
//  Peephole — todos los visit son no-ops (interfaz requerida)
// =============================================================================

Value Peephole::visit(BinaryExp*                 e) { return Value(); }
Value Peephole::visit(NumberExpDecimal*          e) { return Value(); }
Value Peephole::visit(NumberExpFlotante*         e) { return Value(); }
Value Peephole::visit(StringExp*                 e) { return Value(); }
Value Peephole::visit(CharExp*                   e) { return Value(); }
Value Peephole::visit(IdExp*                     e) { return Value(); }
Value Peephole::visit(BoolExp*                   e) { return Value(); }
Value Peephole::visit(NotExp*                    e) { return Value(); }
Value Peephole::visit(FcallExp*                  e) { return Value(); }
Value Peephole::visit(UnaryExp*                  e) { return Value(); }
Value Peephole::visit(NewExp*                    e) { return Value(); }
Value Peephole::visit(NullExp*                   e) { return Value(); }
Value Peephole::visit(UndefinedExp*              e) { return Value(); }
Value Peephole::visit(ReferenceExp*              e) { return Value(); }
Value Peephole::visit(PunteroExp*                e) { return Value(); }
Value Peephole::visit(AlgoconcorchetesylistaExp* e) { return Value(); }
Value Peephole::visit(AlgoconcorchetesExp*       e) { return Value(); }
Value Peephole::visit(PuntoExp*                  e) { return Value(); }
Value Peephole::visit(LambdaExp*                 e) { return Value(); }

void Peephole::visit(IfStmt*          s) {}
void Peephole::visit(WhileStmt*       s) {}
void Peephole::visit(BodyStmt*        s) {}
void Peephole::visit(AsignStmt*       s) {}
void Peephole::visit(PrintStmt*       s) {}
void Peephole::visit(ReturnStm*       s) {}
void Peephole::visit(DeleteStm*       s) {}
void Peephole::visit(ContinueStm*     s) {}
void Peephole::visit(BreakStmt*       s) {}
void Peephole::visit(SwitchStmt*      s) {}
void Peephole::visit(TryStmt*         s) {}
void Peephole::visit(DeferStmt*       s) {}
void Peephole::visit(ForStmt*         s) {}
void Peephole::visit(DerefAssignStmt* s) {}
void Peephole::visit(Fundec*          f) {}
void Peephole::visit(Structdec*       s) {}
void Peephole::visit(VarDec*          v) {}
void Peephole::visit(ConstDec*        c) {}
void Peephole::visit(Template*        t) {}
void Peephole::visit(IdType*          t) {}
void Peephole::visit(PointerType*     t) {}
void Peephole::visit(ArrayType*       t) {}
void Peephole::visit(OptionalType*    t) {}
void Peephole::visit(ErrorType*       t) {}
void Peephole::visit(UnionType*       t) {}
void Peephole::visit(EnumType*        t) {}
void Peephole::visit(Body*            b) {}
void Peephole::visit(Programa*        p) {}