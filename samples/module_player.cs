using System;

// SharpScript demo: the second section of the multi-file module. It only needs
// to be passed on the command line; there is no import statement.
// This file is also valid C#.

class PlayerState
{
    public string name;
    public int hp;

    public PlayerState(string name)
    {
        this.name = name;
        this.hp = 100;
    }

    public void Damage(int amount)
    {
        hp -= amount;
        if (hp < 0)
        {
            hp = 0;
        }
    }

    public string Describe()
    {
        return name + " (" + hp + " hp)";
    }
}
