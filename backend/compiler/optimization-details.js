function buildOptimizationDetails(compilerResult, sourceCode = "") {
  if (!compilerResult.success) return []

  const assembly = compilerResult.assembly || ""
  const optimizedAssembly = compilerResult.optimizedAssembly || ""
  const source = stripComments(sourceCode)
  const instructions = assembly
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line && !line.endsWith(":") && !line.startsWith("."))

  const optimizedInstructions = optimizedAssembly
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line && !line.endsWith(":") && !line.startsWith("."))

  const labels = assembly
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.endsWith(":"))

  const constantFolding = detectConstantFolding(source)
  const cascada = detectCascada(source)
  const sethiUllman = detectSethiUllman(source)
  const peephole = detectPeephole(assembly, optimizedAssembly)

  return [
    {
      pass: "constantFolding",
      name: "Constant Folding",
      status: appliedStatus(constantFolding.applied),
      description: "Evalua expresiones constantes antes de generar codigo.",
      notes: constantFolding.notes,
    },
    {
      pass: "cascada",
      name: "Cascada",
      status: appliedStatus(cascada.applied),
      description: "Ejecuta propagacion de constantes, simplificacion algebraica y limpieza de codigo muerto.",
      notes: cascada.notes,
    },
    {
      pass: "sethiUllman",
      name: "Sethi-Ullman",
      status: appliedStatus(sethiUllman.applied),
      description: "Calcula presion aproximada de registros para la generacion de codigo.",
      notes: sethiUllman.notes,
    },
    {
      pass: "peephole",
      name: "Peephole",
      status: appliedStatus(peephole.applied),
      description: "Optimiza patrones locales sobre el assembly generado.",
      notes: peephole.notes,
      metrics: {
        instructionCount: instructions.length,
        optimizedInstructionCount: optimizedInstructions.length,
        labelCount: labels.length,
        lineCount: assembly ? assembly.split(/\r?\n/).length : 0,
        optimizedLineCount: optimizedAssembly ? optimizedAssembly.split(/\r?\n/).length : 0,
      },
    },
  ]
}

function appliedStatus(applied) {
  return applied ? "Aplicado" : "No aplicado"
}

function stripComments(source) {
  return String(source || "")
    .replace(/\/\/.*$/gm, "")
    .replace(/\/\*[\s\S]*?\*\//g, "")
}

function detectConstantFolding(source) {
  const expressions = extractAssignedExpressions(source)
  const folded = expressions.filter((expression) => isConstantExpression(expression))

  if (folded.length > 0) {
    return {
      applied: true,
      notes: [`Expresiones constantes detectadas: ${folded.length}`],
    }
  }

  return {
    applied: false,
    notes: ["No hay expresiones constantes candidatas."],
  }
}

function detectCascada(source) {
  const literalAssignments = extractLiteralAssignments(source)
  const identities = countAlgebraicIdentities(source)
  const propagated = literalAssignments.filter(({ name }) => countIdentifierUses(source, name) > 1)
  const totalCandidates = propagated.length + identities

  if (totalCandidates > 0) {
    const notes = []
    if (propagated.length > 0) notes.push(`Constantes propagables: ${propagated.length}`)
    if (identities > 0) notes.push(`Simplificaciones algebraicas: ${identities}`)
    return { applied: true, notes }
  }

  return {
    applied: false,
    notes: ["No hay constantes propagables ni identidades algebraicas simples."],
  }
}

function detectSethiUllman(source) {
  const expressions = extractAssignedExpressions(source)
  const complexExpressions = expressions.filter((expression) => estimateExpressionPressure(expression) > 1)

  if (complexExpressions.length > 0) {
    return {
      applied: true,
      notes: [`Expresiones con presion de registros: ${complexExpressions.length}`],
    }
  }

  return {
    applied: false,
    notes: ["No hay expresiones que requieran planificacion de registros."],
  }
}

function detectPeephole(assembly, optimizedAssembly) {
  const before = normalizeAssembly(assembly)
  const after = normalizeAssembly(optimizedAssembly)

  if (before && after && before !== after) {
    return {
      applied: true,
      notes: ["El assembly optimizado difiere del assembly base."],
    }
  }

  return {
    applied: false,
    notes: ["No se encontraron cambios locales en el assembly."],
  }
}

function extractAssignedExpressions(source) {
  return Array.from(source.matchAll(/\b(?:var|const)?\s*[A-Za-z_]\w*\s*(?::[^=;]+)?=\s*([^;]+);/g))
    .map((match) => match[1].trim())
    .filter(Boolean)
}

function extractLiteralAssignments(source) {
  return Array.from(source.matchAll(/\b(?:var|const)\s+([A-Za-z_]\w*)\s*(?::[^=;]+)?=\s*(-?\d+(?:\.\d+)?|true|false|'[^']*'|"[^"]*")\s*;/g))
    .map((match) => ({ name: match[1], value: match[2] }))
}

function isConstantExpression(expression) {
  const normalized = expression.replace(/\s+/g, "")
  if (!/[+\-*/%<>=!&|]/.test(normalized)) return false
  return /^[-+*/%<>=!&|().\dtruefals'"]+$/.test(normalized)
}

function countAlgebraicIdentities(source) {
  const matches = source.match(/(\+\s*0|-\s*0|\*\s*1|\/\s*1|\*\s*0|0\s*\+)/g)
  return matches ? matches.length : 0
}

function countIdentifierUses(source, name) {
  const matches = source.match(new RegExp(`\\b${escapeRegExp(name)}\\b`, "g"))
  return matches ? matches.length : 0
}

function estimateExpressionPressure(expression) {
  const operatorCount = (expression.match(/[+\-*/%]|==|!=|<=|>=|<|>/g) || []).length
  const nestedCount = (expression.match(/[()]/g) || []).length / 2
  return operatorCount + nestedCount
}

function normalizeAssembly(assembly) {
  return String(assembly || "")
    .replace(/\r\n/g, "\n")
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean)
    .join("\n")
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
}

module.exports = {
  buildOptimizationDetails,
}
