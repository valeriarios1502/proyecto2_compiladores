#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include "ast.h"
#include "visitor.h"
#include <cstdint>

using namespace std;

// ═════════════════════════════════════════════════════════
//   GenCodeVisitor — generación de código x86-64 AT&T
// ═════════════════════════════════════════════════════════
//
//  Convenciones System V AMD64:
//   Caller-saved : %rax %rcx %rdx %rsi %rdi %r8 %r9 %r10 %r11
//   Callee-saved : %rbx %rbp %r12 %r13 %r14 %r15
//   Argumentos   : %rdi %rsi %rdx %rcx %r8 %r9  (en ese orden)
//   Retorno      : %rax
//
//  Invariante: toda expresión deja su resultado en %rax.
//  Para A op B:
//    1. eval(A) → pushq %rax
//    2. eval(B) → %rax
//    3. movq %rax, %rcx  ;  popq %rax
//    4. %rax OP %rcx → %rax
// ═════════════════════════════════════════════════════════

static const char* argRegs[] = {
    "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
};
static const int MAX_REG_ARGS = 6;

// ── Punto de entrada ─────────────────────────────────────

void GenCodeVisitor::gencode(Programa* programa) {
    // 1. Registrar funciones para forward references
    for (Top_dec* d : programa->declist)
        if (Fundec* fd = dynamic_cast<Fundec*>(d))
            funEnv[fd->nombre] = fd;

    // 2. Sección de datos: formatos printf
    cout << ".section .data" << endl;
    cout << "print_i32_fmt:  .string \"%d\\n\""  << endl;
    cout << "print_i64_fmt:  .string \"%ld\\n\"" << endl;
    cout << "print_u64_fmt:  .string \"%lu\\n\"" << endl;
    cout << "print_f32_fmt:  .string \"%f\\n\""  << endl;
    cout << "print_f64_fmt:  .string \"%g\\n\""  << endl;
    cout << "print_str_fmt:  .string \"%s\\n\""  << endl;
    cout << endl;

    // 3. Sección de texto
    cout << ".section .text" << endl;
    cout << ".globl main"    << endl;

    // 4. Generar código de cada declaración
    programa->accept(this);

    // 5. Sección .rodata con literales de string acumulados
    if (!stringLiterals.empty()) {
        cout << endl;
        cout << ".section .rodata" << endl;
        for (auto& p : stringLiterals)
            cout << p.first << ": .string \"" << p.second << "\"" << endl;
    }

    // 6. Marca de stack no ejecutable
    cout << endl;
    cout << ".section .note.GNU-stack,\"\",@progbits" << endl;
}

// ── Programa ─────────────────────────────────────────────

void GenCodeVisitor::visit(Programa* p) {
    for (Top_dec* d : p->declist)
        d->accept(this);
}

// ── Funciones ────────────────────────────────────────────

void GenCodeVisitor::visit(Fundec* fd) {
    if (fd->nombre == "__comptime__") return;

    // Guardar estado del contexto anterior
    string prevFunction  = currentFunction;
    string prevLoopEnd   = currentLoopEnd;
    string prevLoopStart = currentLoopStart;
    auto   prevPosicion  = posicion;
    int    prevContador  = varContador;

    currentFunction  = fd->nombre;
    currentLoopEnd   = "";
    currentLoopStart = "";
    posicion.clear();
    varContador = 1;

    // Registrar parámetros en el entorno (primeros slots del frame)
    for (size_t i = 0; i < fd->id_parametros.size(); ++i)
        posicion[fd->id_parametros[i]] = varContador++;

    const int FRAME_SLOTS = 64;  // 512 bytes — espacio para arrays locales
    int frameBytes = alignFrame(FRAME_SLOTS);

    // ── Prólogo ──────────────────────────────────────────
    cout << endl;
    cout << fd->nombre << ":" << endl;
    cout << "    pushq %rbp"                         << endl;
    cout << "    movq  %rsp, %rbp"                   << endl;
    cout << "    subq  $" << frameBytes << ", %rsp"  << endl;

    // Copiar argumentos de registros al stack
    for (size_t i = 0; i < fd->id_parametros.size() && i < (size_t)MAX_REG_ARGS; ++i)
        cout << "    movq  " << argRegs[i] << ", "
             << offset(fd->id_parametros[i]) << endl;

    // ── Cuerpo ───────────────────────────────────────────
    if (fd->cuerpo) fd->cuerpo->accept(this);

    // ── Epílogo de seguridad ─────────────────────────────
    cout << "    xorq  %rax, %rax" << endl;
    cout << "    leave"            << endl;
    cout << "    ret"              << endl;

    // Restaurar contexto
    currentFunction  = prevFunction;
    currentLoopEnd   = prevLoopEnd;
    currentLoopStart = prevLoopStart;
    posicion         = prevPosicion;
    varContador      = prevContador;
}

// ── Body ─────────────────────────────────────────────────

void GenCodeVisitor::visit(Body* b) {
    for (Stmt* s : b->slist)
        s->accept(this);
}

// ── Declaraciones de variables ────────────────────────────

void GenCodeVisitor::visit(VarDec* vd) {
    // Ignorar declaraciones globales (fuera de función)
    if (currentFunction.empty()) return;

    if (posicion.find(vd->nombre) == posicion.end())
        posicion[vd->nombre] = varContador++;

    if (vd->exp) {
        vd->exp->accept(this);                           // resultado en %rax
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

// ── Statements ───────────────────────────────────────────

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

// ── DerefAssignStmt: *lval = rval ────────────────────────
//
//  Estrategia:
//    1. Evaluar rval → %rax → pushq (valor a almacenar)
//    2. Evaluar dirección del lval → %rax
//       - Si el lval es un UnaryExp DEREF sobre un IdExp, la dirección
//         del puntero ya está en el frame; la cargamos con leaq/movq.
//       - Caso general: evaluamos la sub-expresión (que debe dejar la
//         dirección en %rax) y la movemos a %rcx.
//    3. popq %rdx  (valor a almacenar)
//    4. movq %rdx, (%rcx)  (almacenar en la dirección)

void GenCodeVisitor::visit(DerefAssignStmt* stm) {
    // Evaluar el lado derecho primero y apilarlo
    stm->rval->accept(this);
    cout << "    pushq %rax" << endl;   // valor → stack

    // Obtener la dirección del lval
    // El lval puede ser:
    //   - AlgoconcorchetesExp (array[idx]) → necesitamos la dirección del elemento
    //   - PuntoExp (struct.campo)
    //   - UnaryExp DEREF (*ptr) → la dirección está en el puntero
    //   - IdExp (variable simple) → leaq da su dirección en el frame
    if (AlgoconcorchetesExp* arr = dynamic_cast<AlgoconcorchetesExp*>(stm->lval)) {
        // base + idx*8
        arr->nombre->accept(this);              // base → %rax
        cout << "    pushq %rax" << endl;
        arr->dentroexp->accept(this);           // idx  → %rax
        cout << "    imulq $8, %rax" << endl;   // idx * 8
        cout << "    popq  %rcx"     << endl;   // base → %rcx
        cout << "    addq  %rcx, %rax" << endl; // dirección = base + idx*8
    } else if (UnaryExp* ue = dynamic_cast<UnaryExp*>(stm->lval)) {
        // *ptr = rval  → la dirección es el valor del puntero
        ue->exp->accept(this);                  // valor del puntero → %rax
    } else if (IdExp* id = dynamic_cast<IdExp*>(stm->lval)) {
        // &id → dirección en el frame
        if (posicion.find(id->value) == posicion.end())
            posicion[id->value] = varContador++;
        cout << "    leaq  " << offset(id->value) << ", %rax" << endl;
    } else {
        // Caso general: evaluar el lval (debe dejar dirección en %rax)
        stm->lval->accept(this);
    }

    cout << "    movq  %rax, %rcx"  << endl;   // dirección → %rcx
    cout << "    popq  %rdx"        << endl;   // valor     → %rdx
    cout << "    movq  %rdx, (%rcx)" << endl;  // *dirección = valor
}

// ── PrintStmt ────────────────────────────────────────────
//
//  Detectamos si la expresión es un StringExp para usar %s, y en caso
//  contrario usamos %ld (entero de 64 bits).
//
//  Alineación: salvamos %rsp en %r12 (callee-saved) antes de printf.

void GenCodeVisitor::visit(PrintStmt* stm) {
    bool esString = dynamic_cast<StringExp*>(stm->exp) != nullptr;

    stm->exp->accept(this);                             // valor en %rax

    cout << "    movq  %rsp, %r12"                     << endl;
    cout << "    andq  $-16, %rsp"                     << endl;
    cout << "    movq  %rax, %rsi"                     << endl;

    if (esString)
        cout << "    leaq  print_str_fmt(%rip), %rdi"  << endl;
    else
        cout << "    leaq  print_i64_fmt(%rip), %rdi"  << endl;

    cout << "    xorq  %rax, %rax"                     << endl;
    cout << "    call  printf@PLT"                     << endl;
    cout << "    movq  %r12, %rsp"                     << endl;
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
    cout << "    cmpq  $0, %rax"      << endl;
    cout << "    je    " << labelEnd  << endl;

    for (Stmt* s : stm->cuerpodelwhile) s->accept(this);

    cout << "    jmp   " << labelStart << endl;
    cout << labelEnd << ":"            << endl;

    currentLoopStart = prevStart;
    currentLoopEnd   = prevEnd;
}

// ── ForStmt ──────────────────────────────────────────────
//
//  El parser genera ForStmt con:
//    asignacion = AsignStmt(var, iterable)   ← iterable es el array/rango
//    condicion  = NullExp()                  ← placeholder
//    incremento = AsignStmt(idx, NullExp())  ← placeholder
//    cuerpo     = Body
//
//  Para un for real sobre un rango 0..N necesitamos saber N.
//  Dado que el parser usa NullExp como condición, el for clásico
//  con variable de iteración y condición explícita se maneja aquí:
//
//  Si la condición es NullExp asumimos que el iterable es el array
//  y el var es el elemento (for each semántico). En ese caso generamos
//  un bucle infinito que el usuario debe romper con break.
//  Si la condición NO es NullExp, generamos un for clásico.

void GenCodeVisitor::visit(ForStmt* stm) {
    // El parser genera ForStmt para  for (iterable) |elem|  y  for (iterable) |elem, idx|
    // de esta forma:
    //   asignacion = AsignStmt(elem, iterable)
    //   condicion  = NullExp()      ← placeholder (no es un for clásico con condición)
    //   incremento = AsignStmt(idx o __idx__, NullExp())
    //   cuerpo     = Body
    //
    // Detectamos si es un for-each o un for clásico:
    //   - for-each:  condicion == NullExp  (viene del parser de for)
    //   - for clásico: condicion != NullExp

    bool esForEach = (dynamic_cast<NullExp*>(stm->condicion) != nullptr);

    string labelStart = newLabel("for_start");
    string labelEnd   = newLabel("for_end");

    string prevStart = currentLoopStart;
    string prevEnd   = currentLoopEnd;
    currentLoopStart = labelStart;
    currentLoopEnd   = labelEnd;

    if (esForEach) {
        // ── For-each: for (arr) |elem|  o  for (arr) |elem, idx| ────────
        //
        // Extraemos nombres de elem e idx del AST
        AsignStmt* initStmt = dynamic_cast<AsignStmt*>(stm->asignacion);
        AsignStmt* idxStmt  = dynamic_cast<AsignStmt*>(stm->incremento);

        string elemVar = initStmt ? initStmt->variable : "__elem__";
        string idxVar  = idxStmt  ? idxStmt->variable  : "__idx__";

        // Registrar elem e idx en posicion si no existen
        if (posicion.find(elemVar) == posicion.end())
            posicion[elemVar] = varContador++;
        if (posicion.find(idxVar) == posicion.end())
            posicion[idxVar] = varContador++;

        // Registrar variable interna __arr_base__ para guardar la base del array
        string baseVar = newLabel("__arr_base__");
        string lenVar  = newLabel("__arr_len__");
        posicion[baseVar] = varContador++;
        posicion[lenVar]  = varContador++;

        // Evaluar el iterable (el array) → %rax = dirección base
        // El iterable está en initStmt->exp (p.ej. IdExp("arr"))
        if (initStmt && initStmt->exp) {
            // Si arr es una variable declarada, cargamos su dirección (leaq)
            // Si no está declarada, avisamos y usamos 0
            if (IdExp* id = dynamic_cast<IdExp*>(initStmt->exp)) {
                if (posicion.find(id->value) != posicion.end()) {
                    // arr es una variable en el frame: su valor ES la dirección base
                    cout << "    movq  " << offset(id->value) << ", %rax" << endl;
                } else {
                    cerr << "[GenCode] Advertencia: iterable no declarado: '"
                         << id->value << "' — se usará 0" << endl;
                    cout << "    xorq  %rax, %rax" << endl;
                }
            } else {
                initStmt->exp->accept(this);
            }
        } else {
            cout << "    xorq  %rax, %rax" << endl;
        }
        // Guardar base del array en __arr_base__
        cout << "    movq  %rax, " << offset(baseVar) << endl;

        // Inicializar idx = 0
        cout << "    movq  $0, %rax" << endl;
        cout << "    movq  %rax, " << offset(idxVar) << endl;

        // NOTA: sin información de longitud en tiempo de compilación,
        // generamos un bucle infinito (el usuario usa break para salir,
        // o bien el array tiene un centinela). Si el array viene de
        // malloc con tamaño conocido, habría que pasarlo explícitamente.
        // Por ahora el bucle corre hasta break o hasta que se salga del scope.
        cout << labelStart << ":" << endl;

        // Cargar elem = arr[idx]
        cout << "    movq  " << offset(idxVar)  << ", %rax" << endl;  // idx
        cout << "    movq  " << offset(baseVar) << ", %rcx" << endl;  // base
        cout << "    movq  (%rcx,%rax,8), %rax" << endl;                // arr[idx]
        cout << "    movq  %rax, " << offset(elemVar) << endl;           // elem = arr[idx]

        // Ejecutar cuerpo
        if (stm->cuerpo) stm->cuerpo->accept(this);

        // idx = idx + 1
        cout << "    movq  " << offset(idxVar) << ", %rax" << endl;
        cout << "    addq  $1, %rax" << endl;
        cout << "    movq  %rax, " << offset(idxVar) << endl;

        cout << "    jmp   " << labelStart << endl;
        cout << labelEnd << ":" << endl;

    } else {
        // ── For clásico: for (init; cond; inc) ───────────────────────────
        if (stm->asignacion) stm->asignacion->accept(this);

        cout << labelStart << ":" << endl;

        stm->condicion->accept(this);
        cout << "    cmpq  $0, %rax"     << endl;
        cout << "    je    " << labelEnd  << endl;

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

// ── SwitchStmt ───────────────────────────────────────────
//
//  Patrón:
//    eval(condicion) → pushq %rax        ; valor del switch en stack
//    para cada caso:
//      eval(patron)  → %rax
//      movq %rax, %rcx                   ; patrón → %rcx
//      movq 0(%rsp), %rax                ; peek valor sin modificar stack
//      cmpq %rcx, %rax
//      jne  siguiente_caso
//      <body>
//      addq $8, %rsp                     ; limpiar valor del switch
//      jmp  switch_end
//    default (si existe)
//    addq $8, %rsp                       ; limpiar valor del switch
//    switch_end:

void GenCodeVisitor::visit(SwitchStmt* stm) {
    string labelEnd = newLabel("switch_end");

    stm->condicion->accept(this);
    cout << "    pushq %rax" << endl;              // valor del switch en stack

    for (auto& caso : stm->casos) {
        string labelNext = newLabel("switch_next");

        caso.first->accept(this);                  // patrón → %rax
        cout << "    movq  %rax, %rcx"    << endl; // patrón → %rcx
        cout << "    movq  0(%rsp), %rax" << endl; // peek valor del switch → %rax (sin popq)
        cout << "    cmpq  %rcx, %rax"   << endl;  // left=valor, right=patrón
        cout << "    jne   " << labelNext << endl;

        caso.second->accept(this);
        cout << "    addq  $8, %rsp"     << endl;  // limpiar stack
        cout << "    jmp   " << labelEnd << endl;

        cout << labelNext << ":"         << endl;
    }

    // Default
    if (stm->default_caso)
        stm->default_caso->accept(this);

    cout << "    addq  $8, %rsp" << endl;          // limpiar valor del switch
    cout << labelEnd << ":"      << endl;
}

void GenCodeVisitor::visit(BodyStmt* stm) {
    if (stm->cuerpo) stm->cuerpo->accept(this);
}

// ── DeleteStm ────────────────────────────────────────────

void GenCodeVisitor::visit(DeleteStm* stm) {
    stm->exp->accept(this);
    cout << "    movq  %rax, %rdi" << endl;
    emitAlignedCall("free@PLT");
}

// ── DeferStmt / TryStmt — stubs ──────────────────────────

void GenCodeVisitor::visit(DeferStmt* stm) {
    // defer necesita cleanup al salir del scope; stub intencional.
    (void)stm;
}

void GenCodeVisitor::visit(TryStmt* stm) {
    if (stm->try_body) stm->try_body->accept(this);
}

// ── emitAlignedCall (método miembro) ─────────────────────
//
//  Alinea %rsp a 16 bytes antes del call y restaura después.
//  Usamos %r12 (callee-saved) para guardar el %rsp original.
//  NOTA: no salvamos/restauramos %r12 aquí; se asume que cada función
//  generada tiene su propio frame y %r12 no es usado por el compilador
//  para otra cosa en el mismo scope.

void GenCodeVisitor::emitAlignedCall(const string& target) {
    cout << "    movq  %rsp, %r12"    << endl;
    cout << "    andq  $-16, %rsp"    << endl;
    cout << "    xorq  %rax, %rax"    << endl;  // 0 regs XMM (varargs)
    cout << "    call  " << target    << endl;
    cout << "    movq  %r12, %rsp"    << endl;
}

// ═════════════════════════════════════════════════════════
//   Expresiones
// ═════════════════════════════════════════════════════════

Value GenCodeVisitor::visit(NumberExpDecimal* exp) {
    cout << "    movq  $" << exp->value << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(NumberExpFlotante* exp) {
    float   f32  = exp->value;
    double  f64  = (double)f32;
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

Value GenCodeVisitor::visit(IdExp* exp) {
    // Manejo de error.X como constante entera única
    if (exp->value.size() >= 6 && exp->value.substr(0, 6) == "error.") {
        if (errorCodes.find(exp->value) == errorCodes.end())
            errorCodes[exp->value] = (int)errorCodes.size() + 1;
        cout << "    movq  $" << errorCodes[exp->value] << ", %rax" << endl;
        return Value();
    }

    if (posicion.find(exp->value) == posicion.end()) {
        // Variable no declarada: emitir 0 y advertencia (no crashear)
        cerr << "[GenCode] Advertencia: variable no declarada: '" << exp->value
             << "' — se usará 0" << endl;
        cout << "    xorq  %rax, %rax" << endl;
        return Value();
    }
    cout << "    movq  " << offset(exp->value) << ", %rax" << endl;
    return Value();
}

// ── BinaryExp ────────────────────────────────────────────
//
//  Después de eval: %rax = left, %rcx = right
//  AT&T: cmpq src, dst  →  flags para dst − src
//  Entonces cmpq %rcx, %rax  fija flags para left − right:
//    setl  → left <  right  ✓
//    setle → left <= right  ✓
//    setg  → left >  right  ✓
//    setge → left >= right  ✓
//    sete  → left == right  ✓
//    setne → left != right  ✓

Value GenCodeVisitor::visit(BinaryExp* exp) {
    exp->left->accept(this);
    cout << "    pushq %rax"        << endl;   // salvar left
    exp->right->accept(this);
    cout << "    movq  %rax, %rcx" << endl;   // right → %rcx
    cout << "    popq  %rax"       << endl;   // left  → %rax

    switch (exp->op) {
        case PLUS_OP:
            cout << "    addq  %rcx, %rax" << endl;
            break;

        case MINUS_OP:
            cout << "    subq  %rcx, %rax" << endl;
            break;

        case MUL_OP:
            cout << "    imulq %rcx, %rax" << endl;
            break;

        case DIV_OP:
            cout << "    cqto"             << endl;
            cout << "    idivq %rcx"       << endl;
            break;

        case MODULO_OP:
            cout << "    cqto"             << endl;
            cout << "    idivq %rcx"       << endl;
            cout << "    movq  %rdx, %rax" << endl;  // resto en %rdx
            break;

        case MENOR:
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setl  %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case MENORI:
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setle %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case MAYOR:
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setg  %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case MAYORI:
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setge %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case IGUALIGUAL:
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    sete  %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case DIFERENTE_OP:
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setne %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case AND:
            // &&: true si left != 0 Y right != 0
            cout << "    testq %rax, %rax"  << endl;
            cout << "    setne %r10b"        << endl;
            cout << "    testq %rcx, %rcx"  << endl;
            cout << "    setne %r11b"        << endl;
            cout << "    andb  %r11b, %r10b" << endl;
            cout << "    movzbq %r10b, %rax" << endl;
            break;

        case OR:
            // ||: true si left != 0 O right != 0
            cout << "    orq   %rcx, %rax"  << endl;
            cout << "    setne %al"          << endl;
            cout << "    movzbq %al, %rax"   << endl;
            break;

        case REFERENCIA:  // & bitwise AND
            cout << "    andq  %rcx, %rax"  << endl;
            break;

        default:
            cerr << "[GenCode] Operador binario no soportado (op=" << exp->op << ")" << endl;
            break;
    }
    return Value();
}

Value GenCodeVisitor::visit(NotExp* exp) {
    exp->exp->accept(this);
    cout << "    testq %rax, %rax" << endl;
    cout << "    sete  %al"         << endl;
    cout << "    movzbq %al, %rax"  << endl;
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
            cout << "    testq %rax, %rax" << endl;
            cout << "    sete  %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case UnaryExp::ADDRESS:
            // &var: cargamos la dirección efectiva en el frame
            if (IdExp* id = dynamic_cast<IdExp*>(exp->exp)) {
                if (posicion.find(id->value) == posicion.end())
                    posicion[id->value] = varContador++;
                cout << "    leaq  " << offset(id->value) << ", %rax" << endl;
            } else {
                // Para arrays u otras expresiones, evaluar y asumir que
                // la evaluación ya deja la dirección base en %rax
                exp->exp->accept(this);
            }
            break;

        case UnaryExp::DEREF:
            // *ptr: desreferenciar
            exp->exp->accept(this);
            cout << "    movq  (%rax), %rax" << endl;
            break;
    }
    return Value();
}

// ── FcallExp ─────────────────────────────────────────────
//
//  Convención System V AMD64:
//    Args 1..6  → %rdi %rsi %rdx %rcx %r8 %r9
//    Args 7+    → stack en orden inverso
//
//  Algoritmo:
//    1. Evaluar args de derecha a izquierda → apilar todos
//    2. Mover primeros 6 del stack a registros
//    3. emitAlignedCall (alinea %rsp, llama, restaura %rsp)
//    4. Limpiar args 7+ del stack

Value GenCodeVisitor::visit(FcallExp* exp) {
    // Funciones especiales internas
    if (exp->nombre == "__try__" || exp->nombre == "__catch__") {
        if (!exp->argumentos.empty())
            exp->argumentos[0]->accept(this);
        return Value();
    }

    int nArgs      = (int)exp->argumentos.size();
    int nRegArgs   = min(nArgs, MAX_REG_ARGS);
    int nStackArgs = max(0, nArgs - MAX_REG_ARGS);

    // Paso 1: evaluar todos los args de derecha a izquierda y apilar
    for (int i = nArgs - 1; i >= 0; --i) {
        exp->argumentos[i]->accept(this);
        cout << "    pushq %rax" << endl;
    }

    // Paso 2: mover primeros nRegArgs del stack a registros de argumento
    for (int i = 0; i < nRegArgs; ++i)
        cout << "    popq  " << argRegs[i] << endl;

    // Paso 3: llamada alineada (los nStackArgs restantes ya están en stack)
    emitAlignedCall(exp->nombre);

    // Paso 4: limpiar args que quedaron en el stack
    if (nStackArgs > 0)
        cout << "    addq  $" << (nStackArgs * 8) << ", %rsp" << endl;

    return Value();
}

Value GenCodeVisitor::visit(NewExp* exp) {
    // malloc(8): aloca 8 bytes en el heap
    cout << "    movq  $8, %rdi" << endl;
    emitAlignedCall("malloc@PLT");
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

// ── StringExp ────────────────────────────────────────────
//
//  Acumulamos literales en stringLiterals y emitimos en .rodata al final.
//  Aquí solo generamos leaq para cargar la dirección.

Value GenCodeVisitor::visit(StringExp* exp) {
    string lbl = internString(exp->valor);
    cout << "    leaq  " << lbl << "(%rip), %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(CharExp* exp) {
    cout << "    movq  $" << (int)(unsigned char)exp->valor << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(ReferenceExp* exp) {
    if (exp->exp) {
        if (IdExp* id = dynamic_cast<IdExp*>(exp->exp)) {
            if (posicion.find(id->value) == posicion.end())
                posicion[id->value] = varContador++;
            cout << "    leaq  " << offset(id->value) << ", %rax" << endl;
        } else {
            exp->exp->accept(this);
        }
    }
    return Value();
}

Value GenCodeVisitor::visit(PunteroExp* exp) {
    if (exp->exp) {
        exp->exp->accept(this);
        cout << "    movq  (%rax), %rax" << endl;
    }
    return Value();
}

// ── PuntoExp: acceso a campo de struct ───────────────────
//
//  Sin tabla de tipos no conocemos el offset de cada campo.
//  Generamos la base y dejamos un TODO visible en el output.
//  Para structs simples con campos de 8 bytes, el primer campo está
//  en offset 0, el segundo en +8, etc.

Value GenCodeVisitor::visit(PuntoExp* exp) {
    if (exp->id == "__unwrap__") {
        // optional.? — por ahora devolvemos el valor tal cual
        exp->exp->accept(this);
        return Value();
    }
    // Evaluamos la base (dirección del struct)
    exp->exp->accept(this);
    // Sin tabla de structs no podemos calcular el offset real.
    // Devolvemos el primer campo (offset 0) como aproximación.
    // TODO: implementar tabla de structs para offsets correctos.
    cout << "    movq  0(%rax), %rax" << endl;
    return Value();
}

// ── AlgoconcorchetesExp: array[index] ────────────────────
//
//  Corrección del bug original: la base iba a %rcx pero el modo de
//  direccionamiento indexed es (%base, %índice, escala), donde:
//    %rcx = base (dirección del array)
//    %rax = índice
//
//  Secuencia correcta:
//    1. eval(nombre/base) → %rax → pushq    (dirección del array)
//    2. eval(dentroexp/índice) → %rax
//    3. popq %rcx                            (base → %rcx)
//    4. movq (%rcx, %rax, 8), %rax          (elemento → %rax)

Value GenCodeVisitor::visit(AlgoconcorchetesExp* exp) {
    exp->nombre->accept(this);                          // base → %rax
    cout << "    pushq %rax"                 << endl;   // salvar base
    exp->dentroexp->accept(this);                       // índice → %rax
    cout << "    popq  %rcx"                 << endl;   // base → %rcx
    cout << "    movq  (%rcx,%rax,8), %rax" << endl;   // elemento → %rax
    return Value();
}

Value GenCodeVisitor::visit(AlgoconcorchetesylistaExp* exp) {
    // Array 2D u otro acceso multi-índice: stub
    exp->nombre->accept(this);
    return Value();
}

Value GenCodeVisitor::visit(LambdaExp* exp) {
    // Lambdas con captura necesitan closures; stub retorna 0
    cout << "    xorq  %rax, %rax" << endl;
    return Value();
}

// ── Tipos — stubs ─────────────────────────────────────────

void GenCodeVisitor::visit(Template* t) {
    // Los templates se instancian como funciones concretas ignorando el
    // parámetro de tipo (T se trata como i64 implícitamente).
    // El nombre de la función es t->id1, los parámetros reales son
    // t->id_parametros / t->tipo_parametros, el cuerpo es t->block.
 
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
 
    // Registrar parámetros (ignoramos el comptime T, solo los parámetros reales)
    for (size_t i = 0; i < t->id_parametros.size(); ++i)
        posicion[t->id_parametros[i]] = varContador++;
 
    const int FRAME_SLOTS = 64;
    int frameBytes = alignFrame(FRAME_SLOTS);
 
    cout << endl;
    cout << t->id1 << ":" << endl;
    cout << "    pushq %rbp"                        << endl;
    cout << "    movq  %rsp, %rbp"                  << endl;
    cout << "    subq  $" << frameBytes << ", %rsp" << endl;
 
    // Copiar argumentos de registros al stack
    for (size_t i = 0; i < t->id_parametros.size() && i < (size_t)MAX_REG_ARGS; ++i)
        cout << "    movq  " << argRegs[i] << ", "
             << offset(t->id_parametros[i]) << endl;
 
    if (t->block) t->block->accept(this);
 
    // Epílogo de seguridad
    cout << "    xorq  %rax, %rax" << endl;
    cout << "    leave"            << endl;
    cout << "    ret"              << endl;
 
    currentFunction  = prevFunction;
    currentLoopEnd   = prevLoopEnd;
    currentLoopStart = prevLoopStart;
    posicion         = prevPosicion;
    varContador      = prevContador;
}

void GenCodeVisitor::visit(Structdec*   sd) { }

void GenCodeVisitor::visit(IdType*      t)  { }
void GenCodeVisitor::visit(PointerType* t)  { }
void GenCodeVisitor::visit(ArrayType*   t)  { }
void GenCodeVisitor::visit(OptionalType*t)  { }
void GenCodeVisitor::visit(ErrorType*   t)  { }
void GenCodeVisitor::visit(UnionType*   t)  { }
void GenCodeVisitor::visit(EnumType*    t)  { }