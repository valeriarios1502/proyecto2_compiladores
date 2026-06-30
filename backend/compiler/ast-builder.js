const BINARY_PRECEDENCE = new Map([
  ["OR", 1],
  ["AND", 2],
  ["IGUALIGUAL", 3],
  ["DIFERENTE", 3],
  ["MENOR", 4],
  ["MENORIGUAL", 4],
  ["MAYOR", 4],
  ["MAYORIGUAL", 4],
  ["PLUS", 5],
  ["MINUS", 5],
  ["STAR", 6],
  ["DIV", 6],
  ["MODULO", 6],
])

const BINARY_OPERATOR = new Map([
  ["OR", "||"],
  ["AND", "&&"],
  ["IGUALIGUAL", "=="],
  ["DIFERENTE", "!="],
  ["MENOR", "<"],
  ["MENORIGUAL", "<="],
  ["MAYOR", ">"],
  ["MAYORIGUAL", ">="],
  ["PLUS", "+"],
  ["MINUS", "-"],
  ["STAR", "*"],
  ["DIV", "/"],
  ["MODULO", "%"],
])

const STATEMENT_STARTERS = new Set([
  "VAR",
  "CONST",
  "RETURN",
  "PRINT",
  "FREE",
  "DEFER",
  "BREAK",
  "CONTINUE",
  "IF",
  "WHILE",
  "FOR",
  "SWITCH",
  "ID",
  "STAR",
])

function buildAst(tokens) {
  const parser = new AstParser(tokens)
  return parser.parseProgram()
}

class AstParser {
  constructor(tokens) {
    this.tokens = tokens.filter((token) => token.type !== "END")
    this.current = 0
  }

  parseProgram() {
    const declarations = []

    while (!this.isAtEnd()) {
      declarations.push(this.parseTopLevelDeclaration())
    }

    return {
      type: "Program",
      declarations,
      summary: {
        declarations: declarations.length,
        functions: declarations.filter((node) => node.type === "FunctionDeclaration").length,
        structs: declarations.filter((node) => node.type === "StructDeclaration").length,
        globals: declarations.filter((node) => ["VariableDeclaration", "ConstDeclaration"].includes(node.type)).length,
      },
    }
  }

  parseTopLevelDeclaration() {
    if (this.match("VAR")) return this.parseVariableDeclaration("VariableDeclaration", true)
    if (this.match("CONST")) return this.parseVariableDeclaration("ConstDeclaration", true)
    if (this.match("STRUCT")) return this.parseStructDeclaration()
    if (this.match("UNION")) return this.parseUnionDeclaration()
    if (this.match("COMPTIME")) {
      return {
        type: "ComptimeBlock",
        body: this.parseBlock(),
      }
    }
    if (this.match("PUB")) {
      this.consume("FN")
      return this.parseFunctionDeclaration({ public: true })
    }
    if (this.match("FN")) {
      return this.parseFunctionDeclaration({ public: false })
    }

    return this.parseUnknown("TopLevelDeclaration")
  }

  parseFunctionDeclaration(meta) {
    const name = this.consume("ID")?.lexeme || "<anonymous>"
    const template = this.match("MENOR") ? this.parseTemplateSuffix() : null

    this.consume("LPAREN")
    const params = this.parseParams()
    this.consume("RPAREN")

    if (this.match("DOSPUNTOS")) {
      // Some template forms use ':' before the return type.
    }

    const returnType = this.parseTypeUntil(["LBRACE"])
    const body = this.parseBlock()

    return {
      type: template ? "TemplateFunctionDeclaration" : "FunctionDeclaration",
      name,
      public: meta.public,
      template,
      params,
      returnType,
      body,
    }
  }

  parseTemplateSuffix() {
    const parts = []
    let depth = 1

    while (!this.isAtEnd() && depth > 0) {
      if (this.match("MENOR")) {
        depth += 1
        parts.push("<")
        continue
      }
      if (this.match("MAYOR")) {
        depth -= 1
        if (depth > 0) parts.push(">")
        continue
      }
      parts.push(this.advance().lexeme)
    }

    return {
      type: "TemplateParameters",
      text: parts.join(" ").trim(),
    }
  }

  parseParams() {
    const params = []

    while (!this.check("RPAREN") && !this.isAtEnd()) {
      const comptime = this.match("COMPTIME")
      const name = this.consume("ID")?.lexeme || "<param>"
      this.consume("DOSPUNTOS")
      const paramType = this.parseTypeUntil(["COMA", "RPAREN"])
      params.push({ type: "Parameter", name, paramType, comptime })
      this.match("COMA")
    }

    return params
  }

  parseStructDeclaration() {
    const name = this.consume("ID")?.lexeme || "<struct>"
    const fields = []

    this.consume("LBRACE")
    while (!this.check("RBRACE") && !this.isAtEnd()) {
      const fieldName = this.consume("ID")?.lexeme || "<field>"
      this.consume("DOSPUNTOS")
      const fieldType = this.parseTypeUntil(["SEMICOL", "RBRACE"])
      this.match("SEMICOL")
      fields.push({ type: "FieldDeclaration", name: fieldName, fieldType })
    }
    this.consume("RBRACE")

    return { type: "StructDeclaration", name, fields }
  }

  parseUnionDeclaration() {
    const name = this.consume("ID")?.lexeme || "<union>"
    let tag = null

    if (this.match("LPAREN")) {
      tag = this.consume("ID")?.lexeme || null
      this.consume("RPAREN")
    }

    const fields = []
    this.consume("LBRACE")
    while (!this.check("RBRACE") && !this.isAtEnd()) {
      const fieldName = this.consume("ID")?.lexeme || "<field>"
      this.consume("DOSPUNTOS")
      const fieldType = this.parseTypeUntil(["SEMICOL", "RBRACE"])
      this.match("SEMICOL")
      fields.push({ type: "FieldDeclaration", name: fieldName, fieldType })
    }
    this.consume("RBRACE")

    return { type: "UnionDeclaration", name, tag, fields }
  }

  parseBlock() {
    const statements = []
    this.consume("LBRACE")

    while (!this.check("RBRACE") && !this.isAtEnd()) {
      statements.push(this.parseStatement())
    }

    this.consume("RBRACE")
    return { type: "BlockStatement", statements }
  }

  parseStatement() {
    if (this.match("VAR")) return this.parseVariableDeclaration("VariableDeclaration", false)
    if (this.match("CONST")) return this.parseVariableDeclaration("ConstDeclaration", false)
    if (this.match("RETURN")) return this.parseReturnStatement()
    if (this.match("PRINT")) return this.parsePrintStatement()
    if (this.match("FREE")) return this.parseDeleteStatement()
    if (this.match("DEFER")) return { type: "DeferStatement", statement: this.parseStatement() }
    if (this.match("BREAK")) return this.parseBreakStatement()
    if (this.match("CONTINUE")) {
      this.match("SEMICOL")
      return { type: "ContinueStatement" }
    }
    if (this.match("IF")) return this.parseIfStatement()
    if (this.match("WHILE")) return this.parseWhileStatement()
    if (this.match("FOR")) return this.parseForStatement()
    if (this.match("SWITCH")) return this.parseSwitchStatement()
    if (this.check("ID") || this.check("STAR")) return this.parseAssignmentOrCallStatement()

    return this.parseUnknown("Statement")
  }

  parseVariableDeclaration(type, topLevel) {
    const name = this.consume("ID")?.lexeme || "<variable>"
    let valueType = null

    if (this.match("DOSPUNTOS")) {
      valueType = this.parseTypeUntil(["ASSIGN"])
    }

    this.consume("ASSIGN")
    const initializer = this.parseExpression()
    this.match("SEMICOL")

    return {
      type,
      name,
      valueType,
      initializer,
      scope: topLevel ? "global" : "local",
    }
  }

  parseReturnStatement() {
    if (this.check("SEMICOL") || this.check("RBRACE")) {
      this.match("SEMICOL")
      return { type: "ReturnStatement", argument: null }
    }
    const argument = this.parseExpression()
    this.match("SEMICOL")
    return { type: "ReturnStatement", argument }
  }

  parsePrintStatement() {
    this.consume("LPAREN")
    const argument = this.parseExpression()
    this.consume("RPAREN")
    this.match("SEMICOL")
    return { type: "PrintStatement", argument }
  }

  parseDeleteStatement() {
    const argument = this.parseExpression()
    this.match("SEMICOL")
    return { type: "DeleteStatement", argument }
  }

  parseBreakStatement() {
    if (this.match("DOSPUNTOS")) {
      const label = this.match("ID") ? this.previous().lexeme : null
      const value = this.parseExpression()
      this.match("SEMICOL")
      return { type: "BreakStatement", label, value }
    }
    this.match("SEMICOL")
    return { type: "BreakStatement", label: null, value: null }
  }

  parseIfStatement() {
    this.consume("LPAREN")
    const test = this.parseExpression()
    this.consume("RPAREN")
    this.match("THEN")
    const consequent = this.parseBlock()
    const alternate = this.match("ELSE") ? (this.match("THEN"), this.parseBlock()) : null
    return { type: "IfStatement", test, consequent, alternate }
  }

  parseWhileStatement() {
    this.consume("LPAREN")
    const test = this.parseExpression()
    this.consume("RPAREN")
    const body = this.parseBlock()
    return { type: "WhileStatement", test, body }
  }

  parseForStatement() {
    this.consume("LPAREN")
    const iterable = this.parseExpression()
    this.consume("RPAREN")
    this.consume("PIPE")
    const variables = [this.consume("ID")?.lexeme || "<item>"]
    if (this.match("COMA")) variables.push(this.consume("ID")?.lexeme || "<index>")
    this.consume("PIPE")
    const body = this.parseBlock()
    return { type: "ForStatement", iterable, variables, body }
  }

  parseSwitchStatement() {
    this.consume("LPAREN")
    const discriminant = this.parseExpression()
    this.consume("RPAREN")
    this.consume("LBRACE")

    const cases = []
    let defaultCase = null
    while (!this.check("RBRACE") && !this.isAtEnd()) {
      if (this.match("ELSE")) {
        this.consume("ASSIGN")
        this.consume("MAYOR")
        defaultCase = this.parseBlock()
      } else {
        const test = this.parseExpression()
        this.consume("ASSIGN")
        this.consume("MAYOR")
        cases.push({ type: "SwitchCase", test, consequent: this.parseBlock() })
      }
      this.match("COMA")
    }
    this.consume("RBRACE")

    return { type: "SwitchStatement", discriminant, cases, defaultCase }
  }

  parseAssignmentOrCallStatement() {
    const target = this.parseExpression()

    if (target.type === "CallExpression" && this.match("SEMICOL")) {
      return { type: "ExpressionStatement", expression: target }
    }

    if (this.match("ASSIGN")) {
      const value = this.parseExpression()
      this.match("SEMICOL")
      return { type: "AssignmentStatement", target, value }
    }

    this.match("SEMICOL")
    return { type: "ExpressionStatement", expression: target }
  }

  parseExpression(minPrecedence = 1) {
    let left = this.parseUnaryExpression()

    while (!this.isAtEnd()) {
      const precedence = BINARY_PRECEDENCE.get(this.peek().type)
      if (!precedence || precedence < minPrecedence) break

      const operatorToken = this.advance()
      const right = this.parseExpression(precedence + 1)
      left = {
        type: "BinaryExpression",
        operator: BINARY_OPERATOR.get(operatorToken.type) || operatorToken.lexeme,
        left,
        right,
      }
    }

    if (this.match("CATCH")) {
      let errorVar = null
      if (this.match("PIPE")) {
        errorVar = this.match("ID") ? this.previous().lexeme : null
        this.consume("PIPE")
      }
      left = {
        type: "CatchExpression",
        expression: left,
        errorVar,
        handler: this.parseBlock(),
      }
    }

    return left
  }

  parseUnaryExpression() {
    if (this.match("NOT", "REFERENCIA", "STAR", "MINUS", "TRY", "COMPTIME")) {
      const operator = this.previous()
      return {
        type: "UnaryExpression",
        operator: operator.lexeme || operator.type,
        argument: this.parseUnaryExpression(),
      }
    }

    return this.parsePostfixExpression()
  }

  parsePostfixExpression() {
    let expression = this.parsePrimaryExpression()

    while (!this.isAtEnd()) {
      if (this.match("LPAREN")) {
        const args = []
        while (!this.check("RPAREN") && !this.isAtEnd()) {
          args.push(this.parseExpression())
          this.match("COMA")
        }
        this.consume("RPAREN")
        expression = { type: "CallExpression", callee: expression, arguments: args }
        continue
      }

      if (this.match("LCORCHETE")) {
        const args = []
        while (!this.check("RCORCHETE") && !this.isAtEnd()) {
          args.push(this.parseExpression())
          this.match("COMA")
        }
        this.consume("RCORCHETE")
        expression = { type: "IndexExpression", object: expression, arguments: args }
        continue
      }

      if (this.match("PUNTO")) {
        const optional = this.match("QUESTION")
        const property = optional ? "__unwrap__" : this.consume("ID")?.lexeme
        expression = { type: "MemberExpression", object: expression, property, optional }
        continue
      }

      if (this.match("DOTQUESTION")) {
        expression = { type: "MemberExpression", object: expression, property: "__unwrap__", optional: true }
        continue
      }

      break
    }

    return expression
  }

  parsePrimaryExpression() {
    if (this.match("LPAREN")) {
      const expression = this.parseExpression()
      this.consume("RPAREN")
      return { type: "GroupedExpression", expression }
    }

    if (this.match("NUMDECIMAL", "NUMHEX", "NUMBIN")) {
      return { type: "NumericLiteral", value: Number(this.previous().lexeme), raw: this.previous().lexeme }
    }
    if (this.match("NUMFLOTANTE")) {
      return { type: "FloatLiteral", value: Number(this.previous().lexeme), raw: this.previous().lexeme }
    }
    if (this.match("COMILLASDOBLES")) return { type: "StringLiteral", value: this.previous().lexeme }
    if (this.match("COMILLASSIMPLES")) return { type: "CharLiteral", value: this.previous().lexeme }
    if (this.match("TRUE", "FALSE")) return { type: "BooleanLiteral", value: this.previous().type === "TRUE" }
    if (this.match("NULLTOK")) return { type: "NullLiteral" }
    if (this.match("UNDEFINED")) return { type: "UndefinedLiteral" }
    if (this.match("NEW")) return { type: "NewExpression", valueType: this.parseTypeUntil(this.expressionStopTypes()) }
    if (this.match("FN")) return this.parseLambdaExpression()
    if (this.match("ID")) return { type: "Identifier", name: this.previous().lexeme }

    const token = this.advance()
    return { type: "UnknownExpression", token: token ? token.lexeme : "" }
  }

  parseLambdaExpression() {
    this.consume("LPAREN")
    const params = this.parseParams()
    this.consume("RPAREN")
    this.consume("DOSPUNTOS")
    const returnType = this.parseTypeUntil(["LBRACE"])
    const body = this.parseBlock()
    return { type: "LambdaExpression", params, returnType, body }
  }

  parseTypeUntil(stopTypes) {
    const parts = []
    let bracketDepth = 0

    while (!this.isAtEnd()) {
      const token = this.peek()
      if (bracketDepth === 0 && stopTypes.includes(token.type)) break
      if (token.type === "LCORCHETE") bracketDepth += 1
      if (token.type === "RCORCHETE") bracketDepth = Math.max(0, bracketDepth - 1)
      parts.push(this.advance().lexeme || token.type)
    }

    return {
      type: "TypeReference",
      name: parts.join(" ").trim() || "void",
    }
  }

  parseUnknown(type) {
    const tokens = []
    while (!this.isAtEnd() && !this.check("SEMICOL") && !this.check("RBRACE")) {
      tokens.push(this.advance().lexeme)
    }
    this.match("SEMICOL")
    return { type: `Unknown${type}`, raw: tokens.join(" ") }
  }

  expressionStopTypes() {
    return ["COMA", "SEMICOL", "RPAREN", "RBRACE", "RCORCHETE", "THEN"]
  }

  match(...types) {
    if (!types.includes(this.peek()?.type)) return false
    this.advance()
    return true
  }

  consume(type) {
    if (this.check(type)) return this.advance()
    return null
  }

  check(type) {
    return this.peek()?.type === type
  }

  advance() {
    if (!this.isAtEnd()) this.current += 1
    return this.previous()
  }

  previous() {
    return this.tokens[this.current - 1]
  }

  peek() {
    return this.tokens[this.current]
  }

  isAtEnd() {
    return this.current >= this.tokens.length
  }
}

module.exports = {
  buildAst,
}
