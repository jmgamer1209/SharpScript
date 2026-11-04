// SharpScript demo: calling into C# (reflection bound host API)
void main()
{
    print("sqrt(2) =", Math.Sqrt(2.0));
    print("max(3, 8) =", Math.Max(3, 8));
    print("floor(2.9) =", Math.Floor(2.9));

    Host.Log("hello from the C# host");
    print("host time:", Host.Now());
    print("ticks:", Host.GetTickCount());

    string csv = "alpha,beta,gamma";
    string[] parts = csv.Split(",");
    print("split parts:", parts.Length);
    foreach (var part in parts)
    {
        print(" -", part);
    }
}
