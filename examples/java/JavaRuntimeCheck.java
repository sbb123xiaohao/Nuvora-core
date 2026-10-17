import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicLong;

/** Runs on the actual OpenJDK runtime; intentionally exceeds bootstrap's API. */
public final class JavaRuntimeCheck {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static long hot(long value) {
        return (value * 1664525L + 1013904223L) ^ (value >>> 13);
    }
    public static void main(String[] args) throws Exception {
        check(Runtime.version().feature() == 21, "Java version");
        long before = System.nanoTime();
        Thread.sleep(30);
        check(System.nanoTime() - before >= 20_000_000L, "monotonic sleep");
        check(System.currentTimeMillis() > 0, "realtime clock");
        AtomicLong sum = new AtomicLong();
        CountDownLatch ready = new CountDownLatch(4), start = new CountDownLatch(1);
        ArrayList<Thread> workers = new ArrayList<>();
        for (int i = 0; i < 4; i++) {
            Thread worker = new Thread(() -> {
                ready.countDown();
                try {start.await();} catch (InterruptedException e) {throw new AssertionError(e);}
                for (int n = 1; n <= 10000; n++) sum.addAndGet(n);
            }, "nuvora-worker-" + i);
            workers.add(worker);worker.start();
        }
        ready.await();start.countDown();
        for (Thread worker : workers) worker.join();
        check(sum.get() == 200020000L, "threads/atomic/latch/join");
        check(BigInteger.ONE.shiftLeft(80).toString().equals("1208925819614629174706176"), "BigInteger");
        byte[] digest = MessageDigest.getInstance("SHA-256").digest("abc".getBytes(StandardCharsets.UTF_8));
        check(java.util.HexFormat.of().formatHex(digest).equals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 provider");
        boolean jit = (args.length > 0 && args[0].equals("jit")) || (args.length > 1 && args[1].equals("jit"));
        Path path = Path.of(args.length > 0 && !args[0].equals("jit") ? args[0] : "/home/jvm-proof.txt");
        String body = "Real OpenJDK 21 on Nuvora: threads=4 sum=" + sum.get() + "\n";
        Files.writeString(path, body, StandardCharsets.UTF_8);
        check(Files.readString(path, StandardCharsets.UTF_8).equals(body), "NIO UTF-8 file round trip");
        if(args.length>1 && args[1].equals("privacy")) {
            try(var stream=Files.newInputStream(Path.of("/home/.system/accounts.db"))) {
                throw new AssertionError("private account database exposed");
            } catch(java.nio.file.AccessDeniedException expected) {
                System.out.println("OPENJDK PRIVATE ACCESS DENIED");
            }
        }
        for (int n=0;n<16;n++) {byte[] blob=new byte[256*1024];blob[0]=(byte)n;}
        System.gc();
        if (jit) {
            long value=7;
            for(int i=0;i<200000;i++)value=hot(value);
            check(Long.toUnsignedString(value,16).equals("dc9889355911c47f"),"compiled arithmetic");
            System.out.println("HOTSPOT JIT PASS checksum=" + Long.toUnsignedString(value,16));
        }
        System.out.println("OPENJDK JAVA PASS: threads, atomics, latch, join, BigInteger, SHA-256, NIO, clocks and GC");
        System.out.println("sum=" + sum.get() + " epochMillis=" + System.currentTimeMillis());
    }
}
