#ifndef TYPECHECKER_H
#define TYPECHECKER_H

#include "visitor.h"
#include "ast.h"
#include "environment.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>

struct FunInfo {
    std::string returnType;             
    std::vector<std::string> paramTypes;
    std::vector<std::string> paramNames; 
};

class TypeCheckerVisitor : public Visitor {

public:

    std::string currentType;
    std::string expectedReturnType;
    bool hasReturn = false;
    std::string currentFunction;
    Environment<std::string> env;
    std::unordered_map<std::string, FunInfo> funEnv;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> structEnv;

    void typecheck(Programa* programa);

    // exp
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

    //stm
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

    //decl
    void visit(Fundec* fd) override;
    void visit(Structdec* sd) override;
    void visit(VarDec* vd) override;
    void visit(ConstDec* cd) override;
    void visit(Template* t) override;

    //type
    void visit(IdType* tipo) override;
    void visit(PointerType* tipo) override;
    void visit(ArrayType* tipo) override;
    void visit(OptionalType* tipo) override;
    void visit(ErrorType* tipo) override;
    void visit(UnionType* tipo) override;
    void visit(EnumType* tipo) override;

    void visit(Body* b) override;
    void visit(Programa* p) override;

private:

    std::string inferType(Exp* exp) {
        exp->accept(this);
        return currentType;
    }
    
    bool soncompatibles(const std::string& esperado, const std::string& dado) const {
        if (esperado == dado) return true;

        if (dado == "null" || dado == "undefined") {
            if (!esperado.empty() && (esperado[0] == '*' || esperado[0] == '?'))
                return true;
        }

        if (esperado == "float" && dado == "int"){
            return true;
        }

        return false;
    }

    bool isNumeric(const std::string& t) const {
        return t == "int" || t == "float";
    }

    bool isBool(const std::string& t) const {
        return t == "bool";
    }

    bool isPointer(const std::string& t) const {
        return !t.empty() && t[0] == '*';
    }

    bool isOptional(const std::string& t) const {
        return !t.empty() && t[0] == '?';
    }

    void registerFunctions(Programa* p);

    void registerStructs(Programa* p);
};

#endif // TYPECHECKER_H
