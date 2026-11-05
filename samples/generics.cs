using System;

// SharpScript demo: generic classes, generic methods (monomorphized per call
// site), multi-parameter generics and nested generic instances.
// This file is also valid C#.

class Holder<T>
{
    private T value;

    public Holder(T value)
    {
        this.value = value;
    }

    public T Value
    {
        get { return value; }
    }

    public void Set(T value)
    {
        this.value = value;
    }
}

class Pair<TFirst, TSecond>
{
    public TFirst first;
    public TSecond second;

    public Pair(TFirst first, TSecond second)
    {
        this.first = first;
        this.second = second;
    }
}

class GenericProgram
{
    // Type parameters are inferred from the arguments...
    public static T Choose<T>(T whenTrue, T whenFalse, bool condition)
    {
        if (condition)
        {
            return whenTrue;
        }
        return whenFalse;
    }

    // ...or passed explicitly.
    public static T[] Repeat<T>(T value, int count)
    {
        T[] result = new T[count];
        for (int i = 0; i < count; i++)
        {
            result[i] = value;
        }
        return result;
    }

    public static int CountOf<T>(Array<T> items)
    {
        return items.Count;
    }

    public static void Main()
    {
        Holder<int> number = new Holder<int>(42);
        Console.WriteLine("number: " + number.Value);
        number.Set(7);
        Console.WriteLine("number: " + number.Value);

        Holder<string> text = new Holder<string>("boxed");
        Console.WriteLine("text: " + text.Value);

        Pair<string, int> entry = new Pair<string, int>("ann", 30);
        Console.WriteLine(entry.first + " = " + entry.second);

        Console.WriteLine("inferred: " + Choose(1, 2, false));
        Console.WriteLine("inferred: " + Choose("left", "right", true));
        Console.WriteLine("explicit: " + Choose<int>(3, 4, true));

        int[] threes = Repeat<int>(3, 4);
        int total = 0;
        foreach (var value in threes)
        {
            total += value;
        }
        Console.WriteLine("repeated total: " + total + " of " + threes.Length);

        Array<int> values = new Array<int>();
        values.Add(1);
        values.Add(2);
        Console.WriteLine("count: " + CountOf(values));

        // Nested generic instance: '> >' because '>>' is a shift operator.
        Holder<Holder<int> > nested = new Holder<Holder<int> >(number);
        Console.WriteLine("nested: " + nested.Value.Value);

        // A generic class can still be a normal field/parameter type.
        Holder<double> ratio = new Holder<double>(0.5);
        Console.WriteLine("ratio: " + ratio.Value);
    }
}
