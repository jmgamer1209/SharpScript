using System;

// SharpScript demo: switch, do/while, break/continue, ternary, compound
// assignment, bit operations, casts and recursion.
// This file is also valid C#.

class ControlFlowProgram
{
    public static string Classify(int value)
    {
        switch (value)
        {
            case 0:
                return "zero";
            case 1:
                return "one";
            case 2:
                return "two";
            default:
                return "many";
        }
    }

    public static int Factorial(int value)
    {
        if (value <= 1)
        {
            return 1;
        }
        return value * Factorial(value - 1);
    }

    public static void Main()
    {
        int[] samples = new int[] { 0, 1, 2, 5 };
        foreach (var value in samples)
        {
            Console.WriteLine(value + " -> " + Classify(value));
        }

        int countdown = 3;
        while (countdown > 0)
        {
            Console.WriteLine("while " + countdown);
            countdown -= 1;
        }

        int steps = 0;
        do
        {
            steps += 1;
        } while (steps < 2);
        Console.WriteLine("do steps: " + steps);

        // continue skips the rest, break leaves the loop.
        int oddTotal = 0;
        for (int i = 0; i < 10; i++)
        {
            if (i % 2 == 0)
            {
                continue;
            }
            if (i > 7)
            {
                break;
            }
            oddTotal += i;
        }
        Console.WriteLine("odd total: " + oddTotal);

        int flags = 1 | 4;
        flags ^= 1;
        flags &= 5;
        Console.WriteLine("flags: " + flags);
        Console.WriteLine("shift: " + (1 << 5));
        Console.WriteLine("mask: " + (flags & 4));

        int big = 300;
        double ratio = (double)big / 7.0;
        string sign = ratio > 0.0 ? "positive" : "negative";
        Console.WriteLine("ratio " + ratio + " is " + sign);

        int code = (int)ratio;
        Console.WriteLine("truncated: " + code);

        Console.WriteLine("factorial 5 = " + Factorial(5));

        string word = "step";
        Console.WriteLine("first char: " + word[0]);
        Console.WriteLine("last char: " + word[word.Length - 1]);
    }
}
