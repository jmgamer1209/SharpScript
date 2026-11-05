using System;

// SharpScript demo: calling into the host API (Math is built in, Host is
// registered by the CLI). This file is also valid C# when the host API is
// declared (see the C# compatibility check in the test suite).
class NativeProgram
{
    public static void Main()
    {
        Console.WriteLine("sqrt(2) = " + Math.Sqrt(2.0));
        Console.WriteLine("max(3, 8) = " + Math.Max(3, 8));
        Console.WriteLine("floor(2.9) = " + Math.Floor(2.9));

        Host.Log("hello from the C# host");
        Console.WriteLine("host time: " + Host.Now());
        Console.WriteLine("ticks: " + Host.GetTickCount());

        // String methods use the C# names, so this also compiles with Roslyn.
        string csv = "alpha,beta,gamma";
        string[] parts = csv.Split(",");
        Console.WriteLine("parts: " + parts.Length);
        foreach (var part in parts)
        {
            Console.WriteLine(" - " + part.Trim().ToUpper());
        }
    }
}
