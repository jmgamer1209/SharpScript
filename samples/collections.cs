using System;

// SharpScript demo: the script standard library collections
// (Array<T> / List<T> / Dictionary<K,V>) and their foreach enumerators.
// This file is also valid C#.

class Student
{
    public string name;
    public int score;

    public Student(string name, int score)
    {
        this.name = name;
        this.score = score;
    }
}

class CollectionProgram
{
    public static void Main()
    {
        // List<T> is Array<T> plus Remove(T).
        List<string> names = new List<string>();
        names.Add("ann");
        names.Add("bob");
        names.AddRange(new string[] { "cid", "dee" });
        names.Insert(1, "eve");
        Console.WriteLine("removed bob: " + names.Remove("bob"));
        Console.WriteLine("names: " + names.Count);
        foreach (var name in names)
        {
            Console.WriteLine(" - " + name);
        }

        // Array<T> keeps the original name and API.
        Array<Student> students = new Array<Student>();
        students.Add(new Student("ann", 90));
        students.Add(new Student("bob", 75));
        Console.WriteLine("first student: " + students.Get(0).name);
        Console.WriteLine("bob index: " + students.IndexOf(students.Get(1)));

        // Dictionary<K,V>: Add throws on duplicates, Set inserts or overwrites.
        Dictionary<string, int> scores = new Dictionary<string, int>();
        scores.Add("ann", 90);
        scores.Add("bob", 75);
        scores.Set("cid", 88);
        scores.Set("ann", 95);
        Console.WriteLine("scores: " + scores.Count);
        Console.WriteLine("ann = " + scores.Get("ann"));

        int score = 0;
        if (scores.TryGetValue("cid", out score))
        {
            Console.WriteLine("cid = " + score);
        }
        Console.WriteLine("has dee: " + scores.ContainsKey("dee"));
        Console.WriteLine("dee found: " + scores.TryGetValue("dee", out score));

        // foreach over a dictionary yields KeyValuePair<K,V> in insertion order.
        foreach (var pair in scores)
        {
            Console.WriteLine(pair.Key + " -> " + pair.Value);
        }

        // Keys / Values are snapshots.
        string[] keys = scores.Keys;
        int[] values = scores.Values;
        Console.WriteLine("keys: " + keys.Length + ", values: " + values.Length);

        Console.WriteLine("removed bob: " + scores.Remove("bob"));
        Console.WriteLine("scores: " + scores.Count);

        // Nested generics need '> >' (the lexer reads '>>' as a shift).
        Dictionary<string, List<int> > groups = new Dictionary<string, List<int> >();
        List<int> primes = new List<int>();
        primes.Add(2);
        primes.Add(3);
        groups.Add("primes", primes);
        groups.Get("primes").Add(5);
        Console.WriteLine("primes: " + groups.Get("primes").Count);

        List<int> squares = new List<int>();
        for (int i = 1; i <= 5; i++)
        {
            squares.Add(i * i);
        }
        int total = 0;
        foreach (var value in squares)
        {
            total += value;
        }
        Console.WriteLine("sum of squares: " + total);

        scores.Clear();
        Console.WriteLine("cleared: " + scores.Count);
    }
}
