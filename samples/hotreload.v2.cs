using System;

// Second generation used by:
//   sharpscript reload samples/hotreload.cs samples/hotreload.v2.cs
// The prefix changed, an extra field was added, and a new global appeared.
// 'views' and the greeting counter survive the reload.
class Greeter
{
    public string prefix = "v2";
    public int greeted = 0;
    public string suffix = "!";

    public string Greet(string name)
    {
        greeted += 1;
        return prefix + " hello, " + name + " (#" + greeted + ")" + suffix;
    }
}

class ReloadProgram
{
    public static int views = 0;
    public static int revision = 2;
    public static Greeter greeter = new Greeter();

    public static void Main()
    {
        views += 1;
        Console.WriteLine(greeter.Greet("world") + " | views = " + views + " | revision = " + revision);
    }
}
