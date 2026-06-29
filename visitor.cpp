#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "ast.h"
#include "visitor.h"
#include <cstdint>
#include "ConstantFoldingVisitor.h"
#include "Sethi-UllmanVisitor.h"
#include "DeadcodeeliminationVisitor.h"
#include "PeepholeVisitor.h"

using namespace std;

// ── Registros de argumentos (System V AMD64) ─────────────
static const char* argRegs[] = {
    "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
};
static const int MAX_REG_ARGS = 6;

// =============================================================================
//  Helpers internos (no miembro)
// =============================================================================

// Escapa caracteres especiales de un string para .string de GAS
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

// ── Clasifica el tipo de una expresión para saber qué formato usar al imprimir
static std::string inferGlobalType(Exp* exp) {
    if (!exp)                                  return "int";
    if (dynamic_cast<NumberExpFlotante*>(exp)) return "float";
    if (dynamic_cast<StringExp*>(exp))         return "str";
    if (dynamic_cast<CharExp*>(exp))           return "char";
    if (dynamic_cast<BoolExp*>(exp))           return "bool";
    return "int";
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
        cout << vd->nombre << ": .quad " << bits << endl;  // bits de double

    } else if (gtype == "str") {
        // El puntero al string se inicializa en main_init o similar;
        // por simplicidad guardamos la dirección vía .quad con reloc.
        // Guardamos el label del string como quad (requiere enlazador).
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
        // int / hex / binario / null / undefined → todo cabe en .quad
        int64_t val = 0;
        if (NumberExpDecimal* ne = dynamic_cast<NumberExpDecimal*>(vd->exp))
            val = ne->value;
        // null/undefined → 0 ya está
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
//  gencode — punto de entrada público
// =============================================================================

void GenCodeVisitor::gencode(Programa* programa) {


ConstantFoldingPass cf;
cf.run(programa);


   DeadCodeEliminationPass dce;
    dce.run(programa, constMap);

    // ── Pase de Sethi-Ullman ───────────────────────────────────────────
    SethiUllmanPass su;
su.run(programa);

    std::ostringstream asmBuffer;
    std::streambuf* realCoutBuf = std::cout.rdbuf(asmBuffer.rdbuf());



    // ── Pase 0: pre-registrar structs para conocer offsets de campos ──────
    for (Top_dec* d : programa->declist) {
        if (Structdec* sd = dynamic_cast<Structdec*>(d)) {
            for (size_t i = 0; i < sd->id_parametros.size(); ++i)
                structFieldOffsets[sd->nombre][sd->id_parametros[i]] = (int)i * 8;
            structFieldCount[sd->nombre] = (int)sd->id_parametros.size();
        }
        // Unions también se registran como structs
        if (VarDec* vd = dynamic_cast<VarDec*>(d)) {
            if (UnionType* ut = dynamic_cast<UnionType*>(vd->tipo)) {
                for (size_t i = 0; i < ut->campo_nombres.size(); ++i)
                    structFieldOffsets[ut->nombre][ut->campo_nombres[i]] = (int)i * 8;
                structFieldCount[ut->nombre] = (int)ut->campo_nombres.size();
            }
        }
    }

    // ── Pase 1: registrar todas las funciones para forward-references ─────
    for (Top_dec* d : programa->declist)
        if (Fundec* fd = dynamic_cast<Fundec*>(d))
            funEnv[fd->nombre] = fd;

    // ── Sección .data ─────────────────────────────────────────────────────
    cout << ".section .data"                           << endl;
    cout << "print_int_fmt:   .string \"%ld\\n\""     << endl;
    cout << "print_uint_fmt:  .string \"%lu\\n\""     << endl;
    cout << "print_float_fmt: .string \"%g\\n\""      << endl;
    cout << "print_str_fmt:   .string \"%s\\n\""      << endl;
    cout << "print_char_fmt:  .string \"%c\\n\""      << endl;
    cout << "print_bool_true: .string \"true\\n\""    << endl;
    cout << "print_bool_false:.string \"false\\n\""   << endl;
    cout << endl;

    // ── Pase 2: emitir variables/constantes globales en .data ─────────────
    for (Top_dec* d : programa->declist) {
        if (VarDec* vd = dynamic_cast<VarDec*>(d))
            emitGlobalVarDec(vd);
        else if (ConstDec* cd = dynamic_cast<ConstDec*>(d))
            emitGlobalConstDec(cd);
    }

    cout << ".section .text" << endl;

    // ── Sección .text ─────────────────────────────────────────────────────
    cout << ".globl main"    << endl;

    programa->accept(this);

    // ── Sección .rodata con literales de string ───────────────────────────
    if (!stringLiterals.empty()) {
        cout << endl;
        cout << ".section .rodata" << endl;
        for (auto& p : stringLiterals)
            cout << p.first << ": .string \"" << escapeString(p.second) << "\"" << endl;
    }

    // ★ PASO 2: restaurar cout y pasar por Peephole (SEGUNDO CAMBIO, al final)
    std::cout.rdbuf(realCoutBuf);
    PeepholePass pp;
    pp.optimize(asmBuffer.str(), std::cout);

}

// =============================================================================
//  Programa
// =============================================================================

void GenCodeVisitor::visit(Programa* p) {
    for (Top_dec* d : p->declist)
        if (Fundec* fd = dynamic_cast<Fundec*>(d))
            if (fd->nombre == "__comptime__")
                hayComptimeGlobal = true;

    for (Top_dec* d : p->declist)
        d->accept(this);
}

// =============================================================================
//  emitAlignedCall
//  Guarda %rsp en %r12 (callee-saved), alinea a 16, llama, restaura.
//  Antes de usarlo en funciones que también usan %r12 para printf, hay que
//  salvar/restaurar %r12 en el prólogo/epílogo de la función.  Como aquí cada
//  función generada tiene su propio frame grande, es seguro reutilizarlo.
// =============================================================================

void GenCodeVisitor::emitAlignedCall(const string& target) {
    cout << "    pushq %r12"           << endl;   // salvar callee-saved
    cout << "    movq  %rsp, %r12"     << endl;
    cout << "    andq  $-16, %rsp"     << endl;
    cout << "    xorq  %rax, %rax"     << endl;   // 0 regs XMM para varargs
    cout << "    call  " << target     << endl;
    cout << "    movq  %r12, %rsp"     << endl;
    cout << "    popq  %r12"           << endl;   // restaurar
}

// =============================================================================
//  Fundec — generación de función
// =============================================================================

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

    // ── Si es main y hay comptime, llamarlo primero ───────────────────────
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
// =============================================================================
//  Template — instancia genérica tratada como función concreta
// =============================================================================

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

// =============================================================================
//  Structdec — solo registra offsets (ya hecho en gencode()), no emite código
// =============================================================================

void GenCodeVisitor::visit(Structdec* sd) {
    // Los offsets ya fueron registrados en gencode().
    // No se emite ninguna instrucción: los structs son solo layout.
}

// =============================================================================
//  Body
// =============================================================================

void GenCodeVisitor::visit(Body* b) {
    for (Stmt* s : b->slist)
        s->accept(this);
}

// =============================================================================
//  VarDec / ConstDec
//
//  Casos especiales:
//   - Tipo struct: reservar N campos consecutivos; guardar dirección base.
//   - Tipo array [N]T: reservar N slots consecutivos; guardar dirección base.
//   - Caso normal: 1 slot, evaluar expresión inicializadora.
// =============================================================================

void GenCodeVisitor::visit(VarDec* vd) {
    if (currentFunction.empty()) return;  // ignorar globales por ahora

    // ── ¿Es un struct? ────────────────────────────────────────────────────
    if (vd->tienetipo && vd->tipo) {
        if (IdType* it = dynamic_cast<IdType*>(vd->tipo)) {
            auto sit = structFieldOffsets.find(it->id);
            if (sit != structFieldOffsets.end()) {
                int nFields = structFieldCount[it->id];
                // Reservar nFields slots consecutivos
                int baseSlot = varContador;
                posicion[vd->nombre] = baseSlot;  // slot del primer campo
                varContador += nFields;
                // La dirección base es leaq de ese slot
                // (se carga con leaq cuando se usa el nombre)
                // Si hay expresión inicializadora, la ignoramos por ahora
                // (struct literals son complejos; se asignan campo a campo)
                return;
            }
        }
        // ── ¿Es un array? ────────────────────────────────────────────────
        if (ArrayType* at = dynamic_cast<ArrayType*>(vd->tipo)) {
            // Intentamos obtener el tamaño en tiempo de compilación
            int arraySize = 8; // tamaño por defecto si no podemos evaluar
            if (NumberExpDecimal* ne = dynamic_cast<NumberExpDecimal*>(at->exp1))
                arraySize = ne->value;

            int baseSlot = varContador;
            posicion[vd->nombre] = baseSlot;
            varContador += arraySize;

            // Si hay inicializador, lo evaluamos como dirección base
            if (vd->exp) {
                vd->exp->accept(this);
                // El resultado en %rax es la dirección base del array fuente;
                // copiamos elemento por elemento sería ideal, pero como
                // simplificación guardamos la dirección en el primer slot.
                cout << "    movq  %rax, " << offset(vd->nombre) << endl;
            } else {
                // Inicializar como puntero a frame local (leaq)
                cout << "    leaq  " << (baseSlot * -8) << "(%rbp), %rax" << endl;
                cout << "    movq  %rax, " << offset(vd->nombre) << endl;
            }
            return;
        }
    }

    // ── Caso general: variable escalar ────────────────────────────────────
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
//  AsignStmt
// =============================================================================

void GenCodeVisitor::visit(AsignStmt* stm) {
    // Llamada de función usada como sentencia
    if (stm->variable == "__call__") {
        if (stm->exp) stm->exp->accept(this);
        return;
    }

    if (posicion.find(stm->variable) == posicion.end())
        posicion[stm->variable] = varContador++;

    stm->exp->accept(this);
    cout << "    movq  %rax, " << offset(stm->variable) << endl;
}

// =============================================================================
//  DerefAssignStmt  — *lval = rval  o  array[idx] = rval  o  struct.campo = rval
//
//  Protocolo:
//    1. eval(rval) → %rax → pushq       (valor a almacenar)
//    2. calcular dirección del lval → %rcx
//    3. popq %rdx
//    4. movq %rdx, (%rcx)
// =============================================================================

void GenCodeVisitor::visit(DerefAssignStmt* stm) {
    // 1. Evaluar rval y apilar
    stm->rval->accept(this);
    cout << "    pushq %rax" << endl;

    // 2. Calcular dirección del lval
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
        // *ptr = val  →  dirección = valor almacenado en ptr
        ue->exp->accept(this);   // %rax = valor de ptr = dirección destino ✓

    } else if (IdExp* id = dynamic_cast<IdExp*>(stm->lval)) {
        // *ptr = val → la dirección destino ES el valor almacenado en ptr
        if (posicion.find(id->value) == posicion.end())
            posicion[id->value] = varContador++;
        // ANTES: leaq (incorrecto, da &ptr)
        // AHORA: movq (correcto, da *ptr = valor de ptr = dirección de a)
        cout << "    movq  " << offset(id->value) << ", %rax" << endl;

    } else {
        stm->lval->accept(this);
    }

    // 3-4. Almacenar
    cout << "    movq  %rax, %rcx"   << endl;
    cout << "    popq  %rdx"         << endl;
    cout << "    movq  %rdx, (%rcx)" << endl;
}
// =============================================================================
//  PrintStmt — detección de tipo para elegir formato printf correcto
//
//  Heurística en tiempo de compilación:
//   StringExp          → print_str_fmt   (%s)
//   CharExp            → print_char_fmt  (%c)
//   BoolExp            → imprime "true"/"false" directamente
//   NumberExpFlotante  → print_float_fmt (%g)
//   Cualquier otra     → print_int_fmt   (%ld)
//
//  Para variables (IdExp) no tenemos información de tipo en el gencode,
//  así que por defecto usamos %ld.  Si el lenguaje requiere precisión,
//  el TypeChecker debería propagar esa información hasta aquí.
// =============================================================================

void GenCodeVisitor::visit(PrintStmt* stm) {
    Exp* e = stm->exp;

    // ── Detectar si es una variable global con tipo conocido ──────────────
    string knownType = "";
    if (IdExp* id = dynamic_cast<IdExp*>(e)) {
        auto it = globalTypes.find(id->value);
        if (it != globalTypes.end())
            knownType = it->second;
    }

    // ── Bool global ───────────────────────────────────────────────────────
    if (knownType == "bool") {
        // No podemos saber el valor en compile-time; usamos comparación runtime
        e->accept(this);  // valor (0 o 1) → %rax
        string labelTrue  = newLabel("bool_true");
        string labelDone  = newLabel("bool_done");
        cout << "    cmpq  $0, %rax"         << endl;
        cout << "    jne   " << labelTrue     << endl;
        cout << "    leaq  print_bool_false(%rip), %rdi" << endl;
        cout << "    jmp   " << labelDone     << endl;
        cout << labelTrue << ":"              << endl;
        cout << "    leaq  print_bool_true(%rip), %rdi"  << endl;
        cout << labelDone << ":"              << endl;
        cout << "    pushq %r12"             << endl;
        cout << "    movq  %rsp, %r12"       << endl;
        cout << "    andq  $-16, %rsp"       << endl;
        cout << "    xorq  %rax, %rax"       << endl;
        cout << "    call  puts@PLT"         << endl;
        cout << "    movq  %r12, %rsp"       << endl;
        cout << "    popq  %r12"             << endl;
        return;
    }

    // ── Bool literal (igual que antes) ────────────────────────────────────
    if (BoolExp* be = dynamic_cast<BoolExp*>(e)) {
        string lbl = (be->booleano == "true") ? "print_bool_true" : "print_bool_false";
        cout << "    leaq  " << lbl << "(%rip), %rdi" << endl;
        cout << "    pushq %r12" << endl;
        cout << "    movq  %rsp, %r12" << endl;
        cout << "    andq  $-16, %rsp" << endl;
        cout << "    xorq  %rax, %rax" << endl;
        cout << "    call  puts@PLT"   << endl;
        cout << "    movq  %r12, %rsp" << endl;
        cout << "    popq  %r12"       << endl;
        return;
    }

    // ── Evaluar expresión → %rax ──────────────────────────────────────────
    e->accept(this);

    cout << "    pushq %r12"       << endl;
    cout << "    movq  %rsp, %r12" << endl;
    cout << "    andq  $-16, %rsp" << endl;
    cout << "    movq  %rax, %rsi" << endl;

    // Elegir formato (primero por tipo global, luego por tipo AST)
    if (knownType == "float" || dynamic_cast<NumberExpFlotante*>(e)) {
        cout << "    movq  %rax, %xmm0"                 << endl;
        cout << "    leaq  print_float_fmt(%rip), %rdi" << endl;
        cout << "    movq  $1, %rax"                     << endl;
        cout << "    call  printf@PLT"                   << endl;
        cout << "    movq  %r12, %rsp"                   << endl;
        cout << "    popq  %r12"                         << endl;
        return;
    } else if (knownType == "str" || dynamic_cast<StringExp*>(e)) {
        cout << "    leaq  print_str_fmt(%rip), %rdi"   << endl;
    } else if (knownType == "char" || dynamic_cast<CharExp*>(e)) {
        cout << "    leaq  print_char_fmt(%rip), %rdi"  << endl;
    } else {
        cout << "    leaq  print_int_fmt(%rip), %rdi"   << endl;
    }

    cout << "    xorq  %rax, %rax"  << endl;
    cout << "    call  printf@PLT"   << endl;
    cout << "    movq  %r12, %rsp"   << endl;
    cout << "    popq  %r12"         << endl;
}

// =============================================================================
//  ReturnStm
// =============================================================================

void GenCodeVisitor::visit(ReturnStm* stm) {
    if (stm->exp)
        stm->exp->accept(this);
    else
        cout << "    xorq  %rax, %rax" << endl;

    cout << "    leave" << endl;
    cout << "    ret"   << endl;
}

// =============================================================================
//  IfStmt
// =============================================================================

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

// =============================================================================
//  WhileStmt
// =============================================================================

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

// =============================================================================
//  ForStmt
//
//  El parser genera siempre:
//    asignacion = AsignStmt(elemVar, iterable)
//    condicion  = NullExp()   → for-each sobre array
//               = <expr>      → for clásico (raro en este parser)
//    incremento = AsignStmt(idxVar, NullExp())
//    cuerpo     = Body
//
//  For-each:
//    - Carga la dirección base del array en __arr_base__
//    - Inicializa idxVar = 0
//    - En cada iteración: elemVar = *(base + idx*8),  idx++
//    - El bucle es infinito; el usuario usa break para salir,
//      o bien el array tiene un centinela conocido.
//
//  For clásico (condición != NullExp):
//    init → check → cuerpo → inc → check → ...
// =============================================================================

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

        // Evaluar iterable → dirección base del array en %rax
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

        // idx = 0
        cout << "    movq  $0, %rax" << endl;
        cout << "    movq  %rax, " << offset(idxVar) << endl;

        cout << labelStart << ":" << endl;

        // elem = base[idx]
        cout << "    movq  " << offset(idxVar)  << ", %rax" << endl;
        cout << "    movq  " << offset(baseVar) << ", %rcx" << endl;
        cout << "    movq  (%rcx,%rax,8), %rax"             << endl;
        cout << "    movq  %rax, " << offset(elemVar)       << endl;

        if (stm->cuerpo) stm->cuerpo->accept(this);

        // idx++
        cout << "    movq  " << offset(idxVar) << ", %rax" << endl;
        cout << "    addq  $1, %rax"                        << endl;
        cout << "    movq  %rax, " << offset(idxVar)        << endl;

        cout << "    jmp   " << labelStart << endl;
        cout << labelEnd << ":"            << endl;

    } else {
        // For clásico
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

// =============================================================================
//  BreakStmt / ContinueStm
// =============================================================================

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

// =============================================================================
//  SwitchStmt
//
//  Esquema:
//    eval(condicion) → pushq %rax       ← valor del switch permanece en stack
//    para cada caso:
//      eval(patron) → %rax
//      movq %rax, %rcx
//      movq 0(%rsp), %rax              ← peek (no pop)
//      cmpq %rcx, %rax
//      jne  siguiente_caso
//      <body>
//      addq $8, %rsp                   ← pop del valor del switch
//      jmp  switch_end
//    [default]
//    addq $8, %rsp
//    switch_end:
// =============================================================================

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

// =============================================================================
//  BodyStmt
// =============================================================================

void GenCodeVisitor::visit(BodyStmt* stm) {
    if (stm->cuerpo) stm->cuerpo->accept(this);
}

// =============================================================================
//  DeleteStm (free)
// =============================================================================

void GenCodeVisitor::visit(DeleteStm* stm) {
    stm->exp->accept(this);
    cout << "    movq  %rax, %rdi" << endl;
    emitAlignedCall("free@PLT");
}

// =============================================================================
//  DeferStmt — ejecuta el stmt al salir del scope (simplificación: inline)
//  En una implementación completa se usaría una lista de cleanup al final
//  de cada bloque. Aquí lo emitimos inmediatamente (semántica incorrecta
//  pero permite compilar sin crash).
// =============================================================================

void GenCodeVisitor::visit(DeferStmt* stm) {
    // TODO: implementar cleanup stack para semántica correcta de defer.
    // Por ahora emitimos el statement en el lugar (incorrecto pero seguro).
    if (stm->stmt) stm->stmt->accept(this);
}

// =============================================================================
//  TryStmt
// =============================================================================

void GenCodeVisitor::visit(TryStmt* stm) {
    // Ejecutar el cuerpo try normalmente.
    // El manejo real de errores requeriría setjmp/longjmp o similar.
    if (stm->try_body) stm->try_body->accept(this);
    // El catch se omite (no hay mecanismo de excepción a nivel de C en este stub).
}

// =============================================================================
//  Expresiones numéricas y literales
// =============================================================================

Value GenCodeVisitor::visit(NumberExpDecimal* exp) {
    cout << "    movq  $" << exp->value << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(NumberExpFlotante* exp) {
    // Representamos el float como sus bits de double en %rax.
    // Para operaciones aritméticas de float habría que mover a %xmm0;
    // para print lo detectamos en PrintStmt.
    double  f64  = (double)exp->value;
    int64_t bits = 0;
    memcpy(&bits, &f64, sizeof(bits));
    cout << "    movabsq $" << bits << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(BoolExp* exp) {
    int val = (exp->booleano == "true") ? 1 : 0;
    cout << "    movq  $" << val << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(NullExp* exp) {
    cout << "    xorq  %rax, %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(UndefinedExp* exp) {
    cout << "    xorq  %rax, %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(StringExp* exp) {
    string lbl = internString(exp->valor);
    cout << "    leaq  " << lbl << "(%rip), %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(CharExp* exp) {
    cout << "    movq  $" << (int)(unsigned char)exp->valor << ", %rax" << endl;
    return Value();
}

// =============================================================================
//  IdExp — carga variable del frame o constante de error
// =============================================================================

Value GenCodeVisitor::visit(IdExp* exp) {
    // Constantes de error
    if (exp->value.size() >= 6 && exp->value.substr(0, 6) == "error.") {
        if (!errorCodes.count(exp->value))
            errorCodes[exp->value] = (int)errorCodes.size() + 1;
        cout << "    movq  $" << errorCodes[exp->value] << ", %rax" << endl;
        return Value();
    }

    // ── NUEVO: variable global ────────────────────────────────────────────
    if (globalNames.count(exp->value)) {
        cout << "    movq  " << exp->value << "(%rip), %rax" << endl;
        return Value();
    }

    // Variable local (igual que antes)
    if (!posicion.count(exp->value)) {
        cerr << "[GenCode] Advertencia: variable no declarada '"
             << exp->value << "' → 0\n";
        cout << "    xorq  %rax, %rax" << endl;
        return Value();
    }
    cout << "    movq  " << offset(exp->value) << ", %rax" << endl;
    return Value();
}

// =============================================================================
//  BinaryExp
//
//  Secuencia estándar:
//    eval(left)  → pushq %rax
//    eval(right) → %rax
//    movq %rax, %rcx   ; right → %rcx
//    popq %rax          ; left  → %rax
//    OP %rcx, %rax      ; resultado → %rax
//
//  AT&T cmpq src, dst  calcula dst - src y fija flags.
//  Con cmpq %rcx, %rax  →  flags de (left - right):
//    setl  → left <  right
//    setle → left <= right
//    setg  → left >  right
//    setge → left >= right
//    sete  → left == right
//    setne → left != right
// =============================================================================
Value GenCodeVisitor::visit(BinaryExp* exp) {
 
    // ─────────────────────────────────────────────────────────────────────
    // 1. CONSTANT FOLDING
    //    Si el nodo completo fue evaluado en compile-time, emitimos un único
    //    movq con el valor pre-calculado. Cero instrucciones aritméticas.
    // ─────────────────────────────────────────────────────────────────────
    auto cfIt = constMap.find(exp);
    if (cfIt != constMap.end() && cfIt->second.isConst) {
        // Resultado entero (la mayoría de casos en minizig)
        long long ival = (long long)cfIt->second.value;
        cout << "    movq  $" << ival << ", %rax" << endl;
        return Value();
    }
 
    // ─────────────────────────────────────────────────────────────────────
    // 2. SETHI-ULLMAN — determinar el orden de evaluación óptimo
    //
    //    Consultamos los labels calculados por SethiUllmanPass:
    //      · Si label(right) > label(left) → right primero
    //      · Si right es hoja constante    → no hay pushq, va directo a %rcx
    //      · Caso general                  → left primero
    // ─────────────────────────────────────────────────────────────────────
    int lLabel = 1, rLabel = 1;
    bool rIsConst = false;
 
    auto lIt = shuMap.find(exp->left);
    auto rIt = shuMap.find(exp->right);
    if (lIt != shuMap.end()) lLabel   = lIt->second.label;
    if (rIt != shuMap.end()) rLabel   = rIt->second.label;
    if (rIt != shuMap.end()) rIsConst = rIt->second.isConst;
 
    // ── Caso A: right es constante → emitir left; usar inmediato ──────────
    //    No necesitamos pushq/popq: left → %rax, right → %rcx directamente.
    if (rIsConst) {
        // Verificamos que también tenemos el valor en constMap
        auto rcfIt = constMap.find(exp->right);
        if (rcfIt != constMap.end() && rcfIt->second.isConst) {
            long long rval = (long long)rcfIt->second.value;
            exp->left->accept(this);                                     // left → %rax
            cout << "    movq  $" << rval << ", %rcx" << endl;          // right como inmediato
            goto emit_op;
        }
    }
 
    // ── Caso B: right más costoso → evaluar right primero ─────────────────
    //    Guardamos right en el stack, evaluamos left, recuperamos right.
    if (rLabel > lLabel) {
        exp->right->accept(this);
        cout << "    pushq %rax"         << endl;  // right al stack
        exp->left->accept(this);
        cout << "    movq  %rax, %rcx"   << endl;  // left  → %rcx (temporal)
        cout << "    popq  %rax"         << endl;  // right → %rax
        cout << "    xchgq %rax, %rcx"   << endl;  // left  → %rax, right → %rcx
        goto emit_op;
    }
 
    // ── Caso C: left primero (orden natural) ──────────────────────────────
    {
        exp->left->accept(this);
        cout << "    pushq %rax"        << endl;
        exp->right->accept(this);
        cout << "    movq  %rax, %rcx"  << endl;
        cout << "    popq  %rax"        << endl;
    }
 
emit_op:
    // ─────────────────────────────────────────────────────────────────────
    // 3. EMISIÓN DE LA OPERACIÓN
    //    Aquí %rax = left, %rcx = right (invariante AT&T: op src, dst)
    // ─────────────────────────────────────────────────────────────────────
    switch (exp->op) {
        case PLUS_OP:
            cout << "    addq  %rcx, %rax"  << endl;
            break;
        case MINUS_OP:
            cout << "    subq  %rcx, %rax"  << endl;
            break;
        case MUL_OP:
            cout << "    imulq %rcx, %rax"  << endl;
            break;
        case DIV_OP:
            cout << "    cqto"              << endl;
            cout << "    idivq %rcx"        << endl;
            break;
        case MODULO_OP:
            cout << "    cqto"              << endl;
            cout << "    idivq %rcx"        << endl;
            cout << "    movq  %rdx, %rax"  << endl;  // resto en %rdx
            break;
        case MENOR:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setl  %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl;
            break;
        case MENORI:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setle %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl;
            break;
        case MAYOR:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setg  %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl;
            break;
        case MAYORI:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setge %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl;
            break;
        case IGUALIGUAL:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    sete  %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl;
            break;
        case DIFERENTE_OP:
            cout << "    cmpq  %rcx, %rax"  << endl;
            cout << "    setne %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl;
            break;
        case AND:
            cout << "    testq %rax, %rax"   << endl;
            cout << "    setne %r10b"         << endl;
            cout << "    testq %rcx, %rcx"   << endl;
            cout << "    setne %r11b"         << endl;
            cout << "    andb  %r11b, %r10b"  << endl;
            cout << "    movzbq %r10b, %rax"  << endl;
            break;
        case OR:
            cout << "    orq   %rcx, %rax"   << endl;
            cout << "    setne %al"           << endl;
            cout << "    movzbq %al, %rax"    << endl;
            break;
        case REFERENCIA:
            cout << "    andq  %rcx, %rax"   << endl;
            break;
        default:
            cerr << "[GenCode] Operador binario no soportado (op=" << exp->op << ")\n";
            break;
    }
    return Value();
}
// =============================================================================
//  NotExp / UnaryExp
// =============================================================================

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
                exp->exp->accept(this);  // ya deja dirección en %rax
            }
            break;
        case UnaryExp::DEREF:
            exp->exp->accept(this);
            cout << "    movq  (%rax), %rax" << endl;
            break;
    }
    return Value();
}

// =============================================================================
//  FcallExp — llamada a función con convención System V AMD64
//
//  Algoritmo:
//    1. Evaluar argumentos de derecha a izquierda y apilar todos.
//    2. Mover los primeros min(N,6) del stack a %rdi..%r9.
//    3. emitAlignedCall.
//    4. Limpiar argumentos extra del stack (si N > 6).
// =============================================================================

Value GenCodeVisitor::visit(FcallExp* exp) {
    // Funciones internas del compilador
    if (exp->nombre == "__try__" || exp->nombre == "__catch__") {
        if (!exp->argumentos.empty())
            exp->argumentos[0]->accept(this);
        return Value();
    }

    int nArgs      = (int)exp->argumentos.size();
    int nRegArgs   = min(nArgs, MAX_REG_ARGS);
    int nStackArgs = max(0, nArgs - MAX_REG_ARGS);

    // Paso 1: evaluar args de derecha a izquierda y apilar
    for (int i = nArgs - 1; i >= 0; --i) {
        exp->argumentos[i]->accept(this);
        cout << "    pushq %rax" << endl;
    }

    // Paso 2: mover primeros nRegArgs al stack → registros
    for (int i = 0; i < nRegArgs; ++i)
        cout << "    popq  " << argRegs[i] << endl;

    // Paso 3: llamada alineada
    emitAlignedCall(exp->nombre);

    // Paso 4: limpiar args extra del stack
    if (nStackArgs > 0)
        cout << "    addq  $" << (nStackArgs * 8) << ", %rsp" << endl;

    return Value();
}

// =============================================================================
//  NewExp — aloca en el heap
//
//  Si el tipo es array [N]T → malloc(N * 8)
//  Si el tipo es struct     → malloc(nFields * 8)
//  Cualquier otro caso      → malloc(8)
// =============================================================================

Value GenCodeVisitor::visit(NewExp* exp) {
    int bytes = 8;

    if (exp->tipo) {
        if (ArrayType* at = dynamic_cast<ArrayType*>(exp->tipo)) {
            if (NumberExpDecimal* ne = dynamic_cast<NumberExpDecimal*>(at->exp1))
                bytes = ne->value * 8;
            else {
                at->exp1->accept(this);
                cout << "    imulq $8, %rax"   << endl;
                cout << "    movq  %rax, %rdi"  << endl;
                emitAlignedCall("malloc@PLT");
                return Value();
            }
        } else if (IdType* it = dynamic_cast<IdType*>(exp->tipo)) {
            auto sit = structFieldCount.find(it->id);
            if (sit != structFieldCount.end())
                bytes = sit->second * 8;
            else
                bytes = 64 * 8; // ← default generoso para tipos escalares usados como array
        }
    }

    cout << "    movq  $" << bytes << ", %rdi" << endl;
    emitAlignedCall("malloc@PLT");
    return Value();
}

// =============================================================================
//  ReferenceExp — &expr → dirección
// =============================================================================

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

// =============================================================================
//  PunteroExp — *ptr → desreferenciar
// =============================================================================

Value GenCodeVisitor::visit(PunteroExp* exp) {
    if (!exp->exp) { cout << "    xorq  %rax, %rax" << endl; return Value(); }
    exp->exp->accept(this);
    cout << "    movq  (%rax), %rax" << endl;
    return Value();
}

// =============================================================================
//  PuntoExp — acceso a campo de struct: expr.campo
//
//  Protocolo:
//    1. Obtener la dirección base del struct (emitStructBaseAddress).
//    2. Sumar el offset del campo.
//    3. Cargar el valor: movq offset(%rax), %rax
//
//  Para structs en el frame, la variable guarda la dirección base
//  (puesta ahí en VarDec con leaq).
//  Para punteros a struct: el valor de la variable es la dirección
//  del struct en el heap → simplemente usamos ese valor + offset.
// =============================================================================

Value GenCodeVisitor::visit(PuntoExp* exp) {
    if (exp->id == "__unwrap__") {
        // optional.? — devuelve el valor (no verifica null en esta implementación)
        exp->exp->accept(this);
        return Value();
    }

    // Obtener dirección base del struct en %rax
    emitStructBaseAddress(exp->exp);

    // Obtener offset del campo
    int fieldOff = getFieldOffset(exp->exp, exp->id);
    cout << "    movq  " << fieldOff << "(%rax), %rax" << endl;
    return Value();
}

// =============================================================================
//  AlgoconcorchetesExp — array[index]
//
//  base → %rax → pushq
//  idx  → %rax
//  popq %rcx
//  movq (%rcx, %rax, 8), %rax
// =============================================================================

Value GenCodeVisitor::visit(AlgoconcorchetesExp* exp) {
    exp->nombre->accept(this);
    cout << "    pushq %rax"                  << endl;
    exp->dentroexp->accept(this);
    cout << "    popq  %rcx"                  << endl;
    cout << "    movq  (%rcx,%rax,8), %rax"  << endl;
    return Value();
}

// =============================================================================
//  AlgoconcorchetesylistaExp — array[a, b, ...]  (multi-índice / 2D)
//
//  Implementación básica: trata como acceso 1D con el primer índice.
//  Para arrays 2D reales habría que conocer el número de columnas.
// =============================================================================

Value GenCodeVisitor::visit(AlgoconcorchetesylistaExp* exp) {
    if (exp->argumentos.empty()) {
        exp->nombre->accept(this);
        return Value();
    }
    // Caso 2D: asumimos [fila, col] con nCols desconocido → tratamos como 1D flat
    exp->nombre->accept(this);
    cout << "    pushq %rax"                 << endl;  // base
    exp->argumentos[0]->accept(this);                   // primer índice
    if (exp->argumentos.size() >= 2) {
        // Para 2D: idx = fila * nCols + col  (nCols desconocido, usamos 0 como fallback)
        cout << "    pushq %rax"             << endl;  // fila
        exp->argumentos[1]->accept(this);               // col → %rax
        cout << "    movq  %rax, %rcx"      << endl;  // col → %rcx
        cout << "    popq  %rax"            << endl;  // fila → %rax
        // Sin nCols, no podemos calcular; dejamos fila como índice
        cout << "    addq  %rcx, %rax"      << endl;  // idx = fila + col (aproximación)
    }
    cout << "    popq  %rcx"                << endl;  // base → %rcx
    cout << "    movq  (%rcx,%rax,8), %rax"<< endl;
    return Value();
}

// =============================================================================
//  LambdaExp — closures completos requieren heap; emitimos 0 por ahora
// =============================================================================

Value GenCodeVisitor::visit(LambdaExp* exp) {
    // Stub: las lambdas necesitan closures con captura del entorno.
    // Retorna null (0) para no crashear.
    cout << "    xorq  %rax, %rax" << endl;
    return Value();
}

// =============================================================================
//  Tipos — no generan código (solo stubs para satisfacer la interfaz)
// =============================================================================

void GenCodeVisitor::visit(IdType*       t) {}
void GenCodeVisitor::visit(PointerType*  t) {}
void GenCodeVisitor::visit(ArrayType*    t) {}
void GenCodeVisitor::visit(OptionalType* t) {}
void GenCodeVisitor::visit(ErrorType*    t) {}
void GenCodeVisitor::visit(UnionType*    t) {}
void GenCodeVisitor::visit(EnumType*     t) {}

// =============================================================================
//  Helpers privados
// =============================================================================

// ── emitStructBaseAddress ────────────────────────────────────────────────────
//  Deja en %rax la dirección base del struct al que apunta expr.
//  - Si expr es IdExp: la variable puede ser:
//      * Struct en frame (variable local de tipo struct): leaq -N*8(%rbp)
//      * Puntero a struct (heap): movq -N*8(%rbp), %rax  (ya tiene la dir)
//    Como no tenemos info de tipo aquí, usamos movq (funciona para punteros)
//    y también para structs en frame si el primer slot guarda la dirección base.

void GenCodeVisitor::emitStructBaseAddress(Exp* expr) {
    if (IdExp* id = dynamic_cast<IdExp*>(expr)) {
        if (!posicion.count(id->value)) {
            cerr << "[GenCode] Advertencia: struct base '" << id->value << "' no declarado\n";
            cout << "    xorq  %rax, %rax" << endl;
            return;
        }
        // El slot de la variable guarda la dirección base del struct
        // (puesta por VarDec con leaq, o por new con malloc)
        cout << "    movq  " << offset(id->value) << ", %rax" << endl;
    } else {
        // Expresión más compleja: evaluarla (debe dejar dir en %rax)
        expr->accept(this);
    }
}

// ── getFieldOffset ───────────────────────────────────────────────────────────
//  Devuelve el byte offset del campo 'fieldName' del struct al que apunta expr.
//  Busca en structFieldOffsets usando el tipo de la expresión base.
//  Como no tenemos TypeChecker integrado en GenCode, hacemos una búsqueda
//  global en todos los structs registrados.

int GenCodeVisitor::getFieldOffset(Exp* base, const string& fieldName) {
    // Intentar por nombre de variable
    if (IdExp* id = dynamic_cast<IdExp*>(base)) {
        // Buscar en todos los structs
        for (auto& [sname, fields] : structFieldOffsets) {
            auto it = fields.find(fieldName);
            if (it != fields.end())
                return it->second;
        }
    }
    // Si no encontramos, buscar en todos los structs
    for (auto& [sname, fields] : structFieldOffsets) {
        auto it = fields.find(fieldName);
        if (it != fields.end())
            return it->second;
    }
    cerr << "[GenCode] Advertencia: campo '" << fieldName << "' no encontrado → offset 0\n";
    return 0;
}