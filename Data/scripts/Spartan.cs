using System;
using System.Runtime.CompilerServices;

namespace Spartan
{
    public enum DebugType
    {
        Info    = 0,
        Warning = 1,
        Error   = 2
    };

    public class Debug
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Log(string text, DebugType type = DebugType.Info);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Log(float value, DebugType type = DebugType.Info);
    }
}
