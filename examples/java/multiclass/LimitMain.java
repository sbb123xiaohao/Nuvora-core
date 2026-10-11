/* Exceeds the documented 16-class bootstrap cap; OpenJDK prints 16. */
public final class LimitMain {
    public static void main(String[] args) { System.out.println(Limit00.get()); }
}
final class Limit00 { static int get() { return Limit01.get() + 1; } }
final class Limit01 { static int get() { return Limit02.get() + 1; } }
final class Limit02 { static int get() { return Limit03.get() + 1; } }
final class Limit03 { static int get() { return Limit04.get() + 1; } }
final class Limit04 { static int get() { return Limit05.get() + 1; } }
final class Limit05 { static int get() { return Limit06.get() + 1; } }
final class Limit06 { static int get() { return Limit07.get() + 1; } }
final class Limit07 { static int get() { return Limit08.get() + 1; } }
final class Limit08 { static int get() { return Limit09.get() + 1; } }
final class Limit09 { static int get() { return Limit10.get() + 1; } }
final class Limit10 { static int get() { return Limit11.get() + 1; } }
final class Limit11 { static int get() { return Limit12.get() + 1; } }
final class Limit12 { static int get() { return Limit13.get() + 1; } }
final class Limit13 { static int get() { return Limit14.get() + 1; } }
final class Limit14 { static int get() { return Limit15.get() + 1; } }
final class Limit15 { static int get() { return 1; } }
