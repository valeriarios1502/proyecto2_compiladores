const express = require("express")
const cors = require("cors")
const fs = require("fs/promises")
const path = require("path")
const os = require("os")
const { execFile } = require("child_process")

const app = express()

app.use(cors())
app.use(express.json())

app.post("/compile", async (req, res) => {
  const sourceCode = req.body.sourceCode || ""

  if (!sourceCode.trim()) {
    return res.json({
      success: false,
      errors: ["No se proporcionó código fuente."],
    })
  }

  const tempFile = path.join(os.tmpdir(), `input-${Date.now()}.txt`)

  try {
    await fs.writeFile(tempFile, sourceCode, "utf8")

    const compilerPath = path.join(__dirname, "..", "Proyecto2.exe")

    execFile(compilerPath, [tempFile], { timeout: 5000 }, (error, stdout, stderr) => {
      res.json({
        success: !error && !stderr,
        tokens: [],
        parseStatus: error || stderr ? "Error" : "Correcto",
        parseSuccess: !error && !stderr,
        optimizations: {
          constantFolding: false,
          cascada: false,
          sethiUllman: false,
          peephole: false,
        },
        optimizationDetails: [],
        assembly: stdout || "",
        optimizedAssembly: stdout || "",
        ast: null,
        errors: error || stderr ? [stderr || error.message] : [],
        scannerStatus: error || stderr ? "Error" : "Correcto",
        scannerSuccess: !error && !stderr,
        stats: {
          tokenCount: 0,
          lineCount: sourceCode.split("\n").length,
          functionCount: 0,
        },
      })
    })
  } catch (err) {
    res.status(500).json({
      success: false,
      errors: [err.message],
    })
  }
})

app.listen(3001, () => {
  console.log("Backend del compilador corriendo en http://localhost:3001")
})