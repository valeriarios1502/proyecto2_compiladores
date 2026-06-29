#ifndef AST_H
#define AST_H

#include <string>
#include <unordered_map>
#include <list>
#include <ostream>
#include <vector>
#include <utility>
using namespace std;

class Visitor;
class Body;
class VarDec;
class Type;

struct Value {
    enum Kind { VAL_INT, VAL_FLOAT, VAL_BOOL, VAL_VOID, VAL_STRING, VAL_CHAR, VAL_NULL, VAL_UNDEFINED } kind;

    int i;
    double f;
    bool b;
    string s;
    char c;

    Value() : kind(VAL_VOID), i(0), f(0.0), b(false), c('\0') {}

    static Value makeInt(int v) {
        Value val;
        val.kind = VAL_INT;
        val.i = v;
        return val;
    }

    static Value makeFloat(double v) {
        Value val;
        val.kind = VAL_FLOAT;
        val.f = v;
        return val;
    }

    static Value makeBool(bool v) {
        Value val;
        val.kind = VAL_BOOL;
        val.b = v;
        return val;
    }

    static Value makeString(const string &v) {
        Value val;
        val.kind = VAL_STRING;
        val.s = v;
        return val;
    }

    static Value makeChar(char v) {
        Value val;
        val.kind = VAL_CHAR;
        val.c = v;
        return val;
    }

    static Value makeNull() {
        Value val;
        val.kind = VAL_NULL;
        return val;
    }

    static Value makeUndefined() {
        Value val;
        val.kind = VAL_UNDEFINED;
        return val;
    }

    bool isNumeric() const {
        return kind == VAL_INT || kind == VAL_FLOAT;
    }

    bool isBool() const {
        return kind == VAL_BOOL;
    }

    double asNumber() const {
        return kind == VAL_FLOAT ? f : i;
    }

    string toString() const {
        switch (kind) {
        case VAL_INT:
            return to_string(i);
        case VAL_FLOAT: {
            string text = to_string(f);
            if (text.find('.') != string::npos) {
                while (!text.empty() && text.back() == '0')
                    text.pop_back();
                if (!text.empty() && text.back() == '.')
                    text.pop_back();
            }
            return text;
        }
        case VAL_BOOL:
            return b ? "true" : "false";
        case VAL_STRING:
            return s;
        case VAL_CHAR:
            return string(1, c);
        case VAL_NULL:
            return "null";
        case VAL_UNDEFINED:
            return "undefined";
        case VAL_VOID:
            return "void";
        }
        return "void";
    }
};

// Operadores binarios soportados
enum BinaryOp { 
    PLUS_OP, 
    MINUS_OP,
    MODULO_OP,
    MUL_OP, 
    DIV_OP,
    DIFERENTE_OP,
    MENORI,
    MENOR,
    MAYORI,
    MAYOR,
    IGUALIGUAL,
    REFERENCIA,
    NOT,
    AND,
    OR
};

// ==================== Exp ====================
// Todas las subclases de Exp deben retornar Value en accept()

class Exp {
public:
    virtual Value accept(Visitor* visitor) = 0;
    virtual ~Exp() = 0;
    static string binopToChar(BinaryOp op);
};

class BinaryExp : public Exp {
public:
    Exp* left;
    Exp* right;
    BinaryOp op;
    Value accept(Visitor* visitor);
    BinaryExp(Exp* l, Exp* r, BinaryOp op);
    ~BinaryExp();
};

class NumberExpDecimal : public Exp {
public:
    int value;
    Value accept(Visitor* visitor);
    NumberExpDecimal(int v);
    ~NumberExpDecimal();
};

class NumberExpFlotante : public Exp {
public:
    float value;
    Value accept(Visitor* visitor);
    NumberExpFlotante(float v);
    ~NumberExpFlotante();
};

class StringExp : public Exp {
public:
    string valor;
    Value accept(Visitor* visitor);
    StringExp(string v);
    ~StringExp();
};

class CharExp : public Exp {
public:
    char valor;
    Value accept(Visitor* visitor);
    CharExp(char v);
    ~CharExp();
};

class IdExp : public Exp {
public:
    string value;
    Value accept(Visitor* visitor);
    IdExp(string v);
    ~IdExp();
};

class BoolExp : public Exp {
public:
    string booleano;
    Value accept(Visitor* visitor);
    BoolExp();
    ~BoolExp();
};

class NotExp : public Exp {
public:
    Exp* exp;
    Value accept(Visitor* visitor);
    NotExp();
    ~NotExp();
};

class FcallExp : public Exp {
public:
    string nombre;
    vector<Exp*> argumentos;
    Value accept(Visitor* visitor);
    FcallExp();
    ~FcallExp();
};

class UnaryExp : public Exp {
public:
    enum Op { NEGATE, ADDRESS, DEREF, NOT_OP } op;
    Exp* exp;
    Value accept(Visitor* visitor);
    UnaryExp(Op op, Exp* exp);
    ~UnaryExp();
};

class NewExp : public Exp {
public:
    Type* tipo;
    Value accept(Visitor* visitor);
    NewExp(Type* tipo);
    ~NewExp();
};

class NullExp : public Exp {
public:
    Value accept(Visitor* visitor);
    NullExp();
    ~NullExp();
};

class UndefinedExp : public Exp {
public:
    Value accept(Visitor* visitor);
    UndefinedExp();
    ~UndefinedExp();
};

class ReferenceExp : public Exp {
public:
    Exp* exp;
    Value accept(Visitor* visitor);
    ReferenceExp();
    ~ReferenceExp();
};

class PunteroExp : public Exp {
public:
    Exp* exp;
    Value accept(Visitor* visitor);
    PunteroExp();
    ~PunteroExp();
};

class AlgoconcorchetesylistaExp : public Exp {
public:
    Exp* nombre;
    vector<Exp*> argumentos;
    Value accept(Visitor* visitor);
    AlgoconcorchetesylistaExp(Exp* nombre, vector<Exp*> argumentos);
    ~AlgoconcorchetesylistaExp();
};

class AlgoconcorchetesExp : public Exp {
public:
    Exp* nombre;
    Exp* dentroexp;
    Value accept(Visitor* visitor);
    AlgoconcorchetesExp(Exp* nombre, Exp* dentroexp);
    ~AlgoconcorchetesExp();
};

class PuntoExp : public Exp {
public:
    Exp* exp;
    string id;
    Value accept(Visitor* visitor);
    PuntoExp(Exp* exp, string id);
    ~PuntoExp();
};

class LambdaExp : public Exp {
public:
    Type* tipo;
    Body* cuerpo;
    vector<string> id_parametros;
    vector<Type*> tipo_parametros;
    bool hayparametros;
    Value accept(Visitor* visitor);
    LambdaExp(Type* tipo, Body* cuerpo, vector<string> id_parametros, vector<Type*> tipo_parametros);
    ~LambdaExp();
};

// ==================== Stmt ====================
// Todas las subclases de Stmt deben retornar void en accept()

class Stmt {
public:
    virtual void accept(Visitor* visitor) = 0;
    virtual ~Stmt() = 0;
};

class IfStmt : public Stmt {
public:
    Exp* condicion;
    Body* cuerpodelif;
    Body* cuerpodelelse;
    bool hayelse = false;
    void accept(Visitor* visitor) override;
    IfStmt(Exp* condi, Body* ifcuerpo, Body* elsecuerpo, bool hayelse);
    ~IfStmt();
};

class WhileStmt : public Stmt {
public:
    Exp* condicion;
    list<Stmt*> cuerpodelwhile;
    void accept(Visitor* visitor) override;
    WhileStmt(Exp* e, list<Stmt*> cuerpo);
    ~WhileStmt();
};

class BodyStmt : public Stmt {
public:
    Body* cuerpo;
    void accept(Visitor* visitor) override;
    BodyStmt(Body* cuerpo);
    ~BodyStmt();
};

class AsignStmt : public Stmt {
public:
    string variable;
    Exp* exp;
    void accept(Visitor* visitor) override;
    AsignStmt(string, Exp*);
    ~AsignStmt();
};

class PrintStmt : public Stmt {
public:
    Exp* exp;
    void accept(Visitor* visitor) override;
    PrintStmt(Exp* e);
    ~PrintStmt();
};

class ReturnStm : public Stmt {
public:
    Exp* exp;
    void accept(Visitor* visitor);
    ReturnStm(Exp* e);
    ReturnStm();
    ~ReturnStm();
};

class DeleteStm : public Stmt {
public:
    Exp* exp;
    void accept(Visitor* visitor);
    DeleteStm(Exp* e);
    ~DeleteStm();
};

class ContinueStm : public Stmt {
public:
    string c;
    void accept(Visitor* visitor);
    ContinueStm();
    ~ContinueStm();
};

class BreakStmt : public Stmt {
public:
    Exp* valor;
    bool tiene_valor;
    void accept(Visitor* visitor);
    BreakStmt();
    BreakStmt(Exp* valor);
    ~BreakStmt();
};

class SwitchStmt : public Stmt {
public:
    Exp* condicion;
    vector<pair<Exp*, Body*>> casos;
    Body* default_caso;
    void accept(Visitor* visitor);
    SwitchStmt(Exp* condicion);
    ~SwitchStmt();
};

class TryStmt : public Stmt {
public:
    Exp* expr;
    Body* try_body;
    Body* catch_body;
    string error_var;
    void accept(Visitor* visitor);
    TryStmt(Exp* expr, Body* try_body, Body* catch_body, string error_var);
    ~TryStmt();
};

class DeferStmt : public Stmt {
public:
    Stmt* stmt;
    void accept(Visitor* visitor);
    DeferStmt(Stmt* stmt);
    ~DeferStmt();
};

class ForStmt : public Stmt {
public:
    Stmt* asignacion;
    Exp* condicion;
    Stmt* incremento;
    Body* cuerpo;
    void accept(Visitor* visitor);
    ForStmt(Stmt* asignacion, Exp* condicion, Stmt* incremento, Body* cuerpo);
    ~ForStmt();
};

struct DerefAssignStmt : Stmt {
    Exp* lval;
    Exp* rval;
    DerefAssignStmt(Exp* l, Exp* r) : lval(l), rval(r) {}
    void accept(Visitor* v) override; 
};

// ==================== Top_dec ====================
// Todas las subclases de Top_dec deben retornar void en accept()

class Top_dec {
public:
    virtual void accept(Visitor* visitor) = 0;
    virtual ~Top_dec() = 0;
};

class Fundec : public Top_dec {
public:
    string nombre;
    Type* tipo;
    vector<Type*> tipo_parametros;
    vector<string> id_parametros;
    Body* cuerpo;
    void accept(Visitor* visitor);
    Fundec();
    ~Fundec();
};

class Structdec : public Top_dec {
public:
    string nombre;
    vector<string> id_parametros;
    vector<Type*> tipo_parametros;
    void accept(Visitor* visitor);
    Structdec();
    ~Structdec();
};

class VarDec : public Top_dec {
public:
    string nombre;
    Type* tipo;
    Exp* exp;
    bool tienetipo;
    void accept(Visitor* visitor);
    VarDec();
    ~VarDec();
};

class ConstDec : public Top_dec {
public:
    string nombre;
    Type* tipo;
    Exp* exp;
    bool tienetipo;
    void accept(Visitor* visitor);
    ConstDec();
    ~ConstDec();
};

class Template : public Top_dec {
public:
    string id1;
    string id2;
    vector<string> id_parametros;
    vector<Type*> tipo_parametros;
    Type* tipo;
    Body* block;
    void accept(Visitor* visitor);
    Template();
    ~Template();
};

// ==================== Type ====================
// Todas las subclases de Type deben retornar void en accept()

class Type {
public:
    virtual void accept(Visitor* visitor) = 0;
    virtual ~Type() = 0;
};

class IdType : public Type {
public:
    string id;
    void accept(Visitor* visitor);
    IdType(string id);
    ~IdType();
};

class PointerType : public Type {
public:
    Type* tipo;
    void accept(Visitor* visitor);
    PointerType(Type* tipo);
    ~PointerType();
};

class ArrayType : public Type {
public:
    Exp* exp1;
    Exp* exp2;
    Type* tipo;
    bool existe_exp2;
    void accept(Visitor* visitor);
    ArrayType(Exp* exp1, Exp* exp2, Type* tipo, bool existe_exp2);
    ~ArrayType();
};

class OptionalType : public Type {
public:
    Type* tipo;
    void accept(Visitor* visitor);
    OptionalType(Type* tipo);
    ~OptionalType();
};

class ErrorType : public Type {
public:
    Type* tipo;
    void accept(Visitor* visitor);
    ErrorType(Type* tipo);
    ~ErrorType();
};

class UnionType : public Type {
public:
    string nombre;
    vector<string> campo_nombres;
    vector<Type*> campo_tipos;
    void accept(Visitor* visitor);
    UnionType(string nombre);
    ~UnionType();
};

class EnumType : public Type {
public:
    string nombre;
    vector<string> valores;
    void accept(Visitor* visitor);
    EnumType(string nombre);
    ~EnumType();
};

// ==================== Body / Programa ====================

class Body {
public:
    list<Stmt*> slist;
    void accept(Visitor* visitor);
    Body();
    ~Body();
};

class Programa {
public:
    list<Top_dec*> declist;
    void accept(Visitor* visitor);
    ~Programa();
    Programa();
};

#endif // AST_H