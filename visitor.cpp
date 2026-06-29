#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>   // memcpy
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
//   Retorno      : %rax  (entero/puntero), %xmm0 (flotante — aquí no usado)
//   Red-zone     : 128 bytes por debajo de %rsp son preservados por señales;
//                  no necesitamos ajustar %rsp para leaf-functions, pero sí
//                  para funciones que hacen call (necesitamos %rsp alineado a 16).
//
//  Invariante de este generador:
//   Toda expresión deja su resultado (64 bits) en %rax.
//   Para operar dos sub-expresiones A op B:
//     1. eval(A) → %rax ; pushq %rax        (guarda A)
//     2. eval(B) → %rax                     (B queda en %rax)
//     3. movq %rax, %rcx                    (right = B en %rcx)
//        popq  %rax                         (left  = A en %rax)
//     4. %rax OP %rcx → %rax               (resultado en %rax)
//
//  Registros de argumentos (System V AMD64)
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

    // 2. Primera pasada: recolectar string literals
    //    Los emitimos en .rodata antes de .text para no fragmentar secciones.
    //    (la recolección real ocurre durante visit(StringExp*); aquí solo
    //     reservamos la sección y la llenamos al final)

    // 3. Sección de datos: formatos printf
    cout << ".section .data" << endl;
    cout << "print_i32_fmt:  .string \"%d\\n\""  << endl;  // i32  / int
    cout << "print_i64_fmt:  .string \"%ld\\n\"" << endl;  // i64  / long
    cout << "print_u64_fmt:  .string \"%lu\\n\"" << endl;  // u64  (sin signo)
    cout << "print_f32_fmt:  .string \"%f\\n\""  << endl;  // f32  / float
    cout << "print_f64_fmt:  .string \"%g\\n\""  << endl;  // f64  / double
    cout << "print_str_fmt:  .string \"%s\\n\""  << endl;  // string / *char
    cout << endl;

    // 4. Sección de texto
    cout << ".section .text" << endl;
    cout << ".globl main"    << endl;

    // 5. Generar código de cada declaración
    programa->accept(this);

    // 6. Sección .rodata con los literales de string acumulados
    if (!stringLiterals.empty()) {
        cout << endl;
        cout << ".section .rodata" << endl;
        for (auto it = stringLiterals.begin(); it != stringLiterals.end(); ++it)
    cout << it->first << ": .string \"" << it->second << "\"" << endl;
    }

    // 7. Marca de stack no ejecutable (Linux/GAS)
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
    if (fd->nombre == "__comptime__") return; // bloques comptime: ignorar

    // Guardar estado del contexto anterior (soporte para funciones anidadas)
    string prevFunction   = currentFunction;
    string prevLoopEnd    = currentLoopEnd;
    string prevLoopStart  = currentLoopStart;
    auto   prevPosicion   = posicion;
    int    prevContador   = varContador;

    currentFunction  = fd->nombre;
    currentLoopEnd   = "";
    currentLoopStart = "";
    posicion.clear();
    varContador = 1;

    // ── Registrar parámetros en el entorno de posiciones ──
    // Ocupan los primeros slots del frame; se copian desde los registros
    // de argumento al stack en el prólogo.
    for (size_t i = 0; i < fd->id_parametros.size(); ++i)
        posicion[fd->id_parametros[i]] = varContador++;

    // ── Estimar slots necesarios para alinear el frame ──
    // 32 slots = 256 bytes mínimo (conservador; suficiente para la mayoría
    // de funciones de un compilador de curso).
    const int FRAME_SLOTS = 32;
    int frameBytes = alignFrame(FRAME_SLOTS); // siempre múltiplo de 16

    // ── Prólogo estándar ─────────────────────────────────
    //
    //  Análisis de alineación:
    //    Antes de entrar a esta función el caller hizo "call f" que apila la
    //    dirección de retorno (8 bytes). Por convención System V AMD64, %rsp
    //    estaba alineado a 16 en el punto de "call", así que al entrar aquí
    //    %rsp % 16 == 8.
    //    pushq %rbp  →  %rsp -= 8  →  %rsp % 16 == 0
    //    subq  $N    →  %rsp -= N  →  si N % 16 == 0, %rsp % 16 == 0  ✓
    //
    //  Caso especial: main()
    //    El loader llama a __libc_start_main que llama a main.
    //    Al entrar a main, %rsp % 16 == 8  (igual que cualquier otra función).
    //    La secuencia pushq+subq deja %rsp % 16 == 0  ✓
    //
    //  Para blindarse ante cualquier desalineación residual (p.ej. si algún
    //  pushq en el cuerpo no fue compensado con un popq), añadimos
    //  "andq $-16, %rsp" justo antes de cada call en PrintStmt y FcallExp.
    cout << endl;
    cout << fd->nombre << ":" << endl;
    cout << "    pushq %rbp"                        << endl;  // salvar frame anterior
    cout << "    movq  %rsp, %rbp"                  << endl;  // establecer frame base
    cout << "    subq  $" << frameBytes << ", %rsp"  << endl;  // reservar espacio local
    // frameBytes es múltiplo de 16: tras pushq(%rsp%16==0) - frameBytes(%16==0) = 0 ✓

    // Copiar argumentos de registros al stack (máx. 6 por ABI)
    for (size_t i = 0; i < fd->id_parametros.size() && i < (size_t)MAX_REG_ARGS; ++i)
        cout << "    movq  " << argRegs[i] << ", "
             << offset(fd->id_parametros[i]) << endl;

    // ── Cuerpo ───────────────────────────────────────────
    if (fd->cuerpo) fd->cuerpo->accept(this);

    // ── Epílogo de seguridad ─────────────────────────────
    // Si el control llega aquí (sin return explícito), retorna 0.
    cout << "    xorq  %rax, %rax" << endl;
    cout << "    leave"            << endl;  // movq %rbp,%rsp ; popq %rbp
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
    if (currentFunction.empty()) return; // ← AGREGA ESTA LÍNEA
    
    if (posicion.find(vd->nombre) == posicion.end())
        posicion[vd->nombre] = varContador++;
    if (vd->exp) {
        vd->exp->accept(this);
        cout << "    movq  %rax, " << offset(vd->nombre) << endl;
    }
}

void GenCodeVisitor::visit(ConstDec* cd) {
    if (posicion.find(cd->nombre) == posicion.end())
        posicion[cd->nombre] = varContador++;

    if (cd->exp) {
        cd->exp->accept(this);                          // resultado en %rax
        cout << "    movq  %rax, " << offset(cd->nombre) << endl;
    }
}

// ── Statements ───────────────────────────────────────────

void GenCodeVisitor::visit(AsignStmt* stm) {
    if (stm->variable == "__call__") {
        // Llamada usada como sentencia; el resultado en %rax se descarta.
        if (stm->exp) stm->exp->accept(this);
        return;
    }

    if (posicion.find(stm->variable) == posicion.end())
        posicion[stm->variable] = varContador++;

    stm->exp->accept(this);                             // resultado en %rax
    cout << "    movq  %rax, " << offset(stm->variable) << endl;
}

// ── PrintStmt ────────────────────────────────────────────
//
//  Usamos siempre print_i64_fmt (%ld) ya que todas las variables del
//  generador son 64 bits. Un compilador con tabla de tipos elegiría el
//  formato correcto por tipo.
//
//  Alineación de %rsp:
//    Antes de "call printf@PLT" el ABI System V AMD64 exige %rsp % 16 == 0.
//    Salvamos el %rsp real en %r12 (callee-saved → printf no lo toca),
//    lo alineamos con "andq $-16, %rsp" y lo restauramos tras el call.

void GenCodeVisitor::visit(PrintStmt* stm) {
    stm->exp->accept(this);                             // valor en %rax

    // Salvar %rsp en %r12 (callee-saved: printf no lo modifica)
    // y alinear %rsp a 16 bytes antes del call.
    // "andq $-16, %rsp" pone a cero los últimos 4 bits → múltiplo de 16.
    cout << "    movq  %rsp, %r12"                     << endl; // salvar rsp real
    cout << "    andq  $-16, %rsp"                     << endl; // alinear a 16

    cout << "    movq  %rax, %rsi"                     << endl; // arg2: valor
    cout << "    leaq  print_i64_fmt(%rip), %rdi"      << endl; // arg1: formato
    cout << "    xorq  %rax, %rax"                     << endl; // varargs: 0 regs XMM
    cout << "    call  printf@PLT"                     << endl;

    cout << "    movq  %r12, %rsp"                     << endl; // restaurar rsp
}

void GenCodeVisitor::visit(ReturnStm* stm) {
    if (stm->exp)
        stm->exp->accept(this);                         // resultado en %rax
    else
        cout << "    xorq  %rax, %rax" << endl;         // return void/0

    cout << "    leave" << endl;
    cout << "    ret"   << endl;
}

void GenCodeVisitor::visit(IfStmt* stm) {
    string labelElse = newLabel("else");
    string labelEnd  = newLabel("endif");

    // Evaluar condición → %rax
    stm->condicion->accept(this);
    cout << "    cmpq  $0, %rax"        << endl;  // testq %rax,%rax también válido
    cout << "    je    " << labelElse   << endl;  // si falso, saltar a else

    // Rama then
    stm->cuerpodelif->accept(this);
    cout << "    jmp   " << labelEnd    << endl;

    // Rama else
    cout << labelElse << ":" << endl;
    if (stm->hayelse && stm->cuerpodelelse)
        stm->cuerpodelelse->accept(this);

    cout << labelEnd << ":" << endl;
}

void GenCodeVisitor::visit(WhileStmt* stm) {
    string labelStart = newLabel("while_start");
    string labelEnd   = newLabel("while_end");

    // Guardar contexto del bucle externo (soporte para break/continue anidados)
    string prevStart = currentLoopStart;
    string prevEnd   = currentLoopEnd;
    currentLoopStart = labelStart;
    currentLoopEnd   = labelEnd;

    cout << labelStart << ":" << endl;

    stm->condicion->accept(this);                       // condición en %rax
    cout << "    cmpq  $0, %rax"        << endl;
    cout << "    je    " << labelEnd    << endl;

    for (Stmt* s : stm->cuerpodelwhile) s->accept(this);

    cout << "    jmp   " << labelStart  << endl;
    cout << labelEnd << ":"             << endl;

    currentLoopStart = prevStart;
    currentLoopEnd   = prevEnd;
}

void GenCodeVisitor::visit(ForStmt* stm) {
    string labelStart = newLabel("for_start");
    string labelEnd   = newLabel("for_end");

    string prevStart = currentLoopStart;
    string prevEnd   = currentLoopEnd;
    currentLoopStart = labelStart;
    currentLoopEnd   = labelEnd;

    // Inicialización (p.ej. i = 0)
    if (stm->asignacion) stm->asignacion->accept(this);

    cout << labelStart << ":" << endl;

    // Condición
    if (stm->condicion) {
        stm->condicion->accept(this);                   // condición en %rax
        cout << "    cmpq  $0, %rax"       << endl;
        cout << "    je    " << labelEnd   << endl;
    }

    // Cuerpo
    if (stm->cuerpo) stm->cuerpo->accept(this);

    // Incremento (p.ej. i = i + 1)
    if (stm->incremento) stm->incremento->accept(this);

    cout << "    jmp   " << labelStart << endl;
    cout << labelEnd << ":"            << endl;

    currentLoopStart = prevStart;
    currentLoopEnd   = prevEnd;
}

void GenCodeVisitor::visit(BreakStmt* stm) {
    // Si tiene valor (break :label valor) lo evaluamos pero lo descartamos
    // por ahora (soporte completo requeriría retornar el valor del bloque).
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
//  Patrón generado:
//    eval(condicion) → pushq %rax       ; valor del switch en stack
//    para cada caso patron => body:
//      eval(patron) → %rcx
//      popq %rax  ; valor del switch
//      pushq %rax ; volver a poner (para el siguiente caso)
//      cmpq %rcx, %rax           ; AT&T: compara %rax − %rcx
//      jne  siguiente_caso
//      <body>
//      jmp  switch_end
//      siguiente_caso:
//    default body (si existe)
//    popq %rcx   ; limpiar el valor del switch
//    switch_end:

void GenCodeVisitor::visit(SwitchStmt* stm) {
    string labelEnd = newLabel("switch_end");

    stm->condicion->accept(this);                      // valor → %rax
    cout << "    pushq %rax" << endl;                  // apilar valor del switch

   for (auto it = stm->casos.begin(); it != stm->casos.end(); ++it) {
        string labelNext = newLabel("switch_next");

        it->first->accept(this);                          // patrón → %rax
        cout << "    movq  %rax, %rcx"   << endl;     // patrón → %rcx
        cout << "    popq  %rax"         << endl;     // valor del switch → %rax
        cout << "    pushq %rax"         << endl;     // volver a apilar

        // AT&T: cmpq src, dst  ≡  dst − src.  Aquí: %rax − %rcx
        cout << "    cmpq  %rcx, %rax"   << endl;
        cout << "    jne   " << labelNext << endl;

        it->second->accept(this);
        cout << "    popq  %rcx"         << endl;     // limpiar stack
        cout << "    jmp   " << labelEnd << endl;

        cout << labelNext << ":"         << endl;
    }

    // Default
    if (stm->default_caso) {
        stm->default_caso->accept(this);
    }

    cout << "    popq  %rcx"  << endl;                 // limpiar valor del switch
    cout << labelEnd << ":"   << endl;
}

void GenCodeVisitor::visit(BodyStmt* stm) {
    if (stm->cuerpo) stm->cuerpo->accept(this);
}

// ── DeleteStm ────────────────────────────────────────────
//  free(ptr):  ptr en %rdi, luego call free@PLT

void GenCodeVisitor::visit(DeleteStm* stm) {
    stm->exp->accept(this);                            // puntero → %rax
    cout << "    movq  %rax, %rdi"  << endl;           // arg1: puntero a liberar
    emitAlignedCall("free@PLT");
    // %rax queda indefinido tras free; es un statement, no importa.
}

// ── DeferStmt / TryStmt — stubs ──────────────────────────

void GenCodeVisitor::visit(DeferStmt* stm) {
    // defer debería ejecutar el stmt al salir del scope/función.
    // Implementación correcta requiere una pila de cleanup en runtime
    // (o instrucciones al final de cada bloque de salida).
    // Para este compilador de curso lo ignoramos (no ejecutamos nada)
    // para no afectar el flujo de control: ejecutarlo aquí sería semánticamente
    // incorrecto (p.ej. defer delete p lo liberaría al inicio, no al final).
    (void)stm; // stub intencional
}

void GenCodeVisitor::emitAlignedCall(const string& target) {
    // Alinear stack a 16 bytes antes del call (convención AMD64)
    cout << "    andq  $-16, %rsp"  << endl;
    cout << "    call  " << target  << endl;
}

void GenCodeVisitor::visit(TryStmt* stm) {
    // try/catch real necesita setjmp/longjmp o tablas de unwinding.
    // Aquí simplemente ejecutamos el cuerpo try.
    if (stm->try_body)   stm->try_body->accept(this);
    // Ignoramos catch_body (no hay mecanismo de excepción).
}

// ═════════════════════════════════════════════════════════
//   Expresiones
// ═════════════════════════════════════════════════════════

Value GenCodeVisitor::visit(NumberExpDecimal* exp) {
    // Constante entera de 64 bits con signo extendido.
    // movq $imm, %rax  carga inmediato de 64 bits (en práctica GAS acepta
    // inmediatos de 32 bits en movq; para valores grandes usar movabsq).
    cout << "    movq  $" << exp->value << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(NumberExpFlotante* exp) {
    // Reinterpretamos los bits IEEE 754 del float como un entero de 64 bits
    // y los cargamos en %rax.  Esto NO es un valor usable como flotante en
    // registros XMM, pero es lo correcto para un generador de enteros de curso
    // que no implementa soporte FP completo.
    //
    // Para uso real: mover a %xmm0 con movsd / cvtss2sd.
    float  f32  = exp->value;
    double f64  = (double)f32;    // promover a double
    int64_t bits = 0;
    memcpy(&bits, &f64, sizeof(bits));  // reinterpretar bits (no truncar)
    cout << "    movabsq $" << bits << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(BoolExp* exp) {
    int val = (exp->booleano == "true") ? 1 : 0;
    cout << "    movq  $" << val << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(IdExp* exp) {
    // Manejo de error.X — tratar como constante entera única
    if (exp->value.substr(0, 6) == "error.") {
        // Asignamos un entero único por nombre de error
        if (errorCodes.find(exp->value) == errorCodes.end())
            errorCodes[exp->value] = (int)errorCodes.size() + 1;
        cout << "    movq  $" << errorCodes[exp->value] << ", %rax" << endl;
        return Value();
    }

    if (posicion.find(exp->value) == posicion.end()) {
        cerr << "[GenCode] Error: variable no declarada: '" << exp->value << "'" << endl;
        exit(1);
    }
    cout << "    movq  " << offset(exp->value) << ", %rax" << endl;
    return Value();
}

// ── BinaryExp ────────────────────────────────────────────
//
//  Invariante: left → %rax, right → %rcx, resultado → %rax
//
//  AT&T cmpq src, dst  ≡  dst − src  (pone flags según dst − src)
//  Después de  cmpq %rcx, %rax   (= %rax − %rcx  = left − right):
//    setl  → left < right   ✓
//    setle → left <= right  ✓
//    setg  → left > right   ✓
//    setge → left >= right  ✓
//    sete  → left == right  ✓
//    setne → left != right  ✓

Value GenCodeVisitor::visit(BinaryExp* exp) {
    // Evaluar operandos
    exp->left->accept(this);
    cout << "    pushq %rax"         << endl;  // salvar left
    exp->right->accept(this);
    cout << "    movq  %rax, %rcx"  << endl;  // right → %rcx
    cout << "    popq  %rax"        << endl;  // left  → %rax

    switch (exp->op) {
        // ── Aritmética ───────────────────────────────────
        case PLUS_OP:
            cout << "    addq  %rcx, %rax" << endl;  // %rax = left + right
            break;

        case MINUS_OP:
            cout << "    subq  %rcx, %rax" << endl;  // %rax = left - right
            break;

        case MUL_OP:
            // imulq src, dst  →  dst = dst * src  (con signo, 64-bit)
            cout << "    imulq %rcx, %rax" << endl;
            break;

        case DIV_OP:
            // idivq divisor:  %rdx:%rax ÷ divisor  →  cociente %rax, resto %rdx
            // Primero extender signo de %rax a %rdx:%rax con cqto (= cqo)
            cout << "    cqto"              << endl;  // sign-extend %rax → %rdx:%rax
            cout << "    idivq %rcx"        << endl;  // %rax = left / right
            break;

        case MODULO_OP:
            cout << "    cqto"              << endl;
            cout << "    idivq %rcx"        << endl;
            cout << "    movq  %rdx, %rax"  << endl;  // resto en %rdx → %rax
            break;

        // ── Comparaciones ────────────────────────────────
        // cmpq %rcx, %rax  fija flags para  left − right
        case MENOR:      // left < right
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setl  %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;  // zero-extend byte → 64 bits
            break;

        case MENORI:     // left <= right
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setle %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case MAYOR:      // left > right
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setg  %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case MAYORI:     // left >= right
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setge %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case IGUALIGUAL: // left == right
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    sete  %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case DIFERENTE_OP: // left != right
            cout << "    cmpq  %rcx, %rax" << endl;
            cout << "    setne %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        // ── Lógicos ──────────────────────────────────────
        case AND:
            // &&: verdadero si left != 0  Y  right != 0
            // Usamos %r10b y %r11b (caller-saved, no interferimos con %al/%cl)
            cout << "    testq %rax, %rax"   << endl;  // left != 0?
            cout << "    setne %r10b"         << endl;
            cout << "    testq %rcx, %rcx"   << endl;  // right != 0?
            cout << "    setne %r11b"         << endl;
            cout << "    andb  %r11b, %r10b"  << endl;
            cout << "    movzbq %r10b, %rax"  << endl;
            break;

        case OR:
            // ||: verdadero si left != 0  O  right != 0
            cout << "    orq   %rcx, %rax"   << endl;  // left | right
            cout << "    setne %al"           << endl;
            cout << "    movzbq %al, %rax"    << endl;
            break;

        case REFERENCIA: // & bitwise AND
            cout << "    andq  %rcx, %rax"   << endl;
            break;

        default:
            cerr << "[GenCode] Operador binario no soportado (op=" << exp->op << ")" << endl;
            break;
    }
    return Value();
}

Value GenCodeVisitor::visit(NotExp* exp) {
    exp->exp->accept(this);                            // operando → %rax
    cout << "    testq %rax, %rax"  << endl;           // fijar flags
    cout << "    sete  %al"          << endl;           // 1 si operando == 0
    cout << "    movzbq %al, %rax"   << endl;
    return Value();
}

Value GenCodeVisitor::visit(UnaryExp* exp) {
    exp->exp->accept(this);                            // operando → %rax

    switch (exp->op) {
        case UnaryExp::NEGATE:
            // Negación aritmética: %rax = −%rax
            cout << "    negq  %rax" << endl;
            break;

        case UnaryExp::NOT_OP:
            // Negación lógica: 0→1, cualquier_otro→0
            cout << "    testq %rax, %rax" << endl;
            cout << "    sete  %al"         << endl;
            cout << "    movzbq %al, %rax"  << endl;
            break;

        case UnaryExp::ADDRESS:
            // &var — la dirección, no el valor.
            // Solo funciona si el sub-nodo es un IdExp (caso común).
            if (IdExp* id = dynamic_cast<IdExp*>(exp->exp)) {
                // leaq carga la dirección efectiva sin acceder a memoria
                cout << "    leaq  " << offset(id->value) << ", %rax" << endl;
            } else {
                // Caso general: el valor en %rax ya es la dirección (p.ej. array base)
                // No hay nada más que hacer.
            }
            break;

        case UnaryExp::DEREF:
            // *ptr — desreferenciar: leer 64 bits desde la dirección en %rax
            cout << "    movq  (%rax), %rax" << endl;
            break;
    }
    return Value();
}

// ── FcallExp ─────────────────────────────────────────────
//
//  Convención System V AMD64 para argumentos:
//    Args 1..6  → %rdi %rsi %rdx %rcx %r8 %r9  (en ese orden)
//    Args 7+    → stack (en orden inverso, antes del call)
//
//  Algoritmo:
//    1. Evaluar todos los argumentos de derecha a izquierda y apilarlos.
//    2. Mover los primeros 6 del stack a sus registros (%rdi..%r9).
//    3. Los restantes (args 7+) ya están en el stack en el orden correcto.
//    4. Ajustar %rsp si hay args en stack (limpiar después del call).

// ── Macro de alineación de stack ─────────────────────────
//  Antes de cualquier "call" el ABI exige %rsp % 16 == 0.
//  Usamos %r12 (callee-saved) como registro de salvado de %rsp original:
//    movq %rsp, %r12   ; guardar rsp real
//    andq $-16, %rsp   ; alinear (borra bits 0-3)
//    ... call ...
//    movq %r12, %rsp   ; restaurar rsp
//  Esto es seguro porque %r12 es callee-saved: la función llamada lo preserva.
//  NOTA: si hay llamadas anidadas (una expresión que llama funciones dentro
//  de los argumentos de otra llamada), el %r12 de la llamada exterior podría
//  ser sobrescrito por la interior. Para el compilador de curso esto no ocurre
//  porque los argumentos se evalúan y apilan ANTES de cargarlos en registros.

static void emitAlignedCall(const std::string& nombre) {
    std::cout << "    movq  %rsp, %r12"     << std::endl; // salvar %rsp
    std::cout << "    andq  $-16, %rsp"     << std::endl; // alinear a 16 bytes
    std::cout << "    xorq  %rax, %rax"     << std::endl; // 0 regs XMM (varargs)
    std::cout << "    call  " << nombre      << std::endl;
    std::cout << "    movq  %r12, %rsp"     << std::endl; // restaurar %rsp
}

Value GenCodeVisitor::visit(FcallExp* exp) {
    int nArgs      = (int)exp->argumentos.size();
    int nRegArgs   = min(nArgs, MAX_REG_ARGS);
    int nStackArgs = max(0, nArgs - MAX_REG_ARGS);

    // Paso 1: evaluar todos los argumentos y apilarlos (derecha a izquierda).
    // Los argumentos se evalúan antes de tocar %r12, así que no hay conflicto.
    for (int i = nArgs - 1; i >= 0; --i) {
        exp->argumentos[i]->accept(this);
        cout << "    pushq %rax" << endl;
    }

    // Paso 2: mover los primeros nRegArgs del stack a sus registros de argumento.
    for (int i = 0; i < nRegArgs; ++i)
        cout << "    popq  " << argRegs[i] << endl;

    // Paso 3 + alineación: salvar %rsp en %r12, alinear, llamar, restaurar.
    // Los nStackArgs restantes ya están en el stack correctamente posicionados.
    emitAlignedCall(exp->nombre);

    // Paso 4: limpiar los argumentos que quedaron en el stack (si los hay).
    if (nStackArgs > 0)
        cout << "    addq  $" << (nStackArgs * 8) << ", %rsp" << endl;

    // Resultado de la función queda en %rax (convención System V AMD64).
    return Value();
}

Value GenCodeVisitor::visit(NewExp* exp) {
    // malloc(size): aloca memoria en el heap.
    // Asumimos 8 bytes por objeto; un compilador con tipos usaría sizeof(tipo).
    cout << "    movq  $8, %rdi"   << endl;     // arg1: tamaño en bytes
    emitAlignedCall("malloc@PLT");               // %rax ← puntero alocado
    return Value();
}

Value GenCodeVisitor::visit(NullExp* exp) {
    // null = puntero nulo = 0
    cout << "    xorq  %rax, %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(UndefinedExp* exp) {
    // undefined: dejamos %rax con valor 0 (indeterminado en semántica,
    // pero necesitamos generar algo).
    cout << "    xorq  %rax, %rax" << endl;
    return Value();
}

// ── StringExp ────────────────────────────────────────────
//
//  En lugar de emitir inline dentro de .text (que fragmenta secciones y
//  confunde al ensamblador), acumulamos los literales en stringLiterals
//  y los emitimos al final en .rodata desde gencode().
//  Aquí solo generamos la instrucción leaq que carga la dirección.

Value GenCodeVisitor::visit(StringExp* exp) {
    string lbl = internString(exp->valor);
    // leaq label(%rip), %rax: carga dirección relativa a %rip (PIC-safe)
    cout << "    leaq  " << lbl << "(%rip), %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(CharExp* exp) {
    // Un carácter cabe en un inmediato de 64 bits como su valor ASCII
    cout << "    movq  $" << (int)(unsigned char)exp->valor << ", %rax" << endl;
    return Value();
}

Value GenCodeVisitor::visit(ReferenceExp* exp) {
    if (exp->exp) {
        if (IdExp* id = dynamic_cast<IdExp*>(exp->exp)) {
            // &id → dirección en el frame
            cout << "    leaq  " << offset(id->value) << ", %rax" << endl;
        } else {
            // Expresión compleja: evaluar y asumir que %rax ya es una dirección
            exp->exp->accept(this);
        }
    }
    return Value();
}

Value GenCodeVisitor::visit(PunteroExp* exp) {
    if (exp->exp) {
        exp->exp->accept(this);                        // dirección → %rax
        cout << "    movq  (%rax), %rax" << endl;      // desreferenciar
    }
    return Value();
}

Value GenCodeVisitor::visit(PuntoExp* exp) {
    // Acceso a campo de struct: requiere conocer el layout de cada struct.
    // Stub: evaluamos la base y devolvemos su valor (incorrecto en general,
    // pero evita un crash del generador).
    exp->exp->accept(this);
    // TODO: añadir offset del campo cuando haya tabla de structs.
    return Value();
}

Value GenCodeVisitor::visit(AlgoconcorchetesExp* exp) {
    // array[index]:
    //   base  → %rax → push
    //   index → %rax
    //   base  ← pop → %rcx
    //   resultado = *(base + index*8)   (elementos de 8 bytes)
    exp->nombre->accept(this);                         // dirección base → %rax
    cout << "    pushq %rax"            << endl;
    exp->dentroexp->accept(this);                      // índice → %rax
    cout << "    popq  %rcx"            << endl;       // base → %rcx
    // Modo de direccionamiento indexado: base + índice × escala
    cout << "    movq  (%rcx,%rax,8), %rax" << endl;  // carga elemento
    return Value();
}

Value GenCodeVisitor::visit(AlgoconcorchetesylistaExp* exp) {
    // Array con lista de índices (p.ej. matriz[i,j]): stub
    // Evaluamos la base y retornamos su dirección sin indexar.
    exp->nombre->accept(this);
    return Value();
}

Value GenCodeVisitor::visit(LambdaExp* exp) {
    // Las lambdas con captura necesitan closures (estructuras en heap).
    // Stub: retornamos 0. Un compilador completo emitiría una función
    // anónima y construiría el closure en %rax.
    cout << "    xorq  %rax, %rax" << endl;
    return Value();
}

// ── Tipos — stubs ─────────────────────────────────────────
//  Los nodos de tipo no generan código en la fase de emisión;
//  se usan solo para análisis semántico (fases previas).

void GenCodeVisitor::visit(Structdec*  sd) { }
void GenCodeVisitor::visit(Template*   t)  { }
void GenCodeVisitor::visit(IdType*      t) { }
void GenCodeVisitor::visit(PointerType* t) { }
void GenCodeVisitor::visit(ArrayType*   t) { }
void GenCodeVisitor::visit(OptionalType*t) { }
void GenCodeVisitor::visit(ErrorType*   t) { }
void GenCodeVisitor::visit(UnionType*   t) { }
void GenCodeVisitor::visit(EnumType*    t) { }
void GenCodeVisitor::visit(DerefAssignStmt*    t) { }