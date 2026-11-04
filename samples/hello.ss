// SharpScript demo: basics
void main()
{
    print("Hello from SharpScript!");

    int a = 7;
    int b = 6;
    print("7 * 6 =", a * b);

    string text = "SharpScript";
    print("length:", text.Length, "upper:", text.ToUpper());

    double ratio = 3.5;
    print("ratio * 2 =", ratio * 2);

    int[] numbers = new int[] { 5, 3, 9, 1 };
    int total = 0;
    foreach (var n in numbers)
    {
        total += n;
    }
    print("total:", total);

    for (int i = 0; i < 3; i++)
    {
        print("for", i);
    }
}
