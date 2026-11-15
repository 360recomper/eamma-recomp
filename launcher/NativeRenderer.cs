using System.Diagnostics;
using System.Text;
using EAMMALauncher.Tabs;

namespace EAMMALauncher;

/// <summary>The game's renderer. Native-only, like UFC's launcher (its FINDINGS-native-only.md): every Play runs the
/// native D3D12 renderer. There is no Renderer choice and no fallback to the emulated GPU: when the shader cache can't be
/// built or the native device can't be created, the player gets <see cref="RendererError"/> (what failed + what to do).
/// eamma.exe's own default is still the emulated renderer (cvar eamma_renderer, kept for development runs), so the launcher
/// passes --eamma_renderer=native on every start; no launcher path starts the emulated one.</summary>
public static class NativeRenderer
{
    /// <summary>eamma.exe exit codes (src/eamma_renderer_fail.h): the native device can't be created / no shader cache.
    /// The details are in native-error.txt beside the exe.</summary>
    public const int DeviceFailedExitCode = 3;
    public const int CacheMissingExitCode = 4;

    /// <summary>What Play adds to eamma.exe's command line.</summary>
    public static readonly string[] GameArgs = { "--eamma_renderer=native" };

    // Logged only by builds with the native hooks (EAMMA_NATIVE_HOOKS, src/eamma_app.h LatchRenderer).
    static readonly byte[] Marker = Encoding.ASCII.GetBytes("native: renderer on (experimental");

    static string? stamp;
    static bool available;

    /// <summary>True when eamma.exe has the native renderer compiled in (every release build). Scans the exe once per size + date.</summary>
    public static bool Available
    {
        get
        {
            try
            {
                var fi = new FileInfo(Paths.GameExe);
                if (!fi.Exists) return false;
                var s = $"{fi.Length}:{fi.LastWriteTimeUtc.Ticks}";
                if (s != stamp) { available = ExeHasMarker(fi.FullName); stamp = s; }
                return available;
            }
            catch { return false; }
        }
    }

    static bool ExeHasMarker(string exe)
    {
        using var f = new FileStream(exe, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 1 << 16);
        var buf = new byte[4 << 20];
        int keep = 0, n;
        while ((n = f.Read(buf, keep, buf.Length - keep)) > 0)
        {
            int len = keep + n;
            if (buf.AsSpan(0, len).IndexOf(Marker) >= 0) return true;
            // Keep the tail so a marker split across two reads is still found.
            keep = Math.Min(Marker.Length - 1, len);
            Array.Copy(buf, len - keep, buf, 0, keep);
        }
        return false;
    }

    /// <summary>Makes sure the shader cache is there (builds it if not). Null = ready, else the failed result.
    /// Blocks: call off the UI thread.</summary>
    public static ShaderCache.Result? PrepareCache(bool force = false)
    {
        var r = force ? ShaderCache.Build(Paths.Assets) : ShaderCache.EnsurePresent(Paths.Assets);
        return r.Ok ? null : r;
    }

    /// <summary>Starts eamma.exe (native). EAMMA_LAUNCHER=1 tells the game the launcher shows any startup error itself.</summary>
    public static Process Launch()
    {
        try { File.Delete(ErrorFile); } catch { }
        var exe = Cpu.GameExeForThisCpu();
        Log($"start: {Cpu.Describe()} {string.Join(' ', GameArgs)}");
        var psi = new ProcessStartInfo(exe) { WorkingDirectory = Paths.Root, UseShellExecute = false };
        foreach (var a in GameArgs) psi.ArgumentList.Add(a);
        psi.Environment[Branding.LauncherEnv] = "1";
        return Process.Start(psi) ?? throw new InvalidOperationException("eamma.exe did not start");
    }

    /// <summary>Starts the game and waits for it with the launcher hidden. If the native renderer can't start (exit code
    /// 3 or 4) the launcher comes back with <see cref="RendererError"/>; "Rebuild shader cache" rebuilds and starts again.
    /// Any other exit closes the launcher. Logged to launcher-native.log.</summary>
    public static void PlayFrom(Control from)
    {
        var game = Launch();
        var form = from.FindForm();
        form?.Hide();
        new Thread(() =>
        {
            int code = Wait(game);
            if (code != DeviceFailedExitCode && code != CacheMissingExitCode || form == null || !form.IsHandleCreated)
            {
                if (form != null && form.IsHandleCreated) form.BeginInvoke(Application.Exit); else Application.Exit();
                return;
            }
            form.BeginInvoke(new Action(async () =>
            {
                form.Show();
                if (RendererError.ShowGameFailure(code) != DialogResult.Retry) return;
                var failed = await Task.Run(() => PrepareCache(force: true));
                if (failed != null) { RendererError.ShowCacheFailure(failed); return; }
                PlayFrom(from);
            }));
        }) { IsBackground = false, Name = "native-wait" }.Start();
    }

    /// <summary>Waits for the game; returns its exit code (-1 if unknown). A renderer failure is logged.</summary>
    public static int Wait(Process game)
    {
        try
        {
            game.WaitForExit();
            int code = game.ExitCode;
            if (code == DeviceFailedExitCode || code == CacheMissingExitCode)
                Log($"game exit code {code}: the native renderer could not start ({ReadError().GetValueOrDefault("what", "no native-error.txt")})");
            return code;
        }
        catch (Exception ex) { Log("ERROR: " + ex.Message); return -1; }
    }

    /// <summary>native-error.txt beside the exe, written by the game when the renderer can't start.</summary>
    public static string ErrorFile => Path.Combine(Paths.Root, "native-error.txt");

    /// <summary>The "key: value" lines of <see cref="ErrorFile"/> (code, what, gpu, feature_level, log); empty when missing.</summary>
    public static Dictionary<string, string> ReadError()
    {
        var d = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        try
        {
            foreach (var l in File.ReadAllLines(ErrorFile))
            {
                int i = l.IndexOf(": ", StringComparison.Ordinal);
                if (i > 0) d[l[..i].Trim()] = l[(i + 2)..].Trim();
            }
        }
        catch { }
        return d;
    }

    public static string LogFile => Path.Combine(Paths.Root, "launcher-native.log");

    internal static void Log(string line)
    {
        try { File.AppendAllText(LogFile, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {line}{Environment.NewLine}"); }
        catch { }
    }
}
