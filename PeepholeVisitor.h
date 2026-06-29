#ifndef PEEPHOLE_VISITOR_H
#define PEEPHOLE_VISITOR_H

// =============================================================================
// PeepholeVisitor.h
// Optimización de Mirilla (Peephole) para minizig
// Opera sobre el stream de instrucciones x86-64 (AT&T) ya generado.
// =============================================================================
//
// ESTRATEGIA DE USO
// ─────────────────
// En lugar de emitir directamente a cout, GenCodeVisitor redirige su salida
// a un PeepholeBuffer (ostringstream).  Al terminar la generación, se llama
// a PeepholePass::optimize() que aplica las reglas de mirilla sobre el buffer
// y vuelca el resultado limpio a cout.
//
//   PeepholeBuffer pb;
//   GenCodeVisitor gcv(pb);   // o bien: cout.rdbuf(pb.rdbuf())
//   gcv.gencode(programa);
//   PeepholePass pp;
//   pp.optimize(pb, std::cout);
//
// VENTANA DE MIRILLA
// ──────────────────
// La ventana es de N instrucciones (por defecto N=4).  Se desliza instrucción
// a instrucción sobre el vector de líneas.  Cuando una regla hace match, las
// instrucciones involucradas se reemplazan/eliminan y la ventana retrocede para
// permitir que la regla se aplique de nuevo (efecto cascada de la mirilla).
//
// REGLAS IMPLEMENTADAS (en orden de prioridad)
// ─────────────────────────────────────────────
//
//  R1  movq X, %rax          R1: copy-into-self
//      movq %rax, %rax       →  movq X, %rax
//
//  R2  movq X, %rax          R2: load-store elimination
//      movq %rax, X          →  movq X, %rax
//      (cuando X es el mismo operando)
//
//  R3  pushq %rax            R3: push-pop elimination
//      popq  %rax            →  (vacío — no-op)
//
//  R4  pushq %rax            R4: push-pop with different reg, no intervención
//      popq  %rcx            →  movq %rax, %rcx
//
//  R5  movq $0, %rax         R5: xor-self equivalence (semántica idéntica)
//      → xorq %rax, %rax    (instrucción más corta, 2 bytes vs 7)
//
//  R6  addq $0, <reg>        R6: add-zero elimination
//      → (eliminado)
//
//  R7  subq $0, <reg>        R7: sub-zero elimination
//      → (eliminado)
//
//  R8  imulq $1, <reg>       R8: mul-one elimination
//      → (eliminado)
//
//  R9  movq A, %rax          R9: mov-chain collapse
//      movq %rax, %rcx       →  movq A, %rcx
//      (cuando %rax no se usa después de la segunda instrucción)
//
//  R10 movq %rax, %rcx       R10: consecutive same-dst redundancy
//      movq %rdx, %rcx       →  movq %rdx, %rcx
//      (el primer movq a %rcx es inútil si lo sobreescribe el segundo)
//
//  R11 cmpq X, Y             R11: cmp-after-cmp (mismo operandos)
//      cmpq X, Y             →  cmpq X, Y
//      (segundo cmp redundante)
//
//  R12 movq %rax, %rcx       R12: xchg-to-move specialization
//      xchgq %rax, %rcx      →  movq %rax, %rcx
//      xchgq %rax, %rcx          (cuando origen y destino de xchg == movq anterior)
//
//  R13 movq A, %rax          R13: load-use-store (operaciones aritméticas)
//      addq/subq/imulq B, %rax    (las dos primeras se fusionan cuando el destino
//      movq %rax, A              de la aritmética es el mismo que el src del load)
//      Si A es la misma celda de memoria local:
//      →  addq/subq/imulq B, A   (operación directa sobre memoria)
//      NOTA: solo aplicable para operaciones que tienen forma mem,reg en x86.
//
// =============================================================================

#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <regex>
#include <algorithm>
#include <functional>

// =============================================================================
//  PeepholeBuffer — buffer de salida intermedio para GenCodeVisitor
//  Sustituye a std::cout durante la generación; luego se optimiza.
// =============================================================================
class PeepholeBuffer {
public:
    std::ostringstream oss;

    // Interfaz compatible con cout para uso en GenCodeVisitor
    template<typename T>
    PeepholeBuffer& operator<<(const T& val) {
        oss << val;
        return *this;
    }

    std::string str() const { return oss.str(); }
    void        clear()      { oss.str(""); oss.clear(); }
};

// =============================================================================
//  Instrucción — línea parseada del stream ASM
// =============================================================================
struct Instr {
    std::string raw;        // línea original (sin salto de línea final)
    std::string mnemonic;   // opcode en minúsculas, ej. "movq"
    std::string src;        // primer operando (AT&T: fuente)
    std::string dst;        // segundo operando (AT&T: destino)
    bool        isLabel;    // termina en ':'
    bool        isDirective;// empieza con '.'
    bool        isEmpty;    // línea vacía / solo espacios

    Instr() : isLabel(false), isDirective(false), isEmpty(false) {}
};

// =============================================================================
//  PeepholePass — motor de optimización de mirilla
// =============================================================================
class PeepholePass {
public:
    // ── Estadísticas ────────────────────────────────────────────────────────
    int rulesApplied = 0;

    // ── Parámetro: tamaño de la ventana (≥ 2) ───────────────────────────────
    int windowSize = 4;

    // =========================================================================
    //  optimize — punto de entrada público
    //  Toma el buffer completo como string, lo optimiza y lo escribe en 'out'.
    // =========================================================================
    void optimize(const std::string& asmText, std::ostream& out) {
        // 1. Tokenizar en líneas
        std::vector<Instr> prog = tokenize(asmText);

        // 2. Aplicar ventana de mirilla — máximo MAX_PASSES para garantizar
        //    terminación (evita ciclos entre reglas que se contrarrestan).
        const int MAX_PASSES = 8;
        for (int pass = 0; pass < MAX_PASSES; ++pass) {
            if (!applyWindow(prog)) break;
        }

        // 3. Volcar resultado
        for (const Instr& ins : prog) {
            out << ins.raw << "\n";
        }
        out.flush();
    }

    // Sobrecarga conveniente: toma PeepholeBuffer directamente
    void optimize(const PeepholeBuffer& pb, std::ostream& out) {
        optimize(pb.str(), out);
    }

private:
    // =========================================================================
    //  tokenize — divide el texto en líneas y parsea cada una
    // =========================================================================
    std::vector<Instr> tokenize(const std::string& text) {
        std::vector<Instr> result;
        std::istringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            result.push_back(parse(line));
        }
        return result;
    }

    // =========================================================================
    //  parse — extrae mnemonic, src, dst de una línea ASM
    // =========================================================================
    Instr parse(const std::string& raw) {
        Instr ins;
        ins.raw = raw;

        // Trim izquierdo para detectar etiquetas / directivas
        size_t start = raw.find_first_not_of(" \t");
        if (start == std::string::npos) { ins.isEmpty = true; return ins; }

        std::string trimmed = raw.substr(start);

        if (trimmed.empty())           { ins.isEmpty = true; return ins; }
        if (trimmed.front() == '#')    { ins.isEmpty = true; return ins; } // comentario
        if (trimmed.front() == '.')    { ins.isDirective = true; return ins; }

        // Detectar etiquetas:
        //   Caso A: línea que termina en ':'   → "while_start_3:"
        //   Caso B: "nombre: .directiva ..."   → "print_int_fmt: .string ..."
        //   Caso C: "nombre: .quad N"          → variable global
        // En todos los casos con ':', marcar como isLabel (no optimizable).
        size_t colonPos = trimmed.find(':');
        if (colonPos != std::string::npos) {
            // Verificar que lo que precede al ':' es un identificador puro
            // (sin espacios), para no confundir con operandos de memoria.
            std::string beforeColon = trimmed.substr(0, colonPos);
            bool pureId = !beforeColon.empty() &&
                          beforeColon.find_first_of(" \t(") == std::string::npos;
            if (pureId) {
                ins.isLabel = true;
                return ins;
            }
        }

        // Extraer mnemonic (primera palabra)
        size_t mnEnd = trimmed.find_first_of(" \t");
        if (mnEnd == std::string::npos) {
            ins.mnemonic = toLower(trimmed);
            return ins;
        }
        ins.mnemonic = toLower(trimmed.substr(0, mnEnd));

        // Extraer operandos (el resto, dividido por ',')
        std::string rest = trimmed.substr(mnEnd);
        // Trim izquierdo
        size_t rStart = rest.find_first_not_of(" \t");
        if (rStart == std::string::npos) return ins;
        rest = rest.substr(rStart);

        // Dividir en src y dst por la primera ',' que no esté dentro de '('...')'
        int depth = 0;
        size_t comma = std::string::npos;
        for (size_t i = 0; i < rest.size(); ++i) {
            if (rest[i] == '(') ++depth;
            else if (rest[i] == ')') --depth;
            else if (rest[i] == ',' && depth == 0) { comma = i; break; }
        }

        if (comma == std::string::npos) {
            ins.src = trim(rest);
        } else {
            ins.src = trim(rest.substr(0, comma));
            ins.dst = trim(rest.substr(comma + 1));
        }

        return ins;
    }

    // =========================================================================
    //  applyWindow — un pase completo de la ventana deslizante
    //  Retorna true si se aplicó al menos una regla.
    // =========================================================================
    bool applyWindow(std::vector<Instr>& prog) {
        bool changed = false;
        size_t i = 0;
        while (i < prog.size()) {
            // Saltar etiquetas, directivas y líneas vacías como ancla
            if (prog[i].isEmpty || prog[i].isLabel || prog[i].isDirective) {
                ++i; continue;
            }

            // Intentar cada regla con ventana que empieza en i
            if (tryRules(prog, i)) {
                changed = true;
                // No avanzamos i: la regla puede haber creado oportunidades nuevas
            } else {
                ++i;
            }
        }
        return changed;
    }

    // =========================================================================
    //  tryRules — aplica la primera regla que haga match en la posición i
    //  Retorna true si alguna regla disparó.
    // =========================================================================
    bool tryRules(std::vector<Instr>& prog, size_t i) {
        // Obtener las siguientes N instrucciones reales como índices en prog[].
        // La ventana se detiene en cuanto encuentra una etiqueta o directiva:
        // son barreras que no se pueden cruzar (un label puede ser destino de
        // salto; una directiva nunca forma parte del flujo de instrucciones).
        std::vector<size_t> win;
        for (size_t j = i; j < prog.size() && (int)win.size() < windowSize; ++j) {
            if (prog[j].isLabel || prog[j].isDirective) break;  // barrera
            if (!prog[j].isEmpty)
                win.push_back(j);
        }
        if (win.empty()) return false;

        // ── R1: movq X, %rax ; movq %rax, %rax → movq X, %rax ──────────────
        if (win.size() >= 2) {
            Instr& a = prog[win[0]];
            Instr& b = prog[win[1]];
            if (a.mnemonic == "movq" && b.mnemonic == "movq" &&
                b.src == b.dst) {
                remove(prog, win[1]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R2: movq X, %rax ; movq %rax, X → movq X, %rax (load-store) ────
        if (win.size() >= 2) {
            Instr& a = prog[win[0]];
            Instr& b = prog[win[1]];
            if (a.mnemonic == "movq" && b.mnemonic == "movq" &&
                a.src == b.dst && a.dst == b.src) {
                // La segunda instrucción es redundante
                remove(prog, win[1]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R3: pushq %rax ; popq %rax → (eliminados) ───────────────────────
        if (win.size() >= 2) {
            Instr& a = prog[win[0]];
            Instr& b = prog[win[1]];
            if (a.mnemonic == "pushq" && b.mnemonic == "popq" &&
                a.src == b.src) {
                removeTwo(prog, win[0], win[1]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R4: pushq %rax ; popq %rcx → movq %rax, %rcx ───────────────────
        if (win.size() >= 2) {
            Instr& a = prog[win[0]];
            Instr& b = prog[win[1]];
            if (a.mnemonic == "pushq" && b.mnemonic == "popq" &&
                a.src != b.src &&
                isRegister(a.src) && isRegister(b.src)) {
                std::string newRaw = "    movq  " + a.src + ", " + b.src;
                Instr rep = parse(newRaw);
                rep.raw = newRaw;
                prog[win[0]] = rep;
                remove(prog, win[1]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R5: movq $0, <reg> → xorq <reg>, <reg> ──────────────────────────
        if (win.size() >= 1) {
            Instr& a = prog[win[0]];
            if (a.mnemonic == "movq" && a.src == "$0" && isRegister(a.dst)) {
                std::string newRaw = "    xorq  " + a.dst + ", " + a.dst;
                Instr rep = parse(newRaw);
                rep.raw = newRaw;
                prog[win[0]] = rep;
                ++rulesApplied;
                return true;
            }
        }

        // ── R6: addq $0, <dst> → eliminar ───────────────────────────────────
        if (win.size() >= 1) {
            Instr& a = prog[win[0]];
            if (a.mnemonic == "addq" && a.src == "$0") {
                remove(prog, win[0]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R7: subq $0, <dst> → eliminar ───────────────────────────────────
        if (win.size() >= 1) {
            Instr& a = prog[win[0]];
            if (a.mnemonic == "subq" && a.src == "$0") {
                remove(prog, win[0]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R8: imulq $1, <dst> → eliminar ──────────────────────────────────
        if (win.size() >= 1) {
            Instr& a = prog[win[0]];
            if (a.mnemonic == "imulq" && a.src == "$1") {
                remove(prog, win[0]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R9: movq A, %rax ; movq %rax, %rcx → movq A, %rcx ──────────────
        //    (solo si %rax no es el destino final que se necesita después)
        if (win.size() >= 2) {
            Instr& a = prog[win[0]];
            Instr& b = prog[win[1]];
            if (a.mnemonic == "movq" && b.mnemonic == "movq" &&
                a.dst == b.src && isRegister(a.dst) &&
                a.dst != b.dst &&                     // distintos dst
                !isMem(a.src)) {                       // src simple (no indirecto complejo)
                // Verificar que el registro intermedio (a.dst) no se usa en win[2]
                bool usedAfter = false;
                if (win.size() >= 3) {
                    Instr& c = prog[win[2]];
                    usedAfter = (c.src == a.dst || c.dst == a.dst);
                }
                if (!usedAfter) {
                    std::string newRaw = "    movq  " + a.src + ", " + b.dst;
                    Instr rep = parse(newRaw);
                    rep.raw = newRaw;
                    prog[win[0]] = rep;
                    remove(prog, win[1]);
                    ++rulesApplied;
                    return true;
                }
            }
        }

        // ── R10: movq A, %rcx ; movq B, %rcx → movq B, %rcx (sobrescritura) ─
        //    Solo seguro si el dst de A no es src de B (no hay dependencia).
        if (win.size() >= 2) {
            Instr& a = prog[win[0]];
            Instr& b = prog[win[1]];
            if (a.mnemonic == "movq" && b.mnemonic == "movq" &&
                a.dst == b.dst && isRegister(a.dst) &&
                a.dst != b.src) {  // guardia: b no lee lo que a escribió
                remove(prog, win[0]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R11: cmpq A, B ; cmpq A, B → cmpq A, B (cmp redundante) ────────
        if (win.size() >= 2) {
            Instr& a = prog[win[0]];
            Instr& b = prog[win[1]];
            if (a.mnemonic == "cmpq" && b.mnemonic == "cmpq" &&
                a.src == b.src && a.dst == b.dst) {
                remove(prog, win[1]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R12: movq %rax, %rcx ; xchgq %rax, %rcx → movq %rax, %rcx ─────
        //    xchg + prev mov con mismos operandos → solo el mov
        if (win.size() >= 2) {
            Instr& a = prog[win[0]];
            Instr& b = prog[win[1]];
            if (a.mnemonic == "movq" && b.mnemonic == "xchgq" &&
                a.src == b.src && a.dst == b.dst) {
                // El xchg cancela el estado que el movq estableció → solo movq
                remove(prog, win[1]);
                ++rulesApplied;
                return true;
            }
        }

        // ── R13: movq <mem>, %rax ; OP %rcx, %rax ; movq %rax, <mem> ────────
        //         → OP %rcx, <mem>   (operación directa sobre memoria)
        //    Solo para add/sub/imul donde <mem> es una celda local simple.
        if (win.size() >= 3) {
            Instr& a = prog[win[0]];  // movq MEM, %rax
            Instr& b = prog[win[1]];  // OP  SRC, %rax
            Instr& c = prog[win[2]];  // movq %rax, MEM
            bool memLoad  = (a.mnemonic == "movq" && isMem(a.src) && a.dst == "%rax");
            bool memStore = (c.mnemonic == "movq" && c.src == "%rax" && c.dst == a.src);
            bool arith    = (b.mnemonic == "addq" || b.mnemonic == "subq" ||
                             b.mnemonic == "imulq") && b.dst == "%rax";
            if (memLoad && memStore && arith) {
                // Emitir OP SRC, MEM
                std::string newRaw = "    " + b.mnemonic + " " + b.src + ", " + a.src;
                Instr rep = parse(newRaw);
                rep.raw = newRaw;
                prog[win[0]] = rep;
                remove(prog, win[2]);   // eliminar store (índice win[2] se desplazó -1)
                remove(prog, win[1]);   // eliminar arith original
                ++rulesApplied;
                return true;
            }
        }

        return false;
    }

    // =========================================================================
    //  Helpers de manipulación del vector
    // =========================================================================

    void remove(std::vector<Instr>& prog, size_t idx) {
        prog.erase(prog.begin() + idx);
    }

    void removeTwo(std::vector<Instr>& prog, size_t i1, size_t i2) {
        // Eliminar el mayor primero para no invalidar el menor
        size_t hi = std::max(i1, i2);
        size_t lo = std::min(i1, i2);
        prog.erase(prog.begin() + hi);
        prog.erase(prog.begin() + lo);
    }

    // =========================================================================
    //  Predicados auxiliares
    // =========================================================================

    // ¿Es el operando un registro x86-64 (empieza con '%')?
    bool isRegister(const std::string& op) const {
        return !op.empty() && op[0] == '%';
    }

    // ¿Es el operando un acceso a memoria (contiene '(')?
    bool isMem(const std::string& op) const {
        return op.find('(') != std::string::npos;
    }

    // =========================================================================
    //  Utilidades de string
    // =========================================================================

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    static std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }
};

// =============================================================================
//  Función helper global para integrar fácilmente en gencode()
//
//  Ejemplo de uso:
//
//    // En gencode(), antes de emitir:
//    std::ostringstream buf;
//    std::streambuf* oldBuf = std::cout.rdbuf(buf.rdbuf());
//
//    // ... toda la generación normal usando cout ...
//
//    std::cout.rdbuf(oldBuf);   // restaurar cout
//    runPeephole(buf.str(), std::cout);
//
// =============================================================================
inline void runPeephole(const std::string& asmText, std::ostream& out) {
    PeepholePass pp;
    pp.optimize(asmText, out);
}

#endif // PEEPHOLE_VISITOR_H