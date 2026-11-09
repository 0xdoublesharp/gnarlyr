// Define architectural vector data and selected overlay entry before analysis.
// @category SnowskyEcho
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DWordDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class PrepareEcho extends GhidraScript {
    @Override
    public void run() throws Exception {
        if (!currentProgram.getLanguageID().toString().equals("ARM:LE:32:Cortex")) {
            throw new IllegalStateException("Expected explicit Cortex/Thumb language");
        }
        Address vector = toAddr(0x03050000L);
        clearListing(vector, vector.add(63));
        createLabel(vector, "echo_vector_table", true);
        for (int i = 0; i < 16; i++) {
            Address slot = vector.add(i * 4L);
            long value = Integer.toUnsignedLong(getInt(slot));
            createData(slot, DWordDataType.dataType);
            setEOLComment(slot, i == 0 ? "Initial stack pointer from stock vector table" :
                          "Architectural exception vector " + i);
            if (i > 0 && (value & 1) == 1) {
                Address target = toAddr(value & ~1L);
                if (currentProgram.getMemory().contains(target)) {
                    disassemble(target);
                    Function f = getFunctionAt(target);
                    if (f == null) f = createFunction(target, i == 1 ? "reset_handler" : null);
                    if (i == 1 && f != null) f.setName("reset_handler", SourceType.USER_DEFINED);
                }
            }
        }
        String[] args = getScriptArgs();
        for (String arg : args) {
            Address entry = toAddr(Long.parseUnsignedLong(arg.replace("0x", ""), 16));
            if (!currentProgram.getMemory().contains(entry)) throw new IllegalArgumentException("Unmapped overlay entry");
            disassemble(entry);
            if (getFunctionAt(entry) == null) createFunction(entry, "overlay_entry_" + entry);
        }
        println("Verified Cortex language; architectural vectors and requested overlay entries seeded.");
    }
}
