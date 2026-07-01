const fs = require("fs/promises")
const path = require("path")
const os = require("os")
const { execFile } = require("child_process")

async function findCompilerPath() {
  const candidates =
    process.platform === "win32"
      ? [
          path.join(__dirname, "..", "..", "Proyecto2-win.exe"),
          path.join(__dirname, "..", "..", "Proyecto2.exe"),
          path.join(__dirname, "..", "..", "build", "Proyecto2.exe"),
        ]
      : [
          path.join(__dirname, "..", "..", "build", "Proyecto2"),
          path.join(__dirname, "..", "..", "Proyecto2"),
        ];

  for (const candidate of candidates) {
    try {
      await fs.access(candidate);
      return candidate;
    } catch {}
  }

  throw new Error("No se encontro el ejecutable del compilador.");
}

function parseCompilerOutput(stdout) {
  const lines = stdout.replace(/\r\n/g, "\n").split("\n")
  const parseSuccess = lines[0]?.trim() === "Parseo exitoso"
  const assembly = parseSuccess ? lines.slice(1).join("\n").trimStart() : stdout

  return {
    parseSuccess,
    parseStatus: parseSuccess ? "Correcto" : "Error",
    assembly,
  }
}

async function runCompiler(sourceCode) {
  const tempFile = path.join(os.tmpdir(), `input-${Date.now()}-${process.pid}.txt`)
  await fs.writeFile(tempFile, sourceCode, "utf8")

  try {
    const compilerPath = await findCompilerPath()

    return await new Promise((resolve) => {
      execFile(compilerPath, [tempFile], { timeout: 5000 }, (error, stdout, stderr) => {
        const compilerErrors = [stderr, error?.message].filter(Boolean)
        const parsedOutput = parseCompilerOutput(stdout || "")
        const success = !error && !stderr && parsedOutput.parseSuccess
        const assembly = success ? parsedOutput.assembly : ""

        resolve({
          success,
          parseStatus: success ? "Correcto" : "Error",
          parseSuccess: success,
          assembly,
          optimizedAssembly: assembly,
          errors: compilerErrors,
        })
      })
    })
  } finally {
    await fs.unlink(tempFile).catch(() => {})
  }
}

module.exports = {
  runCompiler,
}
