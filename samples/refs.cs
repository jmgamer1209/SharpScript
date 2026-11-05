using System;

// SharpScript demo: ref / out / in parameters, including structs and `out var`.
// This file is also valid C#.

struct Counter
{
    public int value;

    public Counter(int value)
    {
        this.value = value;
    }
}

class ReferenceProgram
{
    public static void Swap(ref int left, ref int right)
    {
        int temp = left;
        left = right;
        right = temp;
    }

    public static void Split(int total, out int half, out int rest)
    {
        half = total / 2;
        rest = total - half;
    }

    // `ref` on a struct edits the caller's storage, not a copy.
    public static void Bump(ref Counter counter)
    {
        counter.value += 1;
    }

    // `in` is a read-only reference: no copy, assignment is a compile error.
    public static int Read(in Counter counter)
    {
        return counter.value;
    }

    public static void Main()
    {
        int a = 1;
        int b = 2;
        Swap(ref a, ref b);
        Console.WriteLine("a = " + a + ", b = " + b);

        int half = 0;
        int rest = 0;
        Split(9, out half, out rest);
        Console.WriteLine("half = " + half + ", rest = " + rest);

        // `out var` declares the variable at the call site.
        Split(11, out var inferredHalf, out var inferredRest);
        Console.WriteLine("inferred = " + inferredHalf + ", " + inferredRest);

        Counter counter = new Counter(5);
        Bump(ref counter);
        Console.WriteLine("counter = " + counter.value);
        Console.WriteLine("read = " + Read(in counter));

        // A struct assigned to another local is a copy, so this stays 6.
        Counter copy = counter;
        copy.value = 0;
        Console.WriteLine("counter after copy edit = " + counter.value);
        Console.WriteLine("copy = " + copy.value);
    }
}
