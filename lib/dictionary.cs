using System;

// ------------------------------------------------------------ Dictionary<K,V>
// `foreach (var pair in dictionary)` yields `KeyValuePair<K,V>`. The BCL type is
// a struct; this one is a script class (structs live here only when needed).
class KeyValuePair<K, V>
{
    private K key;
    private V value;

    public KeyValuePair(K key, V value)
    {
        this.key = key;
        this.value = value;
    }

    public K Key
    {
        get { return key; }
    }

    public V Value
    {
        get { return value; }
    }
}

// Separate chaining over parallel arrays: `buckets` holds 1-based indices into
// `keys`/`values` (0 = empty) and `next` links the entries of one bucket. Entries
// stay in insertion order, so Count/EntryAt/enumeration are plain index loops.
// Keys are hashed with `GetHashCode()` and compared with `Equals(...)`, exactly
// like Array<T> compares elements; add/remove may rehash but never leak entries.
class Dictionary<K, V>
{
    private int[] buckets;
    private int[] next;
    private K[] keys;
    private V[] values;
    private int used = 0;
    // Default value of V, reported by TryGetValue when the key is missing: the
    // language has no `default(V)` expression, so an array slot supplies it.
    private V empty;

    public Dictionary()
    {
        this.buckets = new int[4];
        this.next = new int[4];
        this.keys = new K[4];
        this.values = new V[4];
        this.empty = this.values[0];
    }

    public int Count
    {
        get { return used; }
    }

    public K[] Keys
    {
        get
        {
            K[] result = new K[used];
            for (int i = 0; i < used; i = i + 1) result[i] = keys[i];
            return result;
        }
    }

    public V[] Values
    {
        get
        {
            V[] result = new V[used];
            for (int i = 0; i < used; i = i + 1) result[i] = values[i];
            return result;
        }
    }

    private int Hash(K key)
    {
        return key.GetHashCode() & 2147483647;
    }

    private int IndexOf(K key)
    {
        int bucket = Hash(key) % buckets.Length;
        for (int i = buckets[bucket]; i != 0; i = next[i - 1])
        {
            if (Equals(keys[i - 1], key)) return i - 1;
        }
        return -1;
    }

    public bool ContainsKey(K key)
    {
        return IndexOf(key) >= 0;
    }

    public void Add(K key, V value)
    {
        if (IndexOf(key) >= 0) throw new ArgumentException("duplicate key");
        if (used >= keys.Length) Grow();
        int bucket = Hash(key) % buckets.Length;
        keys[used] = key;
        values[used] = value;
        next[used] = buckets[bucket];
        buckets[bucket] = used + 1;
        used = used + 1;
    }

    public void Set(K key, V value)
    {
        int index = IndexOf(key);
        if (index >= 0)
        {
            values[index] = value;
            return;
        }
        Add(key, value);
    }

    public V Get(K key)
    {
        int index = IndexOf(key);
        if (index < 0) throw new InvalidOperationException("key not found");
        return values[index];
    }

    public bool TryGetValue(K key, out V value)
    {
        int index = IndexOf(key);
        if (index >= 0)
        {
            value = values[index];
            return true;
        }
        value = empty;
        return false;
    }

    public bool Remove(K key)
    {
        int index = IndexOf(key);
        if (index < 0) return false;
        for (int i = index; i < used - 1; i = i + 1)
        {
            keys[i] = keys[i + 1];
            values[i] = values[i + 1];
        }
        used = used - 1;
        Rebuild();
        return true;
    }

    public void Clear()
    {
        used = 0;
        Rebuild();
    }

    public KeyValuePair<K, V> EntryAt(int index)
    {
        return new KeyValuePair<K, V>(keys[index], values[index]);
    }

    public DictionaryEnumerator<K, V> GetEnumerator()
    {
        return new DictionaryEnumerator<K, V>(this);
    }

    private void Grow()
    {
        int capacity = keys.Length * 2;
        K[] biggerKeys = new K[capacity];
        V[] biggerValues = new V[capacity];
        for (int i = 0; i < used; i = i + 1)
        {
            biggerKeys[i] = keys[i];
            biggerValues[i] = values[i];
        }
        keys = biggerKeys;
        values = biggerValues;
        next = new int[capacity];
        buckets = new int[capacity];
        Rebuild();
    }

    private void Rebuild()
    {
        for (int i = 0; i < buckets.Length; i = i + 1) buckets[i] = 0;
        for (int i = 0; i < used; i = i + 1)
        {
            int bucket = Hash(keys[i]) % buckets.Length;
            next[i] = buckets[bucket];
            buckets[bucket] = i + 1;
        }
    }
}

class DictionaryEnumerator<K, V>
{
    private Dictionary<K, V> dictionary;
    private int index = -1;

    public DictionaryEnumerator(Dictionary<K, V> dictionary)
    {
        this.dictionary = dictionary;
    }

    public bool MoveNext()
    {
        index = index + 1;
        return index < dictionary.Count;
    }

    public KeyValuePair<K, V> Current
    {
        get { return dictionary.EntryAt(index); }
    }
}
