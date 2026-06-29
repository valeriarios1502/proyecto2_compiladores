#ifndef DEAD_CODE_ELIMINATION_VISITOR_H
#define DEAD_CODE_ELIMINATION_VISITOR_H

// =============================================================================
// DeadCodeEliminationVisitor.h  — versión segura (no muta nodos del AST)
// =============================================================================
// REGLAS IMPLEMENTADAS:
//   1. Código inalcanzable tras return/break/continue (Efecto Cascada)
//   2. Ramas muertas en if/else con condición constante conocida
//   3. while/for con condición false constante → eliminar bucle
//   4. BodyStmt vacío → eliminar
//
// INVARIANTE DE SEGURIDAD:
//   El DCE NUNCA crea nodos nuevos ni pone nullptr en campos de nodos
//   existentes. Solo elimina punteros de listas (slist). Así el árbol
//   original permanece válido para delete al final del programa.
// =============================================================================

#include "ast.h"
#include "ConstantFoldingVisitor.h"
#include <list>

class DeadCodeEliminationPass {
public:
    int stmtsRemoved   = 0;
    int branchesKilled = 0;
    int loopsKilled    = 0;

    void run(Programa* prog, const ConstMap& cm) {
        if (!prog) return;
        constMap = &cm;
        for (Top_dec* d : prog->declist) {
            if (Fundec*   fd = dynamic_cast<Fundec*>(d))   processBody(fd->cuerpo);
            if (Template* t  = dynamic_cast<Template*>(d)) processBody(t->block);
        }
    }

private:
    const ConstMap* constMap = nullptr;

    // ── Consultas al ConstMap ─────────────────────────────────────────────────
    bool isConstTrue(Exp* e) const {
        if (!e || !constMap) return false;
        auto it = constMap->find(e);
        return it != constMap->end() && it->second.isConst && it->second.value != 0.0;
    }
    bool isConstFalse(Exp* e) const {
        if (!e || !constMap) return false;
        auto it = constMap->find(e);
        return it != constMap->end() && it->second.isConst && it->second.value == 0.0;
    }
    bool isConst(Exp* e) const {
        if (!e || !constMap) return false;
        auto it = constMap->find(e);
        return it != constMap->end() && it->second.isConst;
    }

    // ── ¿Transfiere control incondicionalmente? ───────────────────────────────
    bool isUnconditionalJump(Stmt* s) const {
        return dynamic_cast<ReturnStm*>(s)   != nullptr
            || dynamic_cast<BreakStmt*>(s)   != nullptr
            || dynamic_cast<ContinueStm*>(s) != nullptr;
    }

    // =========================================================================
    // processBody — filtra la lista de sentencias in-place
    // =========================================================================
    void processBody(Body* b) {
        if (!b) return;

        std::list<Stmt*>& stmts = b->slist;
        std::list<Stmt*>  result;

        for (Stmt* s : stmts) {
            // ── Decidir si conservar esta sentencia ──────────────────────────
            if (!keepStmt(s)) {
                ++stmtsRemoved;
                continue;
            }

            // ── Optimizar recursivamente el interior ─────────────────────────
            recurse(s);
            result.push_back(s);

            // ── Efecto cascada: si transfiere control, el resto es muerto ────
            if (isUnconditionalJump(s)) {
                // contar las que quedan
                bool past = false;
                for (Stmt* dead : stmts) {
                    if (dead == s) { past = true; continue; }
                    if (past) ++stmtsRemoved;
                }
                break;
            }
        }

        stmts = std::move(result);
    }

    // =========================================================================
    // keepStmt — decide si una sentencia debe conservarse (sin mutarla)
    // =========================================================================
    bool keepStmt(Stmt* s) {
        if (!s) return false;

        // ── while(false) → eliminar ───────────────────────────────────────────
        if (WhileStmt* ws = dynamic_cast<WhileStmt*>(s)) {
            if (isConstFalse(ws->condicion)) {
                ++loopsKilled;
                return false;
            }
            return true;
        }

        // ── for(; false ;) → eliminar ─────────────────────────────────────────
        if (ForStmt* fs = dynamic_cast<ForStmt*>(s)) {
            // NullExp en condicion = for-each, nunca eliminar
            if (fs->condicion && !dynamic_cast<NullExp*>(fs->condicion)
                && isConstFalse(fs->condicion)) {
                ++loopsKilled;
                return false;
            }
            return true;
        }

        // ── if(false) sin else → eliminar ─────────────────────────────────────
        if (IfStmt* is = dynamic_cast<IfStmt*>(s)) {
            if (isConstFalse(is->condicion) && !is->hayelse) {
                ++branchesKilled;
                return false;
            }
            return true;
        }

        // ── BodyStmt con cuerpo vacío → eliminar ──────────────────────────────
        if (BodyStmt* bs = dynamic_cast<BodyStmt*>(s)) {
            if (!bs->cuerpo || bs->cuerpo->slist.empty()) {
                ++stmtsRemoved;
                return false;
            }
            return true;
        }

        return true;
    }

    // =========================================================================
    // recurse — optimiza el interior de una sentencia que ya decidimos conservar
    // NO reemplaza ni elimina el nodo raíz, solo poda sus hijos.
    // =========================================================================
    void recurse(Stmt* s) {
        if (!s) return;

        if (IfStmt* is = dynamic_cast<IfStmt*>(s)) {
            // Optimizar rama then siempre
            processBody(is->cuerpodelif);
            // Optimizar rama else si existe
            if (is->hayelse && is->cuerpodelelse)
                processBody(is->cuerpodelelse);
            // Si condición es constante, vaciar la rama muerta
            // (la rama viva ya fue optimizada; no tocamos punteros del nodo)
            if (isConstTrue(is->condicion) && is->hayelse) {
                // Vaciar el else (queda el body con lista vacía, no nullptr)
                if (is->cuerpodelelse) is->cuerpodelelse->slist.clear();
                ++branchesKilled;
            } else if (isConstFalse(is->condicion) && is->hayelse) {
                // Vaciar el then
                if (is->cuerpodelif) is->cuerpodelif->slist.clear();
                ++branchesKilled;
            }
            return;
        }

        if (WhileStmt* ws = dynamic_cast<WhileStmt*>(s)) {
            // Operar directamente sobre la lista del while para evitar
            // que el destructor de un Body temporal libere punteros válidos.
            std::list<Stmt*> result;
            for (Stmt* inner : ws->cuerpodelwhile) {
                if (!keepStmt(inner)) { ++stmtsRemoved; continue; }
                recurse(inner);
                result.push_back(inner);
                if (isUnconditionalJump(inner)) { break; }
            }
            ws->cuerpodelwhile = std::move(result);
            return;
        }

        if (ForStmt* fs = dynamic_cast<ForStmt*>(s)) {
            processBody(fs->cuerpo);
            return;
        }

        if (SwitchStmt* sw = dynamic_cast<SwitchStmt*>(s)) {
            for (auto& cas : sw->casos)
                processBody(cas.second);
            processBody(sw->default_caso);
            return;
        }

        if (BodyStmt* bs = dynamic_cast<BodyStmt*>(s)) {
            processBody(bs->cuerpo);
            return;
        }

        if (TryStmt* ts = dynamic_cast<TryStmt*>(s)) {
            processBody(ts->try_body);
            processBody(ts->catch_body);
            return;
        }

        if (DeferStmt* ds = dynamic_cast<DeferStmt*>(s)) {
            // No recursamos en defer — es una sola sentencia
            return;
        }
    }
};

#endif // DEAD_CODE_ELIMINATION_VISITOR_H