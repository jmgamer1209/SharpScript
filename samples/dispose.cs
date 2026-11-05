using System;

// SharpScript demo: `using` statements and IDisposable.
// The resource is disposed when the block ends, also when the body throws.
// This file is also valid C#.

class Resource : IDisposable
{
    private string name;

    public Resource(string name)
    {
        this.name = name;
        Console.WriteLine("open " + name);
    }

    public string Name
    {
        get { return name; }
    }

    public void Dispose()
    {
        Console.WriteLine("close " + name);
    }
}

class DisposeProgram
{
    public static void Main()
    {
        using (Resource first = new Resource("first"))
        {
            Console.WriteLine("using " + first.Name);
        }

        // `using` desugars into try/finally, so the exception still closes it.
        try
        {
            using (Resource second = new Resource("second"))
            {
                Console.WriteLine("using " + second.Name);
                throw new InvalidOperationException("boom");
            }
        }
        catch (InvalidOperationException e)
        {
            Console.WriteLine("caught: " + e.Message);
        }

        // A null resource is legal: `using` checks for null before disposing.
        Resource none = null;
        using (Resource third = none)
        {
            Console.WriteLine("body runs even for a null resource");
        }

        Console.WriteLine("done");
    }
}
