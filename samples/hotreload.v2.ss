// Second generation used by:  sharpscript reload samples/hotreload.ss samples/hotreload.v2.ss
// The prefix changed, an extra field was added, and a new global appeared.
// 'views' and the greeting counter survive the reload.

int views = 0;
int revision = 2;

class Greeter
{
    public string prefix = "v2";
    public int greeted = 0;
    public string suffix = "!";

    public string greet(string name)
    {
        greeted += 1;
        return prefix + " hello, " + name + " (#" + greeted + ")" + suffix;
    }
}

Greeter greeter = new Greeter();

void main()
{
    views += 1;
    print(greeter.greet("world"), "| views =", views, "| revision =", revision);
}
