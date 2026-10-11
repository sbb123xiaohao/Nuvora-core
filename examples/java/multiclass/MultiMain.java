/* Compile all classes with Java 21. No host libraries beyond the documented
 * PrintStream/String subset. This is a loading/initialization test, not a game. */
public final class MultiMain {
    static int initialized = Trace.mark(1);
    public static void main(String[] args) {
        System.out.println("Java 21 / multiple application classes");
        Cold[] unloaded = new Cold[2]; // resolution must not run <clinit>
        System.out.println("lazy=" + Trace.order + ", null=" + (unloaded[0] == null));
        System.out.println("cycle=" + First.value + "," + Second.value);
        Block a = new Block(7, 1234567890123L), b = new Block(5, -19L);
        Block[] blocks = {a, b};
        Object[] covariant = blocks;
        covariant[1] = a;
        System.out.println("block=" + blocks[1].score(3) + ", mass=" + a.mass());
        System.out.println("cast=" + (((Block) covariant[0]) == a) + ", types=" + (a instanceof Block));
        System.out.println("intern=" + (Block.label() == "shared world") + ", text=" + Block.label().equals("shared world"));
        // invokevirtual return values must use the caller's stack limits/pool.
        long total = Chunk.sum(blocks, 10L);
        System.out.println("sum=" + total + ", calls=" + Chunk.calls);
        WriteFirst.value = 88; // putstatic must preserve its value across <clinit>
        System.out.println("write=" + WriteFirst.value + ", cached=" + Block.cached.score(2));
        System.out.println("init=" + Trace.order + ", cold=" + Cold.value + ", once=" + Cold.value);
        System.out.println("final-order=" + Trace.order);
        if (args.length > 0) {
            String mode = args[0];
            if (mode.equals("store")) { covariant[0] = new Chunk(); }
            else if (mode.equals("cast")) { Object o = new Chunk(); Block wrong = (Block) o; }
            else if (mode.equals("missing")) { System.out.println(Missing.value()); }
            else if (mode.equals("init-fail")) { System.out.println(Broken.value); }
            else for (int i = 0; i < args.length; i++) System.out.println("arg[" + i + "]=" + args[i]);
        }
        System.out.println("MULTICLASS PASS");
    }
}
final class Trace {
    static int order;
    static int mark(int n) { order = order * 10 + n; return n; }
}
final class First {
    static int value = Second.value + Trace.mark(2);
}
final class Second {
    static int value = First.value + Trace.mark(3);
}
final class Cold {
    static int value = Trace.mark(4);
}
final class Block {
    static int initialized = Trace.mark(5);
    static Block cached = new Block(1, 2L);
    private final int id;
    private final long weight;
    Block(int id, long weight) { this.id = id; this.weight = weight; }
    int score(int n) { return id * n; }
    long mass() { return weight; }
    static String label() { return "shared world"; }
}
final class Chunk {
    static int initialized = Trace.mark(6);
    static int calls;
    static long sum(Block[] blocks, long bias) {
        calls++;
        for (int i = 0; i < blocks.length; i++) bias += blocks[i].mass();
        return bias;
    }
}
final class WriteFirst { static int value = Trace.mark(7); }
final class Missing { static int value() { return 42; } }
final class Broken { static int zero; static int value = 1 / zero; }
