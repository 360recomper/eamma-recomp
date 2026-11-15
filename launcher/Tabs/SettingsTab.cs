using System.Globalization;

namespace EAMMALauncher.Tabs;

/// <summary>The game options that live in eamma.toml. Each change rewrites only its own line. Only options this game
/// really has (cvars that exist in the EA MMA build): eamma_native_scale (src/native/ufc_resources.cpp),
/// eamma_native_adapter (src/reblue/gpu/device.cpp), eamma_native_upscale (src/reblue/gpu/present.cpp) and the SDK's
/// fullscreen / pause_on_focus_loss (ui/window.cpp). A fight frame-rate row comes when WP1 allows one; UFC's photo
/// mode, gameplay, unlock, roster and music rows have no EA MMA counterpart.</summary>
public class SettingsTab : ILauncherTab
{
    readonly FlowLayoutPanel col = Ui.Column();
    readonly Label status = Ui.Text("");
    bool loading;

    readonly DarkCombo scale = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = Theme.S(260) };
    // Graphics card. gpuValues[i] is what item i writes ("" = Automatic).
    readonly DarkCombo gpu = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = Theme.S(260) };
    readonly List<string> gpuValues = new();
    readonly Dictionary<string, DarkCheck> checks = new();
    readonly Dictionary<string, (DarkCombo box, string[] values, string def)> choices = new();

    public string Title => "Settings";
    public Control View => col;

    /// <summary>eamma_native_scale: a multiple of 1280x720 in steps of 0.25 up to 4; 0 (the cvar's default) = the SDK's
    /// resolution_scale, which is 1.</summary>
    public const string ScaleKey = "eamma_native_scale";
    static readonly string[] ScaleNames = { "1x (720p, original)", "1.5x (1080p)", "2x (1440p)", "3x (4K)" };
    static readonly double[] ScaleValues = { 1.0, 1.5, 2.0, 3.0 };

    public SettingsTab()
    {
        col.Controls.Add(Ui.Head("Game settings"));
        col.Controls.Add(Ui.Text("Saved to eamma.toml next to the game. Other settings in that file are left alone. Takes effect the next time you start the game."));

        col.Controls.Add(Ui.Head("Graphics"));
        scale.Items.AddRange(ScaleNames);
        scale.SelectedIndexChanged += (_, _) =>
        {
            if (loading || scale.SelectedIndex < 0) return;
            var v = ScaleValues[scale.SelectedIndex].ToString("0.0#", CultureInfo.InvariantCulture);
            Write(t => t.Set(ScaleKey, v));
        };
        col.Controls.Add(Row("Render scale", scale));
        col.Controls.Add(Hint("The resolution the game is drawn at, before it is fitted to your window. Higher looks sharper and needs a faster graphics card."));
        // Native-only: no Renderer or Graphics backend rows; the launcher always starts the Direct3D 12 renderer.
        gpu.SelectedIndexChanged += (_, _) =>
        {
            if (loading || gpu.SelectedIndex < 0) return;
            var v = gpuValues[gpu.SelectedIndex];
            Write(t => t.SetStr(Gpu.TomlKey, v));
        };
        col.Controls.Add(Row("Graphics card", gpu));
        col.Controls.Add(Hint("Automatic picks the dedicated (high-performance) graphics card on laptops that also have built-in graphics. Pick a card by name to override it; Windows' own per-app graphics setting can override Automatic too."));
        col.Controls.Add(Choice("eamma_native_upscale", "Upscaling", new[] { "Automatic (default)", "Bilinear", "AMD FSR 1 always" },
            new[] { "auto", "bilinear", "fsr" }, "auto"));
        col.Controls.Add(Hint("How the picture is fitted to the window. Automatic uses AMD FSR 1 when the picture is smaller than the window (1x on a 1080p or bigger screen) and plain bilinear scaling otherwise. Bilinear never sharpens. AMD FSR 1 always also sharpens a picture that is already as big as the window."));

        col.Controls.Add(Ui.Head("Window"));
        col.Controls.Add(Check("fullscreen", "Fullscreen (borderless)", true, "Off = the game runs in a window."));
        col.Controls.Add(Check("pause_on_focus_loss", "Mute while in the background", false, "Sound stops while another window has focus. The game keeps running either way; the controller is ignored until you come back."));

        col.Controls.Add(status);
    }

    /// <summary>Automatic, then every card Windows lists. A saved name that isn't listed (another PC, a
    /// removed card) is kept as its own item, so opening Settings never changes it.</summary>
    void LoadGpus(Toml t)
    {
        var saved = t.GetStr(Gpu.TomlKey, "");
        gpu.Items.Clear();
        gpuValues.Clear();
        gpu.Items.Add("Automatic (default)");
        gpuValues.Add("");
        foreach (var n in Gpu.AdapterNames()) { gpu.Items.Add(n); gpuValues.Add(n); }
        if (saved.Length > 0 && !gpuValues.Contains(saved)) { gpu.Items.Add(saved + " (not found)"); gpuValues.Add(saved); }
        gpu.SelectedIndex = Math.Max(0, gpuValues.IndexOf(saved));
    }

    /// <summary>The list item for a saved eamma_native_scale: the nearest offered step; 0, missing or unreadable = 1x.</summary>
    public static int ScaleIndex(string? saved)
    {
        if (!double.TryParse(saved, NumberStyles.Float, CultureInfo.InvariantCulture, out var v) || v <= 0) return 0;
        int best = 0;
        for (int i = 1; i < ScaleValues.Length; i++)
            if (Math.Abs(ScaleValues[i] - v) < Math.Abs(ScaleValues[best] - v)) best = i;
        return best;
    }

    static Label Hint(string text)
    {
        var l = Ui.Text(text);
        l.ForeColor = Theme.Muted;
        return l;
    }

    static Control Row(string label, Control c)
    {
        var p = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.LeftToRight, WrapContents = false, Margin = new Padding(0, Theme.S(2), 0, Theme.S(2)) };
        p.Controls.Add(new DimLabel { Text = label, AutoSize = false, Width = Theme.S(150), Height = Theme.S(24), ForeColor = Theme.Ink, TextAlign = ContentAlignment.MiddleLeft });
        p.Controls.Add(c);
        return p;
    }

    Control Check(string key, string text, bool def, string hint)
    {
        var cb = new DarkCheck { Text = text, AutoSize = true, Tag = def };
        cb.CheckedChanged += (_, _) => { if (!loading) Write(t => t.Set(key, cb.Checked)); };
        checks[key] = cb;
        var p = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, Margin = new Padding(0, Theme.S(2), 0, Theme.S(4)) };
        p.Controls.Add(cb);
        var h = Ui.Text(hint); h.ForeColor = Theme.Muted; h.Margin = new Padding(Theme.S(24), 0, 0, 0);
        p.Controls.Add(h);
        return p;
    }

    /// <summary>A list whose items write a string value.</summary>
    Control Choice(string key, string label, string[] names, string[] values, string def)
    {
        var box = new DarkCombo { DropDownStyle = ComboBoxStyle.DropDownList, Width = Theme.S(260) };
        box.Items.AddRange(names);
        box.SelectedIndexChanged += (_, _) =>
        {
            if (loading) return;
            var v = values[box.SelectedIndex];
            Write(t => t.SetStr(key, v));
        };
        choices[key] = (box, values, def);
        return Row(label, box);
    }

    void Write(Action<Toml> edit)
    {
        try
        {
            if (Updater.GameRunning()) { status.Text = "The game is running: changes apply at the next start."; }
            var t = new Toml(); edit(t); t.Save();
            if (!Updater.GameRunning()) status.Text = "Saved.";
        }
        catch (Exception ex) { status.Text = "Could not save eamma.toml: " + ex.Message; }
    }

    public void OnShown()
    {
        loading = true;
        var t = new Toml();
        foreach (var (k, cb) in checks) cb.Checked = t.GetBool(k, (bool)cb.Tag!);
        scale.SelectedIndex = ScaleIndex(t.Get(ScaleKey));
        LoadGpus(t);
        foreach (var (k, c) in choices)
        {
            int i = Array.IndexOf(c.values, t.GetStr(k, c.def));
            c.box.SelectedIndex = i >= 0 ? i : Array.IndexOf(c.values, c.def);
        }
        status.Text = File.Exists(Paths.Toml) ? "" : "eamma.toml does not exist yet: it is created when you change something here.";
        loading = false;
    }
}
