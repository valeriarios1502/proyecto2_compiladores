function buildOptimizationDetails(compilerResult) {
  if (!compilerResult.success) return []

  const assembly = compilerResult.assembly || ""
  const instructions = assembly
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line && !line.endsWith(":") && !line.startsWith("."))

  const labels = assembly
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.endsWith(":"))

  return [
    {
      pass: "constantFolding",
      name: "Constant Folding",
      status: "Aplicado",
      description: "Evalua expresiones constantes antes de generar codigo.",
    },
    {
      pass: "cascada",
      name: "Cascada",
      status: "Aplicado",
      description: "Ejecuta propagacion de constantes, simplificacion algebraica y limpieza de codigo muerto.",
    },
    {
      pass: "sethiUllman",
      name: "Sethi-Ullman",
      status: "Aplicado",
      description: "Calcula presion aproximada de registros para la generacion de codigo.",
    },
    {
      pass: "peephole",
      name: "Peephole",
      status: "Aplicado",
      description: "Optimiza patrones locales sobre el assembly generado.",
      metrics: {
        instructionCount: instructions.length,
        labelCount: labels.length,
        lineCount: assembly ? assembly.split(/\r?\n/).length : 0,
      },
    },
  ]
}

module.exports = {
  buildOptimizationDetails,
}
