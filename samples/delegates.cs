using System;

// SharpScript demo: delegates, lambdas, method groups, multicast and closures.
// This file is also valid C#.

delegate int Operation(int left, int right);

class DelegateProgram
{
    public static int Add(int left, int right)
    {
        return left + right;
    }

    public static int Apply(int value, Func<int, int> transform)
    {
        return transform(value);
    }

    public static void Main()
    {
        // Method group -> delegate.
        Operation op = Add;
        Console.WriteLine("add: " + op(2, 3));

        // Lambda with inferred parameter types.
        op = (left, right) => left * right;
        Console.WriteLine("mul: " + op(2, 3));

        // Multicast: every target runs, the last result is returned.
        Operation chain = Add;
        chain += (left, right) => left - right;
        Console.WriteLine("chain: " + chain(10, 4));

        Func<int, int> square = value => value * value;
        Console.WriteLine("square: " + square(9));
        Console.WriteLine("apply: " + Apply(5, square));

        Action<string> log = message => Console.WriteLine("[log] " + message);
        log("hello");

        // Closures capture locals by reference (C# semantics).
        int total = 0;
        Action<int> accumulate = value => total += value;
        accumulate(3);
        accumulate(4);
        Console.WriteLine("total: " + total);

        // A delegate variable can be reassigned and checked for null.
        Func<int> read = null;
        Console.WriteLine("read is null: " + (read == null));
        read = () => total;
        Console.WriteLine("read: " + read());
    }
}
