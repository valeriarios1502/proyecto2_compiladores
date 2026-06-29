#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "scanner.h"
#include "parser.h"
#include "ast.h"
#include "visitor.h"

using namespace std;

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        cout << "Número incorrecto de argumentos.\n";
        cout << "Uso: " << argv[0] << " <archivo_de_entrada>" << endl;
        return 1;
    }

    ifstream infile(argv[1]);
    if (!infile.is_open()) {
        cout << "No se pudo abrir el archivo: " << argv[1] << endl;
        return 1;
    }

    string input, line;
    while (getline(infile, line)) {
        input += line + '\n';
    }
    infile.close();

    Scanner scanner1(input.c_str());
    Scanner scanner2(input.c_str());

    ejecutar_scanner(&scanner1, argv[1]);

    Parser parser(&scanner2);
    Programa* ast = nullptr;

    try {
        ast = parser.parseProgram();

        // Pase 1: constant folding
        ConstantFolding opt1;
        opt1.visit(ast);

        // Pase 2: Cascada (propagación + simplificación algebraica)
        Cascada cascada;
        cascada.optimize(ast);

        // Pase 3: Sethi-Ullman (análisis de registros)
        SethiUlman opt2;
        opt2.visit(ast);

        // Pase 4: GenCode → capturar en buffer para Peephole
        ostringstream buffer;
        streambuf* old = cout.rdbuf(buffer.rdbuf());  // redirigir cout

        GenCodeVisitor eval;
        eval.gencode(ast);

        cout.rdbuf(old);  // restaurar cout

        // Pase 5: Peephole sobre las líneas generadas
        vector<string> lines;
        istringstream ss(buffer.str());
        string ln;
        while (getline(ss, ln))
            lines.push_back(ln);

        Peephole ph;
        vector<string> optimized = ph.optimize(lines);

        // Emitir assembly final
        for (const string& l : optimized)
            cout << l << "\n";
    }
    catch (const exception& e) {
        cerr << "Error al parsear: " << e.what() << endl;
        ast = nullptr;
    }

    delete ast;
    return 0;
}