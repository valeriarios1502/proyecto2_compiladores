#ifndef VISITOR_H
#define VISITOR_H

#include "ast.h"
#include <list>
#include <unordered_map>
#include "environment.h"

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


class Visitor {
public:
    virtual ~Visitor() = default;

    virtual Value visit(BinaryExp* exp) = 0;
    virtual Value visit(NumberExpDecimal* exp) = 0;
    virtual Value visit(NumberExpFlotante* exp) = 0;
    virtual Value visit(StringExp* exp) = 0;
    virtual Value visit(CharExp* exp) = 0;
    virtual Value visit(IdExp* exp) = 0;
    virtual Value visit(BoolExp* exp) = 0;
    virtual Value visit(NotExp* exp) = 0;
    virtual Value visit(FcallExp* exp) = 0;
    virtual Value visit(UnaryExp* exp) = 0;
    virtual Value visit(NewExp* exp) = 0;
    virtual Value visit(NullExp* exp) = 0;
    virtual Value visit(UndefinedExp* exp) = 0;
    virtual Value visit(ReferenceExp* exp) = 0;
    virtual Value visit(PunteroExp* exp) = 0;
    virtual Value visit(AlgoconcorchetesylistaExp* exp) = 0;
    virtual Value visit(AlgoconcorchetesExp* exp) = 0;
    virtual Value visit(PuntoExp* exp) = 0;
    virtual Value visit(LambdaExp* exp) = 0;

    virtual void visit(IfStmt* stm) = 0;
    virtual void visit(WhileStmt* stm) = 0;
    virtual void visit(BodyStmt* stm) = 0;
    virtual void visit(AsignStmt* stm) = 0;
    virtual void visit(PrintStmt* stm) = 0;
    virtual void visit(ReturnStm* stm) = 0;
    virtual void visit(DeleteStm* stm) = 0;
    virtual void visit(ContinueStm* stm) = 0;
    virtual void visit(BreakStmt* stm) = 0;
    virtual void visit(SwitchStmt* stm) = 0;
    virtual void visit(TryStmt* stm) = 0;
    virtual void visit(DeferStmt* stm) = 0;
    virtual void visit(ForStmt* stm) = 0;

    virtual void visit(Fundec* fd) = 0;
    virtual void visit(Structdec* sd) = 0;
    virtual void visit(VarDec* vd) = 0;
    virtual void visit(ConstDec* cd) = 0;
    virtual void visit(Template* t) = 0;

    virtual void visit(IdType* tipo) = 0;
    virtual void visit(PointerType* tipo) = 0;
    virtual void visit(ArrayType* tipo) = 0;
    virtual void visit(OptionalType* tipo) = 0;
    virtual void visit(ErrorType* tipo) = 0;
    virtual void visit(UnionType* tipo) = 0;
    virtual void visit(EnumType* tipo) = 0;

    virtual void visit(Body* b) = 0;
    virtual void visit(Programa* p) = 0;
};

class EVALVisitor : public Visitor {
public:
    Environment<Value> env;
    unordered_map<string, Fundec*> envfun;
    Value retval;
    bool retcall = false;
    bool breakcall = false;
    bool continuecall = false;

    // Exp
    Value visit(BinaryExp* exp) override;
    Value visit(NumberExpDecimal* exp) override;
    Value visit(NumberExpFlotante* exp) override;
    Value visit(StringExp* exp) override;
    Value visit(CharExp* exp) override;
    Value visit(IdExp* exp) override;
    Value visit(BoolExp* exp) override;
    Value visit(NotExp* exp) override;
    Value visit(FcallExp* exp) override;
    Value visit(UnaryExp* exp) override;
    Value visit(NewExp* exp) override;
    Value visit(NullExp* exp) override;
    Value visit(UndefinedExp* exp) override;
    Value visit(ReferenceExp* exp) override;
    Value visit(PunteroExp* exp) override;
    Value visit(AlgoconcorchetesylistaExp* exp) override;
    Value visit(AlgoconcorchetesExp* exp) override;
    Value visit(PuntoExp* exp) override;
    Value visit(LambdaExp* exp) override;

    // Stmt
    void visit(IfStmt* stm) override;
    void visit(WhileStmt* stm) override;
    void visit(BodyStmt* stm) override;
    void visit(AsignStmt* stm) override;
    void visit(PrintStmt* stm) override;
    void visit(ReturnStm* stm) override;
    void visit(DeleteStm* stm) override;
    void visit(ContinueStm* stm) override;
    void visit(BreakStmt* stm) override;
    void visit(SwitchStmt* stm) override;
    void visit(TryStmt* stm) override;
    void visit(DeferStmt* stm) override;
    void visit(ForStmt* stm) override;

    // Top_dec
    void visit(Fundec* fd) override;
    void visit(Structdec* sd) override;
    void visit(VarDec* vd) override;
    void visit(ConstDec* cd) override;
    void visit(Template* t) override;

    // Type
    void visit(IdType* tipo) override;
    void visit(PointerType* tipo) override;
    void visit(ArrayType* tipo) override;
    void visit(OptionalType* tipo) override;
    void visit(ErrorType* tipo) override;
    void visit(UnionType* tipo) override;
    void visit(EnumType* tipo) override;

    // Body / Programa
    void visit(Body* b) override;
    void visit(Programa* p) override;

    void interprete(Programa* programa);
};

#endif // VISITOR_H

