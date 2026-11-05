using System;

// ---------------------------------------------------------------- Array<T>
// Script-side standard library, also valid C# so it can be compiled by Roslyn.
//
// `Array<T>` only relies on the storage primitives of `T[]` (`new T[4]`,
// `items[i]`, `items.Length`); every algorithm is expressed in script, so the VM
// needs no collection opcodes. This file is embedded at build time and added to
// every module as its own section.
class Array<T>
{
    private T[] items;
    private int used = 0;

    public Array()
    {
        this.items = new T[4];
    }

    public int Count
    {
        get { return used; }
    }

    public void Reserve(int capacity)
    {
        if (capacity <= items.Length) return;
        T[] bigger = new T[capacity];
        for (int i = 0; i < used; i = i + 1) bigger[i] = items[i];
        items = bigger;
    }

    private void Grow()
    {
        int capacity = items.Length * 2;
        if (capacity < 4) capacity = 4;
        T[] bigger = new T[capacity];
        for (int i = 0; i < used; i = i + 1) bigger[i] = items[i];
        items = bigger;
    }

    public void Add(T value)
    {
        if (used >= items.Length) Grow();
        items[used] = value;
        used = used + 1;
    }

    public void Insert(int index, T value)
    {
        if (index < 0 || index > used) throw new ArgumentException("index");
        if (used >= items.Length) Grow();
        for (int i = used; i > index; i = i - 1) items[i] = items[i - 1];
        items[index] = value;
        used = used + 1;
    }

    public void RemoveAt(int index)
    {
        if (index < 0 || index >= used) throw new ArgumentException("index");
        for (int i = index; i < used - 1; i = i + 1) items[i] = items[i + 1];
        used = used - 1;
    }

    public void Clear()
    {
        used = 0;
    }

    public int IndexOf(T value)
    {
        for (int i = 0; i < used; i = i + 1)
        {
            if (Equals(items[i], value)) return i;
        }
        return -1;
    }

    public bool Contains(T value)
    {
        return IndexOf(value) >= 0;
    }

    public T Get(int index)
    {
        if (index < 0 || index >= used) throw new ArgumentException("index");
        return items[index];
    }

    public void Set(int index, T value)
    {
        if (index < 0 || index >= used) throw new ArgumentException("index");
        items[index] = value;
    }

    public void ForEach(Action<T> action)
    {
        for (int i = 0; i < used; i = i + 1) action(items[i]);
    }

    public ArrayEnumerator<T> GetEnumerator()
    {
        return new ArrayEnumerator<T>(this);
    }

    public T[] ToArray()
    {
        T[] copy = new T[used];
        for (int i = 0; i < used; i = i + 1) copy[i] = items[i];
        return copy;
    }
}

// `foreach (var x in list)` uses the C# enumerator pattern.
class ArrayEnumerator<T>
{
    private Array<T> items;
    private int index = -1;

    public ArrayEnumerator(Array<T> items)
    {
        this.items = items;
    }

    public bool MoveNext()
    {
        index = index + 1;
        return index < items.Count;
    }

    public T Current
    {
        get { return items.Get(index); }
    }
}