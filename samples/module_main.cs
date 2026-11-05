using System;

// SharpScript demo: a module built from two files (sections share one global
// namespace, so the declarations of both files are visible everywhere).
// Run: sharpscript_cli run samples/module_main.cs samples/module_player.cs
// This file is also valid C#.

class ModuleProgram
{
    public static void Main()
    {
        PlayerState hero = new PlayerState("hero");
        PlayerState boss = new PlayerState("boss");

        Console.WriteLine(hero.Describe());
        hero.Damage(30);
        Console.WriteLine(hero.Describe());
        Console.WriteLine(boss.Describe());
        Console.WriteLine("alive: " + (hero.hp > 0));
    }
}
