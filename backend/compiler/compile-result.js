function buildCompileResult(overrides = {}) {
  return {
    success: false,
    tokens: [],
    parseStatus: "No ejecutado",
    parseSuccess: false,
    optimizations: {
      constantFolding: false,
      cascada: false,
      sethiUllman: false,
      peephole: false,
    },
    optimizationDetails: [],
    assembly: "",
    optimizedAssembly: "",
    ast: null,
    errors: [],
    scannerStatus: "No ejecutado",
    scannerSuccess: false,
    stats: {
      tokenCount: 0,
      lineCount: 0,
      functionCount: 0,
    },
    ...overrides,
  }
}

function calculateStats(sourceCode, tokens) {
  return {
    tokenCount: tokens.filter((token) => token.type !== "END").length,
    lineCount: sourceCode.length ? sourceCode.split(/\r\n|\r|\n/).length : 0,
    functionCount: tokens.filter((token) => token.type === "FN").length,
  }
}

function normalizeSourceCode(value) {
  if (typeof value === "string") return value
  if (Array.isArray(value)) return value.join("\n")
  return ""
}

module.exports = {
  buildCompileResult,
  calculateStats,
  normalizeSourceCode,
}
