using System;

// ---------------------------------------------------------------- List<T>
// C# scripts reach for `List<T>`; it is `Array<T>` plus `Remove(T)`, so the two
// names share one implementation instead of duplicating the growth algorithm.
class List<T>
{
    private Array<T> items;

    public List()
    {
        this.items = new Array<T>();
    }

    public int Count
    {
        get { return items.Count; }
    }

    public void Reserve(int capacity)
    {
        items.Reserve(capacity);
    }

    public void Add(T value)
    {
        items.Add(value);
    }

    public void AddRange(T[] values)
    {
        for (int i = 0; i < values.Length; i = i + 1) items.Add(values[i]);
    }

    public void Insert(int index, T value)
    {
        items.Insert(index, value);
    }

    public void RemoveAt(int index)
    {
        items.RemoveAt(index);
    }

    public bool Remove(T value)
    {
        int index = items.IndexOf(value);
        if (index < 0) return false;
        items.RemoveAt(index);
        return true;
    }

    public void Clear()
    {
        items.Clear();
    }

    public int IndexOf(T value)
    {
        return items.IndexOf(value);
    }

    public bool Contains(T value)
    {
        return items.Contains(value);
    }

    public T Get(int index)
    {
        return items.Get(index);
    }

    public void Set(int index, T value)
    {
        items.Set(index, value);
    }

    public void ForEach(Action<T> action)
    {
        items.ForEach(action);
    }

    public T[] ToArray()
    {
        return items.ToArray();
    }

    public ListEnumerator<T> GetEnumerator()
    {
        return new ListEnumerator<T>(this);
    }
}

class ListEnumerator<T>
{
    private List<T> items;
    private int index = -1;

    public ListEnumerator(List<T> items)
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
