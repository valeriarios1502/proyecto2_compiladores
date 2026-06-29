#ifndef VISITOR_H
#define VISITOR_H

#include "ast.h"
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "environment.h"

// ── forward declarations ──────────────────────────────────────────────────────
class BinaryExp;
class NumberExpDecimal;
class NumberExpFlotante;
class StringExp;
class CharExp;
class IdExp;
class BoolExp;
class NotExp;
class FcallExp;
class UnaryExp;
class NewExp;
class NullExp;
class UndefinedExp;
class ReferenceExp;
class PunteroExp;
class AlgoconcorchetesylistaExp;
class AlgoconcorchetesExp;
class PuntoExp;
class LambdaExp;
class IfStmt;
class WhileStmt;
class BodyStmt;
class AsignStmt;
class PrintStmt;
class ReturnStm;
class DeleteStm;
class ContinueStm;
class BreakStmt;
class SwitchStmt;
class TryStmt;
class DeferStmt;
class ForStmt;
class Fundec;
class Structdec;
class VarDec;
class ConstDec;
class Template;
class IdType;
class PointerType;
class ArrayType;
class OptionalType;
class ErrorType;
class UnionType;
class EnumType;
class Body;
class Programa;

// =============================================================================
//   Visitor — clase base abstracta
// =============================================================================
class Visitor {
public:
    virtual ~Visitor() = default;

    // Expresiones → retornan Value
    virtual Value visit(BinaryExp* exp)                = 0;
    virtual Value visit(NumberExpDecimal* exp)          = 0;
    virtual Value visit(NumberExpFlotante* exp)         = 0;
    virtual Value visit(StringExp* exp)                 = 0;
    virtual Value visit(CharExp* exp)                   = 0;
    virtual Value visit(IdExp* exp)                     = 0;
    virtual Value visit(BoolExp* exp)                   = 0;
    virtual Value visit(NotExp* exp)                    = 0;
    virtual Value visit(FcallExp* exp)                  = 0;
    virtual Value visit(UnaryExp* exp)                  = 0;
    virtual Value visit(NewExp* exp)                    = 0;
    virtual Value visit(NullExp* exp)                   = 0;
    virtual Value visit(UndefinedExp* exp)              = 0;
    virtual Value visit(ReferenceExp* exp)              = 0;
    virtual Value visit(PunteroExp* exp)                = 0;
    virtual Value visit(AlgoconcorchetesylistaExp* exp) = 0;
    virtual Value visit(AlgoconcorchetesExp* exp)       = 0;
    virtual Value visit(PuntoExp* exp)                  = 0;
    virtual Value visit(LambdaExp* exp)                 = 0;

    // Statements → retornan void
    virtual void visit(IfStmt* stm)        = 0;
    virtual void visit(WhileStmt* stm)     = 0;
    virtual void visit(BodyStmt* stm)      = 0;
    virtual void visit(AsignStmt* stm)     = 0;
    virtual void visit(PrintStmt* stm)     = 0;
    virtual void visit(ReturnStm* stm)     = 0;
    virtual void visit(DeleteStm* stm)     = 0;
    virtual void visit(ContinueStm* stm)   = 0;
    virtual void visit(BreakStmt* stm)     = 0;
    virtual void visit(SwitchStmt* stm)    = 0;
    virtual void visit(TryStmt* stm)       = 0;
    virtual void visit(DeferStmt* stm)     = 0;
    virtual void visit(ForStmt* stm)       = 0;
    virtual void visit(DerefAssignStmt* e) = 0;

    // Declaraciones → retornan void
    virtual void visit(Fundec* fd)    = 0;
    virtual void visit(Structdec* sd) = 0;
    virtual void visit(VarDec* vd)    = 0;
    virtual void visit(ConstDec* cd)  = 0;
    virtual void visit(Template* t)   = 0;

    // Tipos → retornan void
    virtual void visit(IdType* tipo)       = 0;
    virtual void visit(PointerType* tipo)  = 0;
    virtual void visit(ArrayType* tipo)    = 0;
    virtual void visit(OptionalType* tipo) = 0;
    virtual void visit(ErrorType* tipo)    = 0;
    virtual void visit(UnionType* tipo)    = 0;
    virtual void visit(EnumType* tipo)     = 0;

    // Body / Programa
    virtual void visit(Body* b)     = 0;
    virtual void visit(Programa* p) = 0;
};

// =============================================================================
//   GenCodeVisitor — generación de código x86-64 AT&T
//
//  Convenciones System V AMD64:
//   Caller-saved : %rax %rcx %rdx %rsi %rdi %r8 %r9 %r10 %r11
//   Callee-saved : %rbx %rbp %r12 %r13 %r14 %r15
//   Argumentos   : %rdi %rsi %rdx %rcx %r8 %r9  (en ese orden)
//   Retorno      : %rax
//
//  Variables locales:
//   posicion["x"] = N  →  slot N  →  -N*8(%rbp)
//
//  Structs:
//   structFieldOffsets[nombreStruct][campo] = byteOffset  (i*8 para campo i)
//   structFieldCount[nombreStruct]          = número de campos
//   Una instancia local ocupa nFields slots consecutivos en el frame.
//   El slot posicion["v"] almacena la DIRECCIÓN BASE del struct
//   (puesta con leaq en VarDec, o con malloc en NewExp).
//
//  Arrays:
//   Locales ([N]T): N slots consecutivos; posicion["a"] = slot del elemento 0.
//   El slot almacena la dirección base (leaq -baseSlot*8(%rbp), %rax).
//   En heap (new [N]T): malloc(N*8), dirección en %rax → guardada en slot.
// =============================================================================
class GenCodeVisitor : public Visitor {
public:
    bool hayComptimeGlobal = false;

    std::unordered_map<std::string, std::string> globalTypes; // nombre → "int"|"float"|"str"|"char"|"bool"
    std::unordered_set<std::string> globalNames;  
    // ── Variables locales: nombre → slot (1-based) ───────────────────────
    //   slot N  →  -N*8(%rbp)
    std::unordered_map<std::string, int> posicion;

    // ── Constantes de error: "error.X" → entero único ────────────────────
    std::unordered_map<std::string, int> errorCodes;

    // ── Próximo slot libre en el frame (empieza en 1) ────────────────────
    int varContador = 1;

    // ── Literales de string acumulados para .rodata ───────────────────────
    std::vector<std::pair<std::string, std::string>> stringLiterals;  // (label, value)

    // ── Funciones registradas para forward references ─────────────────────
    std::unordered_map<std::string, Fundec*> funEnv;

    // ── Tabla de structs: nombre_struct → { campo → byteOffset } ─────────
    std::unordered_map<std::string,
        std::unordered_map<std::string, int>> structFieldOffsets;

    // ── Número de campos por struct ───────────────────────────────────────
    std::unordered_map<std::string, int> structFieldCount;

    // ── Contador global de etiquetas únicas ──────────────────────────────
    int labelCounter = 0;

    // ── Etiquetas del bucle actual (break / continue) ─────────────────────
    std::string currentLoopEnd;
    std::string currentLoopStart;

    // ── Nombre de la función en generación ───────────────────────────────
    std::string currentFunction;

    // ── Punto de entrada público ──────────────────────────────────────────
    void gencode(Programa* programa);

    // =========================================================================
    //  Helpers
    // =========================================================================

    // Genera etiqueta única: newLabel("if") → "if_0", "if_1", …
    std::string newLabel(const std::string& base) {
        return base + "_" + std::to_string(labelCounter++);
    }

    // Dirección de una variable local:  offset("x") → "-8(%rbp)"  si slot==1
    std::string offset(const std::string& nombre) {
        return std::to_string(posicion.at(nombre) * -8) + "(%rbp)";
    }

    // Acumula un literal de string y devuelve su etiqueta
    std::string internString(const std::string& valor) {
        std::string lbl = newLabel("LC");
        stringLiterals.push_back({lbl, valor});
        return lbl;
    }

    // Redondea bytes al múltiplo de 16 superior (alineación ABI)
    static int alignFrame(int slots) {
        int bytes = slots * 8;
        return (bytes + 15) & ~15;
    }

    // Emite una llamada alineada a 16 bytes usando %r12 como scratch callee-saved
    void emitAlignedCall(const std::string& target);

    // Emite instrucciones para dejar en %rax la dirección base del struct
    // al que apunta la expresión 'expr' (variable local o puntero heap).
    void emitStructBaseAddress(Exp* expr);

    // Devuelve el byte offset del campo 'fieldName' buscando en structFieldOffsets.
    int getFieldOffset(Exp* base, const std::string& fieldName);

    // ── Helpers para globales ─────────────────────────────────────────────
    void emitGlobalVarDec(VarDec* vd);
    void emitGlobalConstDec(ConstDec* cd);

    // =========================================================================
    //  Visitas — Expresiones
    // =========================================================================
    Value visit(BinaryExp* exp)                override;
    Value visit(NumberExpDecimal* exp)          override;
    Value visit(NumberExpFlotante* exp)         override;
    Value visit(StringExp* exp)                 override;
    Value visit(CharExp* exp)                   override;
    Value visit(IdExp* exp)                     override;
    Value visit(BoolExp* exp)                   override;
    Value visit(NotExp* exp)                    override;
    Value visit(FcallExp* exp)                  override;
    Value visit(UnaryExp* exp)                  override;
    Value visit(NewExp* exp)                    override;
    Value visit(NullExp* exp)                   override;
    Value visit(UndefinedExp* exp)              override;
    Value visit(ReferenceExp* exp)              override;
    Value visit(PunteroExp* exp)                override;
    Value visit(AlgoconcorchetesylistaExp* exp) override;
    Value visit(AlgoconcorchetesExp* exp)       override;
    Value visit(PuntoExp* exp)                  override;
    Value visit(LambdaExp* exp)                 override;

    // =========================================================================
    //  Visitas — Statements
    // =========================================================================
    void visit(IfStmt* stm)        override;
    void visit(WhileStmt* stm)     override;
    void visit(BodyStmt* stm)      override;
    void visit(AsignStmt* stm)     override;
    void visit(PrintStmt* stm)     override;
    void visit(ReturnStm* stm)     override;
    void visit(DeleteStm* stm)     override;
    void visit(ContinueStm* stm)   override;
    void visit(BreakStmt* stm)     override;
    void visit(SwitchStmt* stm)    override;
    void visit(TryStmt* stm)       override;
    void visit(DeferStmt* stm)     override;
    void visit(ForStmt* stm)       override;
    void visit(DerefAssignStmt* stm) override;

    // =========================================================================
    //  Visitas — Declaraciones
    // =========================================================================
    void visit(Fundec* fd)    override;
    void visit(Structdec* sd) override;
    void visit(VarDec* vd)    override;
    void visit(ConstDec* cd)  override;
    void visit(Template* t)   override;

    // =========================================================================
    //  Visitas — Tipos (stubs, no generan código)
    // =========================================================================
    void visit(IdType* tipo)       override;
    void visit(PointerType* tipo)  override;
    void visit(ArrayType* tipo)    override;
    void visit(OptionalType* tipo) override;
    void visit(ErrorType* tipo)    override;
    void visit(UnionType* tipo)    override;
    void visit(EnumType* tipo)     override;

    // =========================================================================
    //  Visitas — Body / Programa
    // =========================================================================
    void visit(Body* b)     override;
    void visit(Programa* p) override;
};

#endif // VISITOR_H