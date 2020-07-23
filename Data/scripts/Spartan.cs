using System.Runtime.CompilerServices;

namespace Spartan
{
    public enum LogType
    {
        Info,
        Warning,
        Error
    };

    public class Debug
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Log(float delta_time);
    }
}
