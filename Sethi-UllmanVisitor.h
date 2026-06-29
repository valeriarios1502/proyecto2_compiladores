#ifndef SETHI_ULLMAN_VISITOR_H
#define SETHI_ULLMAN_VISITOR_H

// =============================================================================
// Sethi-UllmanVisitor.h
// Sethi-Ullman para minizig — pase previo a GenCodeVisitor
// =============================================================================
// Uso en gencode():
//   SethiUllmanPass su;
//   su.run(programa);
//   shuMap = std::move(su.shuMap);
// =============================================================================

#include "ast.h"
#include <unordered_map>
#include <algorithm>

// -----------------------------------------------------------------------------
// Anotación por nodo Exp
// -----------------------------------------------------------------------------
struct ShuInfo {
    int  label   = 1;     // registros mínimos necesarios (Sethi-Ullman)
    bool isLeaf  = false; // ¿es hoja del árbol de expresiones?
    bool isConst = false; // ¿es constante literal? (label=0, no consume registro)
};

using ShuMap = std::unordered_map<Exp*, ShuInfo>;

// -----------------------------------------------------------------------------
// SethiUllmanPass
// -----------------------------------------------------------------------------
class SethiUllmanPass {
public:
    ShuMap shuMap;

    void run(Programa* prog) {
        if (!prog) return;
        for (Top_dec* d : prog->declist) {
            if (Fundec*   fd = dynamic_cast<Fundec*>(d))   processBody(fd->cuerpo);
            if (Template* t  = dynamic_cast<Template*>(d)) processBody(t->block);
        }
    }

private:
    // ── Recorrido de cuerpo y sentencias ────────────────────────────────────
    void processBody(Body* b) {
        if (!b) return;
        for (Stmt* s : b->slist) processStmt(s);
    }

    void processStmt(Stmt* s) {
        if (!s) return;

        if (VarDec*          vd = dynamic_cast<VarDec*>(s))          { if (vd->exp)  label(vd->exp);  }
        else if (ConstDec*   cd = dynamic_cast<ConstDec*>(s))        { if (cd->exp)  label(cd->exp);  }
        else if (AsignStmt*  as = dynamic_cast<AsignStmt*>(s))       { if (as->exp)  label(as->exp);  }
        else if (PrintStmt*  ps = dynamic_cast<PrintStmt*>(s))       { if (ps->exp)  label(ps->exp);  }
        else if (ReturnStm*  rs = dynamic_cast<ReturnStm*>(s))       { if (rs->exp)  label(rs->exp);  }
        else if (IfStmt*     is = dynamic_cast<IfStmt*>(s)) {
            label(is->condicion);
            processBody(is->cuerpodelif);
            if (is->hayelse) processBody(is->cuerpodelelse);
        }
        else if (WhileStmt*  ws = dynamic_cast<WhileStmt*>(s)) {
            label(ws->condicion);
            for (Stmt* inner : ws->cuerpodelwhile) processStmt(inner);
        }
        else if (ForStmt*    fs = dynamic_cast<ForStmt*>(s)) {
            if (fs->asignacion) processStmt(fs->asignacion);
            if (fs->condicion)  label(fs->condicion);
            if (fs->incremento) processStmt(fs->incremento);
            processBody(fs->cuerpo);
        }
        else if (SwitchStmt* sw = dynamic_cast<SwitchStmt*>(s)) {
            label(sw->condicion);
            for (auto& cas : sw->casos) {
                label(cas.first);
                if (cas.second) processBody(cas.second);
            }
            if (sw->default_caso) processBody(sw->default_caso);
        }
        else if (BodyStmt*   bs = dynamic_cast<BodyStmt*>(s))        { processBody(bs->cuerpo); }
        else if (DerefAssignStmt* da = dynamic_cast<DerefAssignStmt*>(s)) {
            label(da->rval);
            label(da->lval);
        }
    }

    // ── label(Exp*) — calcula el número de registros mínimos (Sethi-Ullman) ─
    //
    // Reglas:
    //   Hoja constante (literal)         → 0  (se usa como inmediato, sin registro)
    //   Hoja variable  (IdExp)           → 1
    //   Nodo binario (lL == rL)          → lL + 1
    //   Nodo binario (lL != rL)          → max(lL, rL)
    //   Llamada a función                → 3  (spill pesado)
    // ────────────────────────────────────────────────────────────────────────
    int label(Exp* exp) {
        if (!exp) return 0;

        // ── Literales → hoja constante, label=0 ────────────────────────────
        if (dynamic_cast<NumberExpDecimal*>(exp)  ||
            dynamic_cast<NumberExpFlotante*>(exp) ||
            dynamic_cast<BoolExp*>(exp)           ||
            dynamic_cast<CharExp*>(exp)           ||
            dynamic_cast<NullExp*>(exp)           ||
            dynamic_cast<UndefinedExp*>(exp)      ||
            dynamic_cast<StringExp*>(exp)) {
            shuMap[exp] = {0, true, true};
            return 0;
        }

        // ── Variable → hoja no constante, label=1 ──────────────────────────
        if (dynamic_cast<IdExp*>(exp)) {
            shuMap[exp] = {1, true, false};
            return 1;
        }

        // ── BinaryExp — núcleo del algoritmo ───────────────────────────────
        if (BinaryExp* bin = dynamic_cast<BinaryExp*>(exp)) {
            int lL = label(bin->left);
            int rL = label(bin->right);

            // Si el hijo derecho es constante → label efectivo = 0
            // (se puede mover como inmediato a %rcx sin pushq)
            auto rIt = shuMap.find(bin->right);
            if (rIt != shuMap.end() && rIt->second.isConst)
                rL = 0;

            int myL = (lL == rL) ? lL + 1 : std::max(lL, rL);
            shuMap[exp] = {myL, false, false};
            return myL;
        }

        // ── UnaryExp / NotExp ───────────────────────────────────────────────
        if (UnaryExp* ue = dynamic_cast<UnaryExp*>(exp)) {
            int l = label(ue->exp);
            shuMap[exp] = {l, false, false};
            return l;
        }
        if (NotExp* ne = dynamic_cast<NotExp*>(exp)) {
            int l = label(ne->exp);
            shuMap[exp] = {l, false, false};
            return l;
        }

        // ── FcallExp → label=3 (conservador: destruye todos los registros) ──
        if (FcallExp* fc = dynamic_cast<FcallExp*>(exp)) {
            for (Exp* arg : fc->argumentos) label(arg);
            shuMap[exp] = {3, false, false};
            return 3;
        }

        // ── Acceso a campo struct.campo ─────────────────────────────────────
        if (PuntoExp* pe = dynamic_cast<PuntoExp*>(exp)) {
            int l = label(pe->exp);
            shuMap[exp] = {l, false, false};
            return l;
        }

        // ── Acceso a array arr[idx] ─────────────────────────────────────────
        if (AlgoconcorchetesExp* ae = dynamic_cast<AlgoconcorchetesExp*>(exp)) {
            int lBase = label(ae->nombre);
            int lIdx  = label(ae->dentroexp);
            // el índice puede ser constante → label efectivo = 0
            auto idxIt = shuMap.find(ae->dentroexp);
            if (idxIt != shuMap.end() && idxIt->second.isConst)
                lIdx = 0;
            int myL = (lBase == lIdx) ? lBase + 1 : std::max(lBase, lIdx);
            shuMap[exp] = {myL, false, false};
            return myL;
        }

        // ── &expr / *expr ───────────────────────────────────────────────────
        if (ReferenceExp* re = dynamic_cast<ReferenceExp*>(exp)) {
            int l = re->exp ? label(re->exp) : 1;
            shuMap[exp] = {l, false, false};
            return l;
        }
        if (PunteroExp* pe = dynamic_cast<PunteroExp*>(exp)) {
            int l = pe->exp ? label(pe->exp) : 1;
            shuMap[exp] = {l, false, false};
            return l;
        }

        // ── NewExp → label=3 (llama a malloc) ──────────────────────────────
        if (dynamic_cast<NewExp*>(exp)) {
            shuMap[exp] = {3, false, false};
            return 3;
        }

        // ── AlgoconcorchetesylistaExp (array 2D) ────────────────────────────
        if (AlgoconcorchetesylistaExp* ale = dynamic_cast<AlgoconcorchetesylistaExp*>(exp)) {
            int l = label(ale->nombre);
            for (Exp* arg : ale->argumentos) {
                int al = label(arg);
                l = std::max(l, al);
            }
            shuMap[exp] = {l + 1, false, false};
            return l + 1;
        }

        // Caso por defecto
        shuMap[exp] = {1, false, false};
        return 1;
    }
};

#endif // SETHI_ULLMAN_VISITOR_H