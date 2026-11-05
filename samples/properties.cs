using System;

// SharpScript demo: auto properties, explicit accessors, computed properties,
// read-only properties, static properties and virtual/override properties.
// This file is also valid C#.

class Temperature
{
    private double celsius;

    public double Celsius
    {
        get { return celsius; }
        set { celsius = value; }
    }

    // Computed property with both accessors.
    public double Fahrenheit
    {
        get { return celsius * 9.0 / 5.0 + 32.0; }
        set { celsius = (value - 32.0) * 5.0 / 9.0; }
    }
}

class Person
{
    // Auto properties: the compiler adds the backing fields.
    public string Name { get; set; }
    public int Age { get; set; }

    // Read-only property over a private field with an initializer.
    private string id = "p-42";

    public string Id
    {
        get { return id; }
    }
}

// Properties can be virtual and overridden like methods.
class Node
{
    public virtual string Kind
    {
        get { return "node"; }
    }
}

class Leaf : Node
{
    public override string Kind
    {
        get { return "leaf"; }
    }
}

class Registry
{
    private static int created = 0;

    public static int Created
    {
        get { return created; }
    }

    public static void Touch()
    {
        created += 1;
    }
}

class PropertyProgram
{
    public static void Main()
    {
        Temperature temperature = new Temperature();
        temperature.Celsius = 100.0;
        Console.WriteLine("fahrenheit = " + temperature.Fahrenheit);

        temperature.Fahrenheit = 32.0;
        Console.WriteLine("celsius = " + temperature.Celsius);

        Person person = new Person();
        person.Name = "ann";
        person.Age = 30;
        Console.WriteLine(person.Name + " (" + person.Id + ") is " + person.Age);

        Node node = new Node();
        Node leaf = new Leaf();
        Console.WriteLine("node kind: " + node.Kind + ", leaf kind: " + leaf.Kind);

        Registry.Touch();
        Registry.Touch();
        Console.WriteLine("created = " + Registry.Created);
    }
}
