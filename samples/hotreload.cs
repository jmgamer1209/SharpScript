using System;

// SharpScript demo: hot reload with state migration.
// Run:  sharpscript watch samples/hotreload.cs
// While it is running, change the prefix below to "v2" and save the file.
// 'views' and the Greeter instance state survive the reload.
class Greeter
{
    public string prefix = "v1";
    public int greeted = 0;

    public string Greet(string name)
    {
        greeted += 1;
        return prefix + " hello, " + name + " (#" + greeted + ")";
    }
}

class ReloadProgram
{
    public static int views = 0;
    public static Greeter greeter = new Greeter();

    public static void Main()
    {
        views += 1;
        Console.WriteLine(greeter.Greet("world") + " | views = " + views);
    }
}
