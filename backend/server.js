const express = require("express")
const cors = require("cors")

const { runCompiler } = require("./compiler/compiler-runner")
const {
  buildCompileResult,
  calculateStats,
  normalizeSourceCode,
} = require("./compiler/compile-result")
const { buildAst } = require("./compiler/ast-builder")
const { buildOptimizationDetails } = require("./compiler/optimization-details")
const { tokenize } = require("./compiler/tokenizer")

const app = express()

app.use(cors())
app.use(express.json())

app.post("/compile", async (req, res) => {
  const sourceCode = normalizeSourceCode(req.body.sourceCode)
  const scanResult = tokenize(sourceCode)
  const stats = calculateStats(sourceCode, scanResult.tokens)
  const ast = scanResult.success ? buildAst(scanResult.tokens) : null

  if (!sourceCode.trim()) {
    return res.json(buildCompileResult({
      tokens: scanResult.tokens,
      errors: ["No se proporciono codigo fuente."],
      scannerStatus: "Error",
      scannerSuccess: false,
      stats,
    }))
  }

  if (!scanResult.success) {
    return res.json(buildCompileResult({
      tokens: scanResult.tokens,
      errors: scanResult.errors,
      scannerStatus: "Error",
      scannerSuccess: false,
      stats,
    }))
  }

  try {
    const compilerResult = await runCompiler(sourceCode)
    const optimizationDetails = buildOptimizationDetails(compilerResult, sourceCode)
    const optimizations = buildOptimizationFlags(optimizationDetails)

    return res.json(buildCompileResult({
      ...compilerResult,
      tokens: scanResult.tokens,
      optimizations,
      optimizationDetails,
      ast: compilerResult.success ? ast : null,
      scannerStatus: "Correcto",
      scannerSuccess: true,
      stats,
    }))
  } catch (err) {
    return res.status(500).json(buildCompileResult({
      tokens: scanResult.tokens,
      errors: [err.message],
      scannerStatus: scanResult.success ? "Correcto" : "Error",
      scannerSuccess: scanResult.success,
      stats,
    }))
  }
})

function buildOptimizationFlags(details) {
  return {
    constantFolding: isApplied(details, "constantFolding"),
    cascada: isApplied(details, "cascada"),
    sethiUllman: isApplied(details, "sethiUllman"),
    peephole: isApplied(details, "peephole"),
  }
}

function isApplied(details, pass) {
  return details.some((detail) => detail.pass === pass && detail.status === "Aplicado")
}

const port = process.env.PORT || 3001

app.listen(port, () => {
  console.log(`Backend del compilador corriendo en http://localhost:${port}`)
})
