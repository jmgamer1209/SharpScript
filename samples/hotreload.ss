// SharpScript demo: hot reload with state migration.
// Run:  sharpscript watch samples/hotreload.ss
// While it is running, change the prefix below to "v2" and save the file.
// 'views' and the Greeter instance state survive the reload.

int views = 0;

class Greeter
{
    public string prefix = "v1";
    public int greeted = 0;

    public string greet(string name)
    {
        greeted += 1;
        return prefix + " hello, " + name + " (#" + greeted + ")";
    }
}

Greeter greeter = new Greeter();

void main()
{
    views += 1;
    print(greeter.greet("world"), "| views =", views);
}
