package pkg;
public final class Entry {
    public static void main(String[] args) {
        System.out.println("package=" + Value.get() + ", arg=" + (args.length == 0 ? "none" : args[0]));
        System.out.println("PACKAGE PASS");
    }
}
final class Value { static int get() { return 21; } }
