#ifndef CONSTANT_FOLDING_VISITOR_H
#define CONSTANT_FOLDING_VISITOR_H

// =============================================================================
// ConstantFoldingVisitor.h
// Constant Folding para minizig — pase previo a GenCodeVisitor
// =============================================================================
// Uso en gencode():
//   ConstantFoldingPass cf;
//   cf.run(programa);
//   constMap = std::move(cf.constMap);
// =============================================================================

#include "ast.h"
#include <unordered_map>
#include <cmath>

// -----------------------------------------------------------------------------
// Anotación pegada a cada nodo Exp via mapa externo
// -----------------------------------------------------------------------------
struct ConstInfo {
    bool   isConst = false;
    double value   = 0.0;   // double cubre enteros y flotantes
};

using ConstMap = std::unordered_map<Exp*, ConstInfo>;

// -----------------------------------------------------------------------------
// ConstantFoldingPass
// -----------------------------------------------------------------------------
class ConstantFoldingPass {
public:
    ConstMap constMap;

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

        if (VarDec*          vd = dynamic_cast<VarDec*>(s))          { if (vd->exp)  fold(vd->exp);  }
        else if (ConstDec*   cd = dynamic_cast<ConstDec*>(s))        { if (cd->exp)  fold(cd->exp);  }
        else if (AsignStmt*  as = dynamic_cast<AsignStmt*>(s))       { if (as->exp)  fold(as->exp);  }
        else if (PrintStmt*  ps = dynamic_cast<PrintStmt*>(s))       { if (ps->exp)  fold(ps->exp);  }
        else if (ReturnStm*  rs = dynamic_cast<ReturnStm*>(s))       { if (rs->exp)  fold(rs->exp);  }
        else if (IfStmt*     is = dynamic_cast<IfStmt*>(s)) {
            fold(is->condicion);
            processBody(is->cuerpodelif);
            if (is->hayelse) processBody(is->cuerpodelelse);
        }
        else if (WhileStmt*  ws = dynamic_cast<WhileStmt*>(s)) {
            fold(ws->condicion);
            for (Stmt* inner : ws->cuerpodelwhile) processStmt(inner);
        }
        else if (ForStmt*    fs = dynamic_cast<ForStmt*>(s)) {
            if (fs->asignacion) processStmt(fs->asignacion);
            if (fs->condicion)  fold(fs->condicion);
            if (fs->incremento) processStmt(fs->incremento);
            processBody(fs->cuerpo);
        }
        else if (SwitchStmt* sw = dynamic_cast<SwitchStmt*>(s)) {
            fold(sw->condicion);
            for (auto& cas : sw->casos) {
                fold(cas.first);
                if (cas.second) processBody(cas.second);
            }
            if (sw->default_caso) processBody(sw->default_caso);
        }
        else if (BodyStmt*   bs = dynamic_cast<BodyStmt*>(s))        { processBody(bs->cuerpo); }
        else if (DerefAssignStmt* da = dynamic_cast<DerefAssignStmt*>(s)) {
            fold(da->rval);
            fold(da->lval);
        }
    }

    // ── fold(Exp*) — retorna true si la expresión es constante en compile-time
    bool fold(Exp* exp) {
        if (!exp) return false;

        // ── Literales enteros ───────────────────────────────────────────────
        if (NumberExpDecimal* ne = dynamic_cast<NumberExpDecimal*>(exp)) {
            constMap[exp] = {true, (double)ne->value};
            return true;
        }
        // ── Literales flotantes ─────────────────────────────────────────────
        if (NumberExpFlotante* nf = dynamic_cast<NumberExpFlotante*>(exp)) {
            constMap[exp] = {true, (double)nf->value};
            return true;
        }
        // ── Booleanos ───────────────────────────────────────────────────────
        if (BoolExp* be = dynamic_cast<BoolExp*>(exp)) {
            constMap[exp] = {true, (be->booleano == "true") ? 1.0 : 0.0};
            return true;
        }
        // ── Char ────────────────────────────────────────────────────────────
        if (CharExp* ce = dynamic_cast<CharExp*>(exp)) {
            constMap[exp] = {true, (double)(unsigned char)ce->valor};
            return true;
        }
        // ── Null / Undefined ────────────────────────────────────────────────
        if (dynamic_cast<NullExp*>(exp) || dynamic_cast<UndefinedExp*>(exp)) {
            constMap[exp] = {true, 0.0};
            return true;
        }

        // ── BinaryExp — fold recursivo ───────────────────────────────────────
        if (BinaryExp* bin = dynamic_cast<BinaryExp*>(exp)) {
            bool lC = fold(bin->left);
            bool rC = fold(bin->right);

            if (lC && rC) {
                double L = constMap[bin->left].value;
                double R = constMap[bin->right].value;
                double result = 0.0;
                bool   valid  = true;

                switch (bin->op) {
                    case PLUS_OP:      result = L + R;  break;
                    case MINUS_OP:     result = L - R;  break;
                    case MUL_OP:       result = L * R;  break;
                    case DIV_OP:
                        if (R == 0.0) { valid = false; break; }
                        result = (double)((long long)L / (long long)R);
                        break;
                    case MODULO_OP:
                        if ((long long)R == 0) { valid = false; break; }
                        result = (double)((long long)L % (long long)R);
                        break;
                    case MENOR:        result = (L <  R) ? 1.0 : 0.0; break;
                    case MENORI:       result = (L <= R) ? 1.0 : 0.0; break;
                    case MAYOR:        result = (L >  R) ? 1.0 : 0.0; break;
                    case MAYORI:       result = (L >= R) ? 1.0 : 0.0; break;
                    case IGUALIGUAL:   result = (L == R) ? 1.0 : 0.0; break;
                    case DIFERENTE_OP: result = (L != R) ? 1.0 : 0.0; break;
                    case AND:          result = (L && R) ? 1.0 : 0.0; break;
                    case OR:           result = (L || R) ? 1.0 : 0.0; break;
                    default:           valid = false;                   break;
                }

                if (valid) {
                    constMap[exp] = {true, result};
                    return true;
                }
            }
            constMap[exp] = {false, 0.0};
            return false;
        }

        // ── UnaryExp::NEGATE de constante ───────────────────────────────────
        if (UnaryExp* ue = dynamic_cast<UnaryExp*>(exp)) {
            if (ue->op == UnaryExp::NEGATE && fold(ue->exp)) {
                constMap[exp] = {true, -constMap[ue->exp].value};
                return true;
            }
            if (ue->exp) fold(ue->exp);
            constMap[exp] = {false, 0.0};
            return false;
        }

        // ── NotExp de constante ─────────────────────────────────────────────
        if (NotExp* ne = dynamic_cast<NotExp*>(exp)) {
            if (fold(ne->exp)) {
                constMap[exp] = {true, (constMap[ne->exp].value == 0.0) ? 1.0 : 0.0};
                return true;
            }
            constMap[exp] = {false, 0.0};
            return false;
        }

        // ── FcallExp — recorrer argumentos aunque no sea constante ──────────
        if (FcallExp* fc = dynamic_cast<FcallExp*>(exp)) {
            for (Exp* arg : fc->argumentos) fold(arg);
            constMap[exp] = {false, 0.0};
            return false;
        }

        // ── PuntoExp, AlgoconcorchetesExp, etc. ─────────────────────────────
        if (PuntoExp* pe = dynamic_cast<PuntoExp*>(exp)) {
            fold(pe->exp);
            constMap[exp] = {false, 0.0};
            return false;
        }
        if (AlgoconcorchetesExp* ae = dynamic_cast<AlgoconcorchetesExp*>(exp)) {
            fold(ae->nombre);
            fold(ae->dentroexp);
            constMap[exp] = {false, 0.0};
            return false;
        }

        // Por defecto: no constante
        constMap[exp] = {false, 0.0};
        return false;
    }
};

#endif // CONSTANT_FOLDING_VISITOR_H