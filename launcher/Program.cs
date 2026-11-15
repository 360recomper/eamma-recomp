using EAMMALauncher.Tabs;

namespace EAMMALauncher;

static class Program
{
    /// <summary>
    ///   (no args)              launcher window
    ///   --update [--check-only] [--force] [--zip f --sha h --tag t]   headless update; log in update-backups\cli-log.txt
    ///                          (while Updater.Enabled is false only the local --zip form works; nothing asks the network)
    ///   --setup-iso &lt;iso&gt; [--dest dir]   headless disc extract (default dest: assets\); log in setup-cli-log.txt
    ///   --setup-dlc &lt;zip|pkg|folder&gt; [--userdata dir]   headless DLC install (unverified for this game, Setup.DlcUi)
    ///   --updated              (internal) started by the launcher after it replaced itself
    ///   --shader-cache [--dest dir] [--out file] [--tools dir]   headless shader cache build, the step after --setup-iso
    ///                          (tests). --out: write shader_cache.bin there instead of the game's place; --tools: where
    ///                          XenosRecomp.exe / eamma_prelink.exe are (default: beside the launcher). Log in setup-cli-log.txt
    ///   --shader-extract &lt;dir&gt; [--dest dir]   only step 1: the shader containers as files (tests; compare with
    ///                          tools/eamma_shader_extract.py)
    ///   --native               start the game as Play does (tests): builds a missing shader cache; if that fails, or the game
    ///                          exits with 3/4 (renderer can't start), shows the renderer error dialog. Never falls back.
    ///   --screenshot-all &lt;dir&gt; [percent]   write a PNG of every tab at that display scale, never starts the game (tests)
    ///   --cpu-check            which game exe this CPU gets; writes cpu-check.txt
    ///   --log-dir &lt;dir&gt;        with the headless options: write their log files there instead of beside the launcher
    /// </summary>
    [STAThread]
    static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        int si = Array.IndexOf(args, "--screenshot-all");
        int? simPct = si >= 0 && si + 2 < args.Length && int.TryParse(args[si + 2], out var sp) ? sp : null;
        Theme.Init(simPct);
        if (si >= 0 && si + 1 < args.Length) { Environment.Exit(Shots.Run(args[si + 1], simPct ?? 100)); return; }
        if (args.Contains("--setup-iso") || args.Contains("--setup-dlc") || args.Contains("--shader-cache") || args.Contains("--shader-extract"))
        { Environment.Exit(SetupCli(args)); return; }
        if (args.Contains("--cpu-check")) { File.WriteAllText(Path.Combine(Paths.Root, "cpu-check.txt"), Cpu.Describe() + Environment.NewLine); Environment.Exit(0); return; }
        if (args.Length > 0 && args[0] == "--update") { Environment.Exit(Updater.Cli(args)); return; }
        if (args.Contains("--native"))
        {
            try
            {
                bool force = false;
                while (true)
                {
                    if (NativeRenderer.PrepareCache(force) is { } failed)
                    {
                        if (RendererError.ShowCacheFailure(failed) != DialogResult.Retry) return;
                        force = true; continue;
                    }
                    int code = NativeRenderer.Wait(NativeRenderer.Launch());
                    if (code != NativeRenderer.DeviceFailedExitCode && code != NativeRenderer.CacheMissingExitCode) return;
                    if (RendererError.ShowGameFailure(code) != DialogResult.Retry) return;
                    force = true;
                }
            }
            catch (Exception ex) { DarkBox.Show("Could not start the game: " + ex.Message, Branding.LauncherTitle); }
            return;
        }
        Application.Run(new MainForm(args.Contains("--updated")));
    }

    static string? Arg(string[] a, string name) { int i = Array.IndexOf(a, name); return i >= 0 && i + 1 < a.Length ? a[i + 1] : null; }

    static int SetupCli(string[] args)
    {
        var log = new List<string> { $"{DateTime.Now:s} {string.Join(' ', args)}" };
        int code = 0;
        var sw = System.Diagnostics.Stopwatch.StartNew();
        try
        {
            var none = new Progress<(string, double)>(_ => { });
            if (Arg(args, "--setup-iso") is { } iso)
            {
                var dest = Arg(args, "--dest") ?? Paths.Assets;
                Setup.ImportIso(iso, dest, none, CancellationToken.None);
                log.Add($"iso extracted to {dest} in {sw.Elapsed.TotalSeconds:0}s");
                log.Add(Setup.FinishImport(dest, none, CancellationToken.None));
            }
            else if (Arg(args, "--shader-extract") is { } dir)
            {
                var set = ShaderCache.Extract(Arg(args, "--dest") ?? Paths.Assets);
                ShaderCache.WriteSet(set, dir);
                log.Add(set.Summary);
            }
            else if (args.Contains("--shader-cache"))
            {
                var data = Arg(args, "--dest") ?? Paths.Assets;
                if (Arg(args, "--out") is { } outFile)
                {
                    // Synchronous progress: these lines must be in the log before the process exits.
                    var lines = new List<string>();
                    var r = ShaderCache.BuildTo(data, outFile, Arg(args, "--tools") ?? Paths.Root, new Sync(lines.Add));
                    log.AddRange(lines);
                    if (!r.Ok) throw new InvalidOperationException(r.Message + (r.ExitCode is int c ? $" (step code {c})" : ""));
                    log.Add(r.Message);
                }
                else log.Add(Setup.FinishImport(data, none, CancellationToken.None));
            }
            if (Arg(args, "--setup-dlc") is { } dlc)
            {
                var ud = Arg(args, "--userdata") ?? Paths.UserData;
                log.AddRange(Setup.ImportDlc(dlc, ud, none, CancellationToken.None));
            }
            log.Add("OK");
        }
        catch (Exception ex) { log.Add("FAILED: " + ex.Message); code = 1; }
        File.AppendAllLines(Path.Combine(Arg(args, "--log-dir") ?? Paths.Root, "setup-cli-log.txt"), log);
        return code;
    }

    sealed class Sync : IProgress<string>
    {
        readonly Action<string> a;
        public Sync(Action<string> a) => this.a = a;
        public void Report(string value) => a(value);
    }
}

class MainForm : Form
{
    readonly List<ILauncherTab> tabs = new();
    readonly NavHost tc = new();
    readonly bool preview;
    const string Title = Branding.ProductName + " - Launcher " + AppVersion.Launcher;

    /// <summary>--screenshot-all: never take the focus from the user.</summary>
    protected override bool ShowWithoutActivation => preview;

    internal NavHost Nav => tc;
    internal IReadOnlyList<ILauncherTab> Tabs => tabs;

    /// <param name="preview">--screenshot-all: no update check, no messages.</param>
    public MainForm(bool updated = false, bool preview = false)
    {
        this.preview = preview;
        Text = Title;
        AutoScaleMode = AutoScaleMode.None;   // sizes are scaled by Theme.S
        Theme.DarkWindow(this);
        ClientSize = new Size(Theme.S(940), Theme.S(680));
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = SizeFromClientSize(new Size(Theme.S(780), Theme.S(540)));

        var play = new PlayTab();
        var setup = new SetupTab();
        // Mod packs (WP11) and the music tool (WP12) are not ported yet: add their tabs here when they are.
        tabs.AddRange(new ILauncherTab[] { play, setup, new SettingsTab() });
        play.UpdateFound += r => Text = $"{Title}  (update available: v{r.Version}, see Play)";
        play.Updated += () => Text = Title;
        foreach (var t in tabs) tc.Add(t.Title, Glyph(t.Title), t.View, t is not PlayTab);
        tc.SelectedIndexChanged += (_, _) => tabs[tc.SelectedIndex].OnShown();
        Controls.Add(tc);
        Theme.DarkScroll(this);
        Shown += (_, _) =>
        {
            if (preview) return;
            tc.SelectedIndex = Paths.AssetsReady ? 0 : tabs.IndexOf(setup);   // raises OnShown
            if (updated) DarkBox.Show($"Updated to v{AppVersion.Installed}. The replaced files are in the update-backups folder.", Branding.LauncherTitle);
#pragma warning disable CS0162 // unreachable while the updater is off: no network call at startup
            else if (Updater.Enabled && Paths.IsInstalled && Config.Load().CheckForUpdates) _ = play.CheckForUpdates(false);
#pragma warning restore CS0162
        };
    }

    /// <summary>Sidebar icons (Segoe Fluent Icons / MDL2 Assets, both ship with Windows).</summary>
    static string Glyph(string title) => title switch
    {
        "Play" => "",
        "Setup" => "",
        "Settings" => "",
        _ => "",
    };

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (keyData == (Keys.Control | Keys.Tab)) { tc.Next(1); return true; }
        if (keyData == (Keys.Control | Keys.Shift | Keys.Tab)) { tc.Next(-1); return true; }
        return base.ProcessCmdKey(ref msg, keyData);
    }
}
