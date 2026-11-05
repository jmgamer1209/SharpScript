using System;

// SharpScript demo: try / catch / finally / throw, typed catches, rethrow-free
// nesting and runtime errors that surface as catchable script exceptions.
// This file is also valid C#.

class Account
{
    private int balance;

    public Account(int balance)
    {
        this.balance = balance;
    }

    public int Balance
    {
        get { return balance; }
    }

    public void Withdraw(int amount)
    {
        if (amount < 0)
        {
            throw new ArgumentException("negative amount");
        }
        if (amount > balance)
        {
            throw new InvalidOperationException("insufficient funds");
        }
        balance -= amount;
    }
}

class ExceptionProgram
{
    private static int attempts = 0;

    public static void Try(string label, Account account, int amount)
    {
        attempts += 1;
        try
        {
            account.Withdraw(amount);
            Console.WriteLine(label + ": ok, balance " + account.Balance);
        }
        catch (ArgumentException e)
        {
            Console.WriteLine(label + ": bad request (" + e.Message + ")");
        }
        catch (InvalidOperationException e)
        {
            Console.WriteLine(label + ": refused (" + e.Message + ")");
        }
        finally
        {
            Console.WriteLine(label + ": done");
        }
    }

    public static void Main()
    {
        Account account = new Account(100);
        Try("normal", account, 30);
        Try("too much", account, 500);
        Try("negative", account, -5);

        // finally runs before the exception reaches the outer catch.
        try
        {
            try
            {
                throw new Exception("inner");
            }
            finally
            {
                Console.WriteLine("inner finally");
            }
        }
        catch (Exception e)
        {
            Console.WriteLine("outer catch: " + e.Message);
        }

        // Out-of-range array access becomes a catchable script exception.
        try
        {
            int[] numbers = new int[2];
            Console.WriteLine(numbers[5]);
        }
        catch (IndexOutOfRangeException e)
        {
            Console.WriteLine("caught: " + e.Message);
        }

        Console.WriteLine("attempts: " + attempts);
    }
}
