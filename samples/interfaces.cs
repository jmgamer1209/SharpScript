using System;

// SharpScript demo: interfaces, interface inheritance, interface-typed storage
// and `is` / `as` against interfaces. This file is also valid C#.

interface IAnimal
{
    string Name { get; }
    string Speak();
}

// An interface may inherit another interface and add members.
interface IPet : IAnimal
{
    string Owner();
}

class Dog : IPet
{
    private string name;

    public Dog(string name)
    {
        this.name = name;
    }

    public string Name
    {
        get { return name; }
    }

    public string Speak()
    {
        return "woof";
    }

    public string Owner()
    {
        return "ana";
    }
}

class Cat : IAnimal
{
    private string name;

    public Cat(string name)
    {
        this.name = name;
    }

    public string Name
    {
        get { return name; }
    }

    public string Speak()
    {
        return "meow";
    }
}

class InterfaceProgram
{
    public static void Main()
    {
        // Interface-typed arrays hold any implementation; calls dispatch virtually.
        IAnimal[] animals = new IAnimal[3];
        animals[0] = new Dog("rex");
        animals[1] = new Cat("mimi");
        animals[2] = new Dog("fido");

        foreach (var animal in animals)
        {
            Console.WriteLine(animal.Name + " says " + animal.Speak());
        }

        IAnimal first = animals[0];
        IAnimal second = animals[1];

        Console.WriteLine("first is IPet: " + (first is IPet));
        Console.WriteLine("second is IPet: " + (second is IPet));

        IPet pet = first as IPet;
        Console.WriteLine("owner: " + pet.Owner());

        IPet missing = second as IPet;
        Console.WriteLine("missing is null: " + (missing == null));

        // A class implementing an interface can still expose its own members.
        Dog dog = first as Dog;
        Console.WriteLine("cast back to Dog: " + dog.Name);
    }
}
