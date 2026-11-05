using System;

// SharpScript demo: classes, inheritance and virtual dispatch.
// This file is also valid C#.
class Shape
{
    public string name = "shape";

    public virtual double Area()
    {
        return 0.0;
    }

    public string Describe()
    {
        return name + " area=" + Area();
    }
}

class Circle : Shape
{
    public double radius;

    public Circle(double radius)
    {
        this.radius = radius;
        name = "circle";
    }

    public override double Area()
    {
        return 3.141592653589793 * radius * radius;
    }
}

class Rect : Shape
{
    public double width;
    public double height;

    public Rect(double w, double h)
    {
        width = w;
        height = h;
        name = "rect";
    }

    public override double Area()
    {
        return width * height;
    }
}

class OopProgram
{
    public static void Main()
    {
        Shape[] shapes = new Shape[3];
        shapes[0] = new Circle(1.0);
        shapes[1] = new Rect(2.0, 3.0);
        shapes[2] = new Circle(2.0);

        double total = 0.0;
        foreach (var shape in shapes)
        {
            Console.WriteLine(shape.Describe());
            total += shape.Area();
        }
        Console.WriteLine("total area = " + total);

        Shape first = shapes[0];
        Console.WriteLine("first is Circle: " + (first is Circle));
        Console.WriteLine("first is Rect: " + (first is Rect));

        Circle copy = first as Circle;
        Console.WriteLine("copy radius: " + copy.radius);
    }
}
