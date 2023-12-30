using System.Runtime.InteropServices;

namespace Scripting.SDK;

public static class Engine
{
    [UnmanagedCallersOnly]
    public static void Initialize()
    {
        System.IO.File.WriteAllText("./Initialize.log", "I am starting the SDK");
    }

    [UnmanagedCallersOnly]
    public static void Tick()
    {
        System.IO.File.WriteAllText("./Tick.log", "I am Ticking from the SDK");
    }

    [UnmanagedCallersOnly]
    public static void Shutdown()
    {
        System.IO.File.WriteAllText("./Shutdown.log", "I am shutting down from the SDK");
    }
}
