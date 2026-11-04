// SharpScript demo: classes, inheritance and virtual dispatch
class Shape
{
    public string name = "shape";

    public virtual double area()
    {
        return 0.0;
    }

    public string describe()
    {
        return name + " area=" + area();
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

    public override double area()
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

    public override double area()
    {
        return width * height;
    }
}

void main()
{
    Shape[] shapes = new Shape[3];
    shapes[0] = new Circle(1.0);
    shapes[1] = new Rect(2.0, 3.0);
    shapes[2] = new Circle(2.0);

    double total = 0.0;
    foreach (var shape in shapes)
    {
        print(shape.describe());
        total += shape.area();
    }
    print("total area =", total);

    Shape first = shapes[0];
    print("first is Circle:", first is Circle);
    print("first is Rect:", first is Rect);

    Circle copy = first as Circle;
    print("copy radius:", copy.radius);
}
