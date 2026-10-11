/** Real Java 21 bytecode; also runs unchanged on OpenJDK. No Minecraft code. */
public class NuvoraJavaDemo {
    static int checks;
    static int bias = 7;
    private long seed;

    NuvoraJavaDemo(long seed) { this.seed = seed; }
    long next() {
        seed = seed * 6364136223846793005L + 1442695040888963407L;
        return seed;
    }
    static void check(boolean ok) {
        if (!ok) {
            int zero = 0;
            System.out.println(1 / zero);
        }
        checks++;
    }
    static int dense(int n) {
        return switch (n) { case 0 -> 3; case 1 -> 7; case 2 -> 11; default -> -1; };
    }
    static int sparse(int n) {
        return switch (n) { case -500 -> 9; case 1000 -> 5; case 999999 -> 2; default -> -1; };
    }
    static int fibonacci(int n) { return n < 2 ? n : fibonacci(n-1) + fibonacci(n-2); }
    static long chunk() {
        // A small block-grid workload, not a Minecraft implementation.
        int[] blocks = new int[16 * 16 * 32];
        NuvoraJavaDemo random = new NuvoraJavaDemo(21);
        for (int i = 0; i < blocks.length; i++) blocks[i] = (int)(random.next() >>> 56);
        long checksum = 0;
        for (int z = 0; z < 32; z++)
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    checksum = (checksum * 31 + blocks[x + y*16 + z*256]) & 0xffffffffL;
        return checksum;
    }
    public static void main(String[] args) {
        System.out.println("Nuvora Java bootstrap / class format 65");
        int min = -2147483648, minus = -1;
        long wideMin = -9223372036854775808L, wideMinus = -1;
        check(min / minus == min && min % minus == 0);
        check(wideMin / wideMinus == wideMin && wideMin % wideMinus == 0);
        check((min >>> 31) == 1 && (wideMin >>> 63) == 1);
        check((min >> 63) == -1 && (wideMin >> 127) == -1);
        check((min << 32) == min && (wideMin << 64) == wideMin);
        check((min + minus) == 2147483647 && (wideMin + wideMinus) == 9223372036854775807L);
        check((long)min == -2147483648L && (int)wideMin == 0);
        check((byte)255 == -1 && (short)65535 == -1 && (char)65535 == 65535);
        check(bias == 7 && fibonacci(10) == 55);
        check(dense(1) == 7 && dense(9) == -1 && sparse(-500) == 9 && sparse(0) == -1);
        int[] a = new int[3]; a[1] = 4; check(a[1]++ == 4 && a[1] == 5);
        long[] b = new long[2]; b[0] = wideMin; check(b[0]++ == wideMin && b[0] == wideMin + 1);
        byte[] bytes = new byte[1]; bytes[0] = -42; check(bytes[0] == -42);
        short[] shorts = new short[1]; shorts[0] = -32000; check(shorts[0] == -32000);
        char[] chars = new char[1]; chars[0] = '\uffff'; check(chars[0] == 65535);
        boolean[] bools = new boolean[1]; bools[0] = true; check(bools[0]);
        String[] words = new String[2]; words[0] = "native"; check(words[0].length() == 6 && words[1] == null);
        Object object = new NuvoraJavaDemo(12);
        check(object instanceof NuvoraJavaDemo && ((NuvoraJavaDemo)object).seed == 12);
        String unicode = "Java\u4e2d\u6587\ud83c\udf0d";
        check(unicode.length() == 8 && unicode.charAt(4) == '\u4e2d' && unicode.charAt(6) == '\ud83c');
        check("native" == words[0] && "native".equals(words[0]) && !"native".equals(null));
        long checksum = chunk();
        System.out.println("checks=" + checks + ", chunk=" + checksum);
        System.out.println("int=" + min + ", long=" + wideMin + ", flag=" + true + ", char=" + 'J');
        for (int i = 0; i < args.length; i++) System.out.println("arg[" + i + "]=" + args[i]);
        System.out.println("JAVA21 DEMO PASS");
    }
}
