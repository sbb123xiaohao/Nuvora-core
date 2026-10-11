/** Source for checked-in exception/limit/UTF-16 fixtures. */
public class JavaEdges {
    static int recurse(int n) { return 1 + recurse(n+1); }
    public static void main(String[] args) {
        String mode = args[0];
        int zero = 0;
        if (mode.equals("divide")) System.out.println(4 / zero);
        if (mode.equals("long-divide")) System.out.println(4L / zero);
        if (mode.equals("null")) { String s = null; System.out.println(s.length()); }
        if (mode.equals("bounds")) { int[] a = new int[2]; System.out.println(a[-1]); }
        if (mode.equals("store-bounds")) { long[] a = new long[2]; a[2] = 7; }
        if (mode.equals("negative")) { int[] a = new int[-1]; System.out.println(a.length); }
        if (mode.equals("store")) { Object[] a = new String[1]; a[0] = new JavaEdges(); }
        if (mode.equals("cast")) { Object o = new JavaEdges(); System.out.println((String)o); }
        if (mode.equals("char-bounds")) System.out.println("a".charAt(1));
        if (mode.equals("memory")) { long[] a = new long[1000000]; System.out.println(a.length); }
        if (mode.equals("recursive")) System.out.println(recurse(0));
        if (mode.equals("forever")) { while (true) zero++; }
        if (mode.equals("library")) System.out.println(System.nanoTime());
        if (mode.equals("utf")) {
            String s = "\u4e2d\u6587\ud83c\udf0d\u0000end";
            System.out.println(s + ":" + s.length() + ":" + s.charAt(0));
            System.out.println(args[1]);
            System.out.println(args[1].length());
        }
        if (mode.equals("arithmetic")) {
            long x = 9223372036854775807L;
            int y = 2147483647;
            for (int i = 0; i < 200; i++) {
                x = x * 6364136223846793005L + 1;
                y = y * 1664525 + 1013904223;
                long d = (x >>> 32) - 2147483648L;
                int e = (y >>> 16) - 32768;
                if (d == 0) d = 1;
                if (e == 0) e = 1;
                System.out.println((x / d) + ":" + (x % d) + ":" + (y / e) + ":" + (y % e));
                System.out.println((x << i) + ":" + (x >> i) + ":" + (x >>> i));
                System.out.println((y << i) + ":" + (y >> i) + ":" + (y >>> i));
                System.out.println((x & y) + ":" + (x | y) + ":" + (x ^ y) + ":" + (-x) + ":" + (-y));
            }
        }
    }
}
