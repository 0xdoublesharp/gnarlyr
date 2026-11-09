// Export selected function pseudocode and instructions after offline analysis.
// @category SnowskyEcho
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import com.google.gson.GsonBuilder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.security.MessageDigest;

public class ExportEcho extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("output-directory and function addresses required");
        Path out = Path.of(args[0]);
        Files.createDirectories(out);
        Map<String, Object> report = new LinkedHashMap<>();
        report.put("program", currentProgram.getName());
        report.put("import_sha256", currentProgram.getExecutableSHA256());
        report.put("language", currentProgram.getLanguageID().toString());
        report.put("function_count", currentProgram.getFunctionManager().getFunctionCount());
        List<Object> memory = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            Map<String, Object> item = new LinkedHashMap<>();
            item.put("name", block.getName()); item.put("start", block.getStart().toString());
            item.put("size", block.getSize()); item.put("initialized", block.isInitialized());
            item.put("execute", block.isExecute());
            if (block.isInitialized()) {
                byte[] bytes = new byte[(int) block.getSize()];
                currentProgram.getMemory().getBytes(block.getStart(), bytes);
                item.put("sha256", HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)));
            }
            memory.add(item);
        }
        report.put("memory", memory);
        List<Object> functions = new ArrayList<>();
        DecompInterface decompiler = new DecompInterface();
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
        try {
            for (int i = 1; i < args.length; i++) {
                Address address = toAddr(Long.parseUnsignedLong(args[i].replace("0x", ""), 16));
                Function f = getFunctionAt(address);
                if (f == null) throw new IllegalStateException("No function at " + address);
                DecompileResults result = decompiler.decompileFunction(f, 60, monitor);
                Map<String, Object> item = new LinkedHashMap<>();
                item.put("address", address.toString()); item.put("name", f.getName());
                item.put("thumb_mode", currentProgram.getProgramContext().getValue(currentProgram.getRegister("TMode"), address, false));
                item.put("decompile_completed", result.decompileCompleted());
                item.put("diagnostic", result.getErrorMessage());
                if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
                    throw new IllegalStateException("Decompilation failed at " + address + ": " + result.getErrorMessage());
                }
                String pseudocode = result.getDecompiledFunction().getC();
                Files.writeString(out.resolve(address + ".c"), pseudocode, StandardCharsets.UTF_8);
                StringBuilder assembly = new StringBuilder();
                InstructionIterator instructions = currentProgram.getListing().getInstructions(f.getBody(), true);
                int count = 0;
                while (instructions.hasNext()) {
                    Instruction instruction = instructions.next();
                    assembly.append(instruction.getAddress()).append("  ");
                    for (byte b : instruction.getBytes()) assembly.append(String.format("%02x", b & 255));
                    assembly.append("  ").append(instruction).append('\n'); count++;
                }
                Files.writeString(out.resolve(address + ".asm"), assembly.toString(), StandardCharsets.UTF_8);
                item.put("instructions", count); item.put("pseudocode_characters", pseudocode.length());
                List<String> callees = new ArrayList<>();
                for (Function callee : f.getCalledFunctions(monitor)) callees.add(callee.getEntryPoint().toString());
                Collections.sort(callees); item.put("callees", callees);
                List<String> callers = new ArrayList<>();
                ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(address);
                while (references.hasNext()) callers.add(references.next().getFromAddress().toString());
                item.put("references_to_entry", callers);
                functions.add(item);
                println("DECOMPILED " + address + " " + f.getName() + " instructions=" + count + " chars=" + pseudocode.length());
            }
        } finally { decompiler.dispose(); }
        report.put("functions", functions);
        String json = new GsonBuilder().setPrettyPrinting().create().toJson(report);
        Files.writeString(out.resolve("analysis.json"), json + "\n", StandardCharsets.UTF_8);
        println("ANALYSIS_REPORT " + out.resolve("analysis.json"));
    }
}
