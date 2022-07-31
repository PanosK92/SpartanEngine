using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Spartan
{
    using ObjectHandle = UInt64;

    public enum DebugType
    {
        Info    = 0,
        Warning = 1,
        Error   = 2
    };

    public enum KeyCode
    {
        // Keyboard
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,/*Function*/
        Alpha0, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,/*Numbers*/
        Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,/*Numpad*/
        Q, W, E, R, T, Y, U, I, O, P,/*Letters*/
        A, S, D, F, G, H, J, K, L,
        Z, X, C, V, B, N, M,
        Esc,/*Controls*/
        Tab,
        Shift_Left, Shift_Right,
        Ctrl_Left, Ctrl_Right,
        Alt_Left, Alt_Right,
        Space,
        CapsLock,
        Backspace,
        Enter,
        Delete,
        Arrow_Left, Arrow_Right, Arrow_Up, Arrow_Down,
        Page_Up, Page_Down,
        Home,
        End,
        Insert,

        // Mouse
        Click_Left,
        Click_Middle,
        Click_Right,

        // Gamepad
        DPad_Up,
        DPad_Down,
        DPad_Left,
        DPad_Right,
        Button_A,
        Button_B,
        Button_X,
        Button_Y,
        Start,
        Back,
        Left_Thumb,
        Right_Thumb,
        Left_Shoulder,
        Right_Shoulder
    };

    public class Debug
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Log(string text, DebugType type = DebugType.Info);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Log(float value, DebugType type = DebugType.Info);
    }

    public struct Vector2
    {
        public Vector2(float x, float y)
        {
            this.x = x;
            this.y = y;
        }

        public float x;
        public float y;
    }

    public struct Vector3
    {
        public Vector3(float x, float y, float z)
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        public float x;
        public float y;
        public float z;
    }

    public class Input
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool GetKey(KeyCode key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool GetKeyDown(KeyCode key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool GetKeyUp(KeyCode key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern Vector2 GetMousePosition();

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern Vector2 GetMouseDelta();

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern float GetMouseWheelDelta();
    }

    public class World
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Load(string file_path);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Save(string file_path);
    }

    public class Entity
    {
        public Entity(ObjectHandle handle)
        {
            this.handle = handle;
        }

        private ObjectHandle handle;
    }

    public class Transform
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 _internal_GetPosition(ObjectHandle handle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void _internal_SetPosition(ObjectHandle handle, Vector3 position);

        private ObjectHandle handle;

        public Transform(ObjectHandle handle)
        {
            this.handle = handle;
        }

        public Vector3 GetPosition()                { return _internal_GetPosition(handle); }
        public void SetPosition(Vector3 position)   { _internal_SetPosition(handle, position); }
    }

    public class SpartanClass
    {
        public SpartanClass()
        {
            entity      = new Entity(_internal_entity_handle);
            transform   = new Transform(_internal_transform_handle);
        }

        private ObjectHandle _internal_entity_handle;
        private ObjectHandle _internal_transform_handle;

        public Entity entity;
        public Transform transform;
    }
}
