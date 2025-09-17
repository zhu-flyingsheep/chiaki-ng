using System;
using System.Runtime.InteropServices;

namespace ChiakiQuitCallbackExample
{
    // 基于退出回调的 Chiaki 会话管理示例
    public class ChiakiSessionManager
    {
        // 退出回调委托
        public delegate void QuitCallback(int quit_reason, IntPtr reason_string, IntPtr user_data);

        // 导入 C 函数
        [DllImport("chiakilibwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int start_session(
            string host, string rp_key, string rp_regist_key, int target, 
            IntPtr log, QuitCallback quit_callback, IntPtr user_data);

        [DllImport("chiakilibwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern bool wakeup_ps(
            string host, string regist_key, bool ps5, IntPtr log);

        [DllImport("chiakilibwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr regist_ps(
            string host, string psn_id, string pin, string cpin, bool broadcast, 
            int target, IntPtr regist_cb, IntPtr log, IntPtr cb_user);

        // 事件定义
        public event EventHandler<SessionQuitEventArgs> SessionQuit;

        // 事件参数类
        public class SessionQuitEventArgs : EventArgs
        {
            public int QuitReason { get; set; }
            public string ReasonString { get; set; }
            public string QuitReasonDescription { get; set; }
        }

        // 退出原因枚举
        public enum QuitReason
        {
            CTRL_UNKNOWN = 0,
            CTRL_QUIT = 1,
            CTRL_ABORT = 2,
            STREAM_CONNECTION_REMOTE_DISCONNECTED = 3,
            STREAM_CONNECTION_NETWORK_ERROR = 4,
            LOGIN_PIN_REQUEST_TIMEOUT = 5,
            REGIST_KEY_TIMEOUT = 6,
            NETWORK_ERROR = 7,
            UNKNOWN = 8
        }

        // 单例模式
        private static ChiakiSessionManager _instance;
        private static readonly object _lock = new object();

        public static ChiakiSessionManager Instance
        {
            get
            {
                lock (_lock)
                {
                    if (_instance == null)
                    {
                        _instance = new ChiakiSessionManager();
                    }
                    return _instance;
                }
            }
        }

        private ChiakiSessionManager()
        {
            lock (_lock)
            {
                if (_instance == null)
                {
                    _instance = this;
                }
            }
        }

        // 开始会话
        public int StartSession(string host, string rpKey, string rpRegistKey, int target, IntPtr log)
        {
            // 创建回调函数
            QuitCallback quitCallback = OnQuitCallback;

            // 调用 C 函数，传入回调
            return start_session(host, rpKey, rpRegistKey, target, log, quitCallback, IntPtr.Zero);
        }

        // 唤醒设备
        public bool WakeupPs(string host, string registKey, bool ps5, IntPtr log)
        {
            return wakeup_ps(host, registKey, ps5, log);
        }

        // 注册设备
        public IntPtr RegistPs(string host, string psnId, string pin, string cpin, 
                              bool broadcast, int target, IntPtr registCb, IntPtr log, IntPtr cbUser)
        {
            return regist_ps(host, psnId, pin, cpin, broadcast, target, registCb, log, cbUser);
        }

        // C 回调函数 - 会话退出
        [MonoPInvokeCallback(typeof(QuitCallback))]
        private static void OnQuitCallback(int quit_reason, IntPtr reason_string, IntPtr user_data)
        {
            if (_instance != null)
            {
                var args = new SessionQuitEventArgs
                {
                    QuitReason = quit_reason,
                    ReasonString = Marshal.PtrToStringAnsi(reason_string) ?? "",
                    QuitReasonDescription = GetQuitReasonDescription(quit_reason)
                };

                _instance.SessionQuit?.Invoke(_instance, args);
            }
        }

        // 获取退出原因描述
        private static string GetQuitReasonDescription(int quitReason)
        {
            return quitReason switch
            {
                1 => "用户主动退出",
                2 => "会话异常中止", 
                3 => "PS设备主动断开连接",
                4 => "网络连接错误",
                5 => "PIN码请求超时",
                6 => "注册密钥超时",
                7 => "网络错误",
                _ => "未知原因"
            };
        }
    }

    // 使用示例
    class Program
    {
        private static ChiakiSessionManager sessionManager;

        static void Main(string[] args)
        {
            Console.WriteLine("Chiaki 会话退出回调示例");
            Console.WriteLine("=========================");

            // 创建会话管理器
            sessionManager = ChiakiSessionManager.Instance;

            // 注册退出事件处理
            sessionManager.SessionQuit += OnSessionQuit;

            // 示例：唤醒设备
            Console.WriteLine("尝试唤醒 PS 设备...");
            bool wakeupResult = sessionManager.WakeupPs("192.168.1.100", "1234567890abcdef", true, IntPtr.Zero);
            Console.WriteLine($"唤醒结果: {wakeupResult}");

            // 示例：开始会话
            Console.WriteLine("开始远程播放会话...");
            int sessionResult = sessionManager.StartSession("192.168.1.100", "rpkey", "regkey", 1, IntPtr.Zero);
            Console.WriteLine($"会话开始结果: {sessionResult}");

            if (sessionResult == 0)
            {
                Console.WriteLine("会话已启动，等待事件...");
                Console.WriteLine("当PS设备停电、网络断开或其他异常时，会自动停止帧处理并触发退出事件。");
            }

            Console.WriteLine("按任意键退出...");
            Console.ReadKey();
        }

        private static void OnSessionQuit(object sender, ChiakiSessionManager.SessionQuitEventArgs e)
        {
            Console.WriteLine($"[会话退出] {DateTime.Now:HH:mm:ss}");
            Console.WriteLine($"  退出原因: {e.QuitReason} - {e.QuitReasonDescription}");
            if (!string.IsNullOrEmpty(e.ReasonString))
            {
                Console.WriteLine($"  详细信息: {e.ReasonString}");
            }

            // 根据退出原因采取不同措施
            HandleSessionQuit((ChiakiSessionManager.QuitReason)e.QuitReason);
        }

        private static void HandleSessionQuit(ChiakiSessionManager.QuitReason quitReason)
        {
            switch (quitReason)
            {
                case ChiakiSessionManager.QuitReason.STREAM_CONNECTION_REMOTE_DISCONNECTED:
                    Console.WriteLine("  处理方案: PS设备断开连接，尝试重新连接...");
                    // 可以在这里实现自动重连逻辑
                    break;
                case ChiakiSessionManager.QuitReason.STREAM_CONNECTION_NETWORK_ERROR:
                case ChiakiSessionManager.QuitReason.NETWORK_ERROR:
                    Console.WriteLine("  处理方案: 网络错误，检查网络连接...");
                    break;
                case ChiakiSessionManager.QuitReason.CTRL_QUIT:
                    Console.WriteLine("  处理方案: 用户主动退出，正常结束");
                    break;
                case ChiakiSessionManager.QuitReason.LOGIN_PIN_REQUEST_TIMEOUT:
                    Console.WriteLine("  处理方案: PIN码请求超时，需要重新输入PIN码");
                    break;
                default:
                    Console.WriteLine("  处理方案: 未知退出原因，记录日志并通知用户");
                    break;
            }
        }
    }
}
