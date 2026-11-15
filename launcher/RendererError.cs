using System.Diagnostics;
using EAMMALauncher.Tabs;

namespace EAMMALauncher;

/// <summary>The one error dialog for "the renderer can't start" (native-only). It says what failed (shader cache build:
/// the step, its exit code and the log; device: the GPU and its Direct3D 12 feature level from the game's
/// native-error.txt) and what to do: rebuild the shader cache, update the graphics driver, report it. There is no switch
/// to another renderer. "Rebuild shader cache" returns <see cref="DialogResult.Retry"/>.</summary>
public static class RendererError
{
    public const string Caption = Branding.LauncherTitle + " - the game can't start";

    /// <summary>Where problems are reported: the release repo's issue page. Null until there is one (Updater.Repo).</summary>
    public static string? ReportUrl => Updater.Repo.Length > 0 ? "https://github.com/" + Updater.Repo + "/issues/new" : null;

    public static string CacheText(ShaderCache.Result r) =>
        "The shader cache could not be built, so the game was not started.\n\n" +
        "What failed: " + r.Message + "\n" +
        (r.ExitCode is int c ? $"Exit code: {c}\n" : "") +
        "Log: " + NativeRenderer.LogFile + "\n\n" +
        "What to do:\n" +
        "  1. Press Rebuild shader cache to try again.\n" +
        "  2. If it fails again: check the game files (Setup tab) and that the game folder can be written to " +
        "(not inside Program Files), and that antivirus didn't remove " + ShaderCache.TranslateTool + " or " + ShaderCache.LinkTool + ".\n" +
        "  3. Still failing? Report it with the log file.";

    public static string GameText(int code, IReadOnlyDictionary<string, string> e)
    {
        string what = e.GetValueOrDefault("what") ?? (code == NativeRenderer.CacheMissingExitCode
            ? "the shader cache is missing" : "the Direct3D 12 device could not be created");
        string gpu = e.GetValueOrDefault("gpu") ?? FirstGpu() ?? "unknown";
        string level = e.GetValueOrDefault("feature_level") ?? "unknown";
        string log = e.GetValueOrDefault("log") is { Length: > 0 } l && l != "the game's log file" ? l : NativeRenderer.ErrorFile;
        return "The game's renderer could not start, so the game closed.\n\n" +
            "What failed: " + what + $" (exit code {code})\n" +
            "Graphics card: " + gpu + "\n" +
            "Direct3D 12 feature level: " + level + " (12_0 or newer is needed)\n" +
            "Details: " + log + "\n\n" +
            "What to do:\n" +
            "  1. Update your graphics driver (Update GPU driver opens your card maker's page), then press Play again.\n" +
            "  2. Laptop with two graphics cards: pick the other one in Settings > Graphics card.\n" +
            "  3. Rebuild shader cache rebuilds it and starts the game again.\n" +
            "  4. Still failing? Report it with native-error.txt and the log.";
    }

    /// <summary>A failed cache build (Play, Setup, --native). Retry = the player asked for a rebuild.</summary>
    public static DialogResult ShowCacheFailure(ShaderCache.Result r)
    {
        NativeRenderer.Log("error dialog: shader cache build failed: " + r.Message);
        return Show(CacheText(r), FirstGpu(), NativeRenderer.LogFile);
    }

    /// <summary>eamma.exe exited with 3 or 4: details from native-error.txt.</summary>
    public static DialogResult ShowGameFailure(int code)
    {
        var e = NativeRenderer.ReadError();
        return Show(GameText(code, e), e.GetValueOrDefault("gpu") ?? FirstGpu(),
            File.Exists(NativeRenderer.ErrorFile) ? NativeRenderer.ErrorFile : NativeRenderer.LogFile);
    }

    static DialogResult Show(string text, string? gpu, string logFile)
    {
        // launcher\tests\shader-cache-test.ps1: headless, the dialog's text goes to the log and it counts as Close.
        if (Environment.GetEnvironmentVariable("EAMMA_TEST_NO_DIALOG") == "1")
        {
            NativeRenderer.Log("error dialog (test, not shown): " + text.Replace("\n", " | "));
            return DialogResult.Cancel;
        }
        return DarkBox.Show(text, Caption, MessageBoxIcon.Error, Buttons(gpu, logFile));
    }

    /// <summary>Rebuild (primary), driver page, report (only once there is somewhere to report to), show the log, close.</summary>
    public static DarkBox.BoxButton[] Buttons(string? gpu, string logFile)
    {
        var b = new List<DarkBox.BoxButton>
        {
            new("Rebuild shader cache", DialogResult.Retry),
            new("Update GPU driver", DialogResult.None, () => Open(DriverUrl(gpu))),
        };
        if (ReportUrl is { } url) b.Add(new("Report a problem", DialogResult.None, () => Open(url)));
        b.Add(new("Show log", DialogResult.None, () => ShowFile(logFile)));
        b.Add(new("Close", DialogResult.Cancel));
        return b.ToArray();
    }

    /// <summary>The card maker's driver page; Windows Update when the maker isn't known.</summary>
    public static string DriverUrl(string? gpu)
    {
        var g = gpu ?? "";
        if (g.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase) || g.Contains("GeForce", StringComparison.OrdinalIgnoreCase))
            return "https://www.nvidia.com/en-us/drivers/";
        if (g.Contains("AMD", StringComparison.OrdinalIgnoreCase) || g.Contains("Radeon", StringComparison.OrdinalIgnoreCase))
            return "https://www.amd.com/en/support/download/drivers.html";
        if (g.Contains("Intel", StringComparison.OrdinalIgnoreCase))
            return "https://www.intel.com/content/www/us/en/support/detect.html";
        return "ms-settings:windowsupdate";
    }

    static string? FirstGpu()
    {
        try { return Gpu.AdapterNames().FirstOrDefault(); } catch { return null; }
    }

    static void Open(string url) => Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });

    static void ShowFile(string path)
    {
        if (File.Exists(path)) Process.Start(new ProcessStartInfo("explorer.exe", $"/select,\"{path}\"") { UseShellExecute = true });
        else Ui.OpenFolder(Paths.Root);
    }
}
