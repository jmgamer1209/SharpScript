using System;

// SharpScript demo: `struct` value semantics (copy on assignment, field access,
// nesting, methods) and value-based equality through `Equals`.
// This file is also valid C#.

struct Vec2
{
    public double x;
    public double y;

    public Vec2(double x, double y)
    {
        this.x = x;
        this.y = y;
    }

    public double Length()
    {
        return Math.Sqrt(x * x + y * y);
    }

    public Vec2 Plus(Vec2 other)
    {
        return new Vec2(x + other.x, y + other.y);
    }
}

// A struct may contain other structs: copying is recursive.
struct Box
{
    public Vec2 min;
    public Vec2 max;

    public Box(Vec2 min, Vec2 max)
    {
        this.min = min;
        this.max = max;
    }

    public Vec2 Size()
    {
        return new Vec2(max.x - min.x, max.y - min.y);
    }
}

class StructProgram
{
    public static void Main()
    {
        Vec2 a = new Vec2(3.0, 4.0);
        Vec2 b = a;                 // value copy
        b.x = 100.0;
        Console.WriteLine("a.x = " + a.x);
        Console.WriteLine("b.x = " + b.x);
        Console.WriteLine("a length = " + a.Length());

        Vec2 sum = a.Plus(new Vec2(1.0, 1.0));
        Console.WriteLine("sum = " + sum.x + "," + sum.y);

        // Taking a struct out of an array copies it as well.
        Vec2[] points = new Vec2[2];
        points[0] = a;
        points[1] = sum;
        Vec2 first = points[0];
        first.x = 7.0;
        Console.WriteLine("points[0].x = " + points[0].x);
        Console.WriteLine("first.x = " + first.x);

        Box box = new Box(new Vec2(1.0, 2.0), new Vec2(5.0, 8.0));
        Console.WriteLine("box size = " + box.Size().x + "," + box.Size().y);

        // Nested struct copies are deep: editing the copy keeps the original.
        Box moved = box;
        moved.min.x = -1.0;
        Console.WriteLine("box.min.x = " + box.min.x);
        Console.WriteLine("moved.min.x = " + moved.min.x);

        // `Equals(...)` compares structs field by field (C#'s object.Equals).
        Console.WriteLine("a equals a copy: " + Equals(a, a));
        Console.WriteLine("a equals sum: " + Equals(a, sum));
    }
}
