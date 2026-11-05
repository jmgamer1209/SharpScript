using System;

// SharpScript demo: basics. This file is also valid C#.
class HelloProgram
{
    public static void Main()
    {
        Console.WriteLine("Hello from SharpScript!");

        int a = 7;
        int b = 6;
        Console.WriteLine("7 * 6 = " + (a * b));

        string text = "SharpScript";
        Console.WriteLine("length: " + text.Length);

        double ratio = 3.5;
        Console.WriteLine("ratio * 2 = " + (ratio * 2));

        int[] numbers = new int[] { 5, 3, 9, 1 };
        int total = 0;
        foreach (var n in numbers)
        {
            total += n;
        }
        Console.WriteLine("total: " + total);

        for (int i = 0; i < 3; i++)
        {
            Console.WriteLine("for " + i);
        }

        Array<int> list = new Array<int>();
        list.Add(4);
        list.Add(6);
        list.Insert(1, 5);
        Console.WriteLine("list count: " + list.Count + " at 1: " + list.Get(1));
    }
}
