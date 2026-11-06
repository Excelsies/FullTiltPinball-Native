// Export every function of the current program as decompiled C (<out>/<program>.c) and an index
// (<out>/<program>.tsv: entry, name, size, called entries). Headless:
//   analyzeHeadless <proj dir> <proj> -import <exe> -scriptPath ghidra_scripts -postScript ExportAll.java <out>
//@category FullTilt
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

import java.io.File;
import java.io.PrintWriter;

public class ExportAll extends GhidraScript {
    /** Virtual methods are only reachable through vtables in data: make functions of those targets. */
    private int createDataReferencedFunctions() throws Exception {
        int created = 0;
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (block.isExecute() || !block.isInitialized()) {
                continue;
            }
            for (Address a = block.getStart(); a.add(3).compareTo(block.getEnd()) <= 0; a = a.add(4)) {
                monitor.checkCancelled();
                Address target = toAddr(getInt(a) & 0xffffffffL);
                MemoryBlock tb = currentProgram.getMemory().getBlock(target);
                if (tb == null || !tb.isExecute() || getFunctionAt(target) != null || getFunctionContaining(target) != null) {
                    continue;
                }
                if (disassemble(target) && createFunction(target, null) != null) {
                    created++;
                }
            }
        }
        return created;
    }

    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0]);
        out.mkdirs();
        String name = currentProgram.getName();
        println(String.format("ExportAll %s: %d functions created at code pointers in data", name, createDataReferencedFunctions()));
        DecompInterface decompiler = new DecompInterface();
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("decompiler failed to open " + name + ": " + decompiler.getLastMessage());
        }
        int failed = 0, total = 0;
        try (PrintWriter c = new PrintWriter(new File(out, name + ".c"));
             PrintWriter index = new PrintWriter(new File(out, name + ".tsv"))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                monitor.checkCancelled();
                total++;
                DecompileResults r = decompiler.decompileFunction(f, 120, monitor);
                String code;
                if (r.decompileCompleted()) {
                    code = r.getDecompiledFunction().getC();
                } else {
                    failed++;
                    code = "/* decompile failed: " + r.getErrorMessage() + " */\n";
                }
                long size = f.getBody().getNumAddresses();
                c.printf("// ==== %s %s size %d%n%s%n", f.getEntryPoint(), f.getName(), size, code);
                StringBuilder calls = new StringBuilder();
                for (Function callee : f.getCalledFunctions(monitor)) {
                    calls.append(callee.getEntryPoint()).append(',');
                }
                index.printf("%s\t%s\t%d\t%s%n", f.getEntryPoint(), f.getName(), size, calls);
            }
        } finally {
            decompiler.dispose();
        }
        println(String.format("ExportAll %s: %d functions, %d failed to decompile", name, total, failed));
        if (failed > total / 10) {
            throw new IllegalStateException("too many decompile failures: " + failed + " of " + total);
        }
    }
}
