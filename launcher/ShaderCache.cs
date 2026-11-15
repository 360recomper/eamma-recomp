using System.ComponentModel;
using System.Diagnostics;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

namespace EAMMALauncher;

/// <summary>The native renderer's shader cache, built on the player's machine from their own disc files
/// (NATIVE-FINISH-DESIGN.md WP7: no Python, no xex decrypt). The same three steps as the developer tools:
///   1. extract (here, in C#; tools/eamma_shader_extract.py): every XDK shader container in
///      data\xenon-gen\bigs\shader_big.ast, a BGFA big of zlib-compressed RNAFX effects;
///   2. translate: XenosRecomp.exe turns them into shader_cache.bin (DXIL);
///   3. link: eamma_prelink.exe writes shader_cache_linked.bin (every spec-constant variant).
/// XenosRecomp.exe, eamma_prelink.exe, dxcompiler.dll and dxil.dll ship beside eamma.exe (the game build puts them
/// there); XenosRecomp's shader_common.h is embedded in this launcher (EAMMALauncher.csproj). The game reads the two
/// files from where the cvar eamma_shader_cache points; a relative path means beside the exe (guest_shaders.cpp).
/// Everything is built in &lt;cache folder&gt;\shader_cache.tmp and moved into place at the end, so a failed run leaves
/// an older cache alone. Only one build runs at a time in this launcher: a second caller waits for the first.</summary>
public static class ShaderCache
{
    public const string TranslateTool = "XenosRecomp.exe", LinkTool = "eamma_prelink.exe";
    /// <summary>Inside the game files.</summary>
    public static readonly string ShaderBig = Path.Combine("data", "xenon-gen", "bigs", "shader_big.ast");
    /// <summary>Bump when Extract changes what it writes: caches built by an older launcher are then rebuilt.</summary>
    const string ExtractVersion = "extract 1";
    // Takes seconds on the machines tried. This only stops a stuck tool from holding up Play forever.
    static readonly TimeSpan Limit = TimeSpan.FromMinutes(15);

    /// <summary>Failure codes, the same numbers as UFC's ufc3_shader_cache.exe exit codes.</summary>
    public const int GameFiles = 2, Output = 3, Translate = 4, Link = 5;

    static readonly object gate = new();
    static Process? running;

    static ShaderCache()
    {
        // The launcher closing mid-build takes the tool with it.
        AppDomain.CurrentDomain.ProcessExit += (_, _) =>
        {
            try { running?.Kill(true); } catch { }
        };
    }

    /// <summary>ExitCode: one of the failure codes above when a step failed (null otherwise).</summary>
    public sealed record Result(bool Ok, string Message, int? ExitCode = null);

    sealed class Failure : Exception
    {
        public int Code { get; }
        public Failure(int code, string message) : base(message) => Code = code;
    }

    public static string CacheFile
    {
        get
        {
            var set = new Toml().Get("eamma_shader_cache");
            return Path.GetFullPath(string.IsNullOrWhiteSpace(set) ? "shader_cache.bin" : set, Paths.Root);
        }
    }

    public static string LinkedFor(string cache) =>
        Path.Combine(Path.GetDirectoryName(cache)!, Path.GetFileNameWithoutExtension(cache) + "_linked" + Path.GetExtension(cache));

    public static string LinkedFile => LinkedFor(CacheFile);

    /// <summary>True when both files are there. Whether they belong together is the game's check (it refuses a
    /// pair that doesn't match).</summary>
    public static bool Present
    {
        get
        {
            try
            {
                var a = new FileInfo(CacheFile); var b = new FileInfo(LinkedFile);
                return a.Exists && a.Length > 0 && b.Exists && b.Length > 0;
            }
            catch { return false; }
        }
    }

    /// <summary>Beside the cache: the hash of what built it, written after a good build.</summary>
    public static string StampFile => Path.ChangeExtension(CacheFile, ".stamp");

    static byte[] ShaderCommon()
    {
        using var s = typeof(ShaderCache).Assembly.GetManifestResourceStream("shader_common.h")
            ?? throw new Failure(Translate, "shader_common.h is not embedded in this launcher (launcher build problem).");
        using var m = new MemoryStream();
        s.CopyTo(m);
        return m.ToArray();
    }

    /// <summary>What makes the cache: SHA-256 of each tool in the game folder ("-" when missing) and of the embedded
    /// shader_common.h, plus the extract version, one line each. An update that changes any of them changes the shaders.</summary>
    static string ToolStamp(string toolDir)
    {
        var sb = new StringBuilder();
        foreach (var n in new[] { TranslateTool, LinkTool })
        {
            string h;
            try { using var s = File.OpenRead(Path.Combine(toolDir, n)); h = Convert.ToHexString(SHA256.HashData(s)); }
            catch { h = "-"; }
            sb.Append(n).Append(' ').Append(h).Append('\n');
        }
        string c;
        try { c = Convert.ToHexString(SHA256.HashData(ShaderCommon())); } catch { c = "-"; }
        sb.Append("shader_common.h ").Append(c).Append('\n').Append(ExtractVersion).Append('\n');
        return sb.ToString();
    }

    /// <summary>Present and built by the tools in the game folder now. A cache from before an update (or with no
    /// stamp) is rebuilt.</summary>
    public static bool Current
    {
        get
        {
            if (!Present) return false;
            try { return File.ReadAllText(StampFile) == ToolStamp(Paths.Root); }
            catch { return false; }
        }
    }

    /// <summary>Builds the cache unless a current one is already there.</summary>
    public static Result EnsurePresent(string gameData, IProgress<string>? progress = null, CancellationToken ct = default)
    {
        lock (gate)
        {
            if (Current) return new Result(true, "The shader cache is already there.");
            if (Present) NativeRenderer.Log("shader cache: built by other tools than the ones here (update?), rebuilding");
            return Build(gameData, progress, ct);
        }
    }

    /// <summary>Builds into the game's cache location and waits. A failure is returned, not thrown; only cancelling
    /// throws.</summary>
    public static Result Build(string gameData, IProgress<string>? progress = null, CancellationToken ct = default) =>
        BuildTo(gameData, CacheFile, Paths.Root, progress, ct);

    /// <summary>The whole build with every path given (tests: --shader-cache --out). toolDir holds XenosRecomp.exe,
    /// eamma_prelink.exe and the two DXC DLLs.</summary>
    public static Result BuildTo(string gameData, string cacheFile, string toolDir, IProgress<string>? progress = null, CancellationToken ct = default)
    {
        lock (gate)
        {
            NativeRenderer.Log($"shader cache build: \"{gameData}\" -> \"{cacheFile}\"");
            Result r;
            try { r = Run(gameData, Path.GetFullPath(cacheFile), toolDir, progress, ct); }
            catch (Failure f) { r = new Result(false, f.Message, f.Code); }
            catch (OperationCanceledException) { throw; }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                r = new Result(false, "The shader cache folder can't be written: " + ex.Message, Output);
            }
            NativeRenderer.Log(r.Ok ? "shader cache build: done" : "shader cache build FAILED: " + r.Message);
            return r;
        }
    }

    // ---- step 1: extract (tools/eamma_shader_extract.py, line for line) ----

    public sealed record Container(string Stem, byte[] Data, List<string> Sources);
    public sealed record ShaderSet(List<Container> Containers, int Effects, int Total, string Summary);

    static int LE(byte[] d, int o, int n) { int v = 0; for (int i = n - 1; i >= 0; i--) v = v << 8 | d[o + i]; return v; }
    static uint BE32(byte[] d, int o) => (uint)(d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]);

    /// <summary>One zlib stream starting at off (at most 1 MB of input, as the Python tool reads). A stream cut short
    /// gives what could be unpacked, like zlib's decompressobj.</summary>
    static byte[] Inflate(byte[] d, int off)
    {
        using var src = new MemoryStream(d, off, Math.Min(0x100000, d.Length - off), false);
        using var z = new ZLibStream(src, CompressionMode.Decompress);
        using var o = new MemoryStream();
        var buf = new byte[1 << 16];
        try { int n; while ((n = z.Read(buf, 0, buf.Length)) > 0) o.Write(buf, 0, n); }
        catch (InvalidDataException) { }
        return o.ToArray();
    }

    /// <summary>Every unique shader container of shader_big.ast, named sha1[:16], sorted by sha1.</summary>
    public static ShaderSet Extract(string gameData)
    {
        var path = Path.Combine(gameData, ShaderBig);
        byte[] big;
        try { big = File.ReadAllBytes(path); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new Failure(GameFiles, $"The game files in {gameData} are missing or can't be read ({ShaderBig}: {ex.Message}).");
        }
        if (big.Length < 0x44 || big[0] != 'B' || big[1] != 'G' || big[2] != 'F' || big[3] != 'A')
            throw new Failure(GameFiles, $"{path} is not the game's shader file (no BGFA header).");

        const int EntrySize = 77;
        int count = LE(big, 12, 4);
        if (count <= 0 || 0x44 + (long)count * EntrySize > big.Length)
            throw new Failure(GameFiles, $"{path} is damaged (entry table).");

        var uniq = new Dictionary<string, Container>();   // by sha1 of the container
        int effects = 0, total = 0;
        for (int i = 0; i < count; i++)
        {
            int e = 0x44 + i * EntrySize;
            int off = LE(big, e + 9, 3) * 8;
            int nameEnd = Array.IndexOf(big, (byte)0, e + 17, EntrySize - 17);
            var name = Encoding.ASCII.GetString(big, e + 17, (nameEnd < 0 ? e + EntrySize : nameEnd) - (e + 17));
            if (off >= big.Length) throw new Failure(GameFiles, $"{path} is damaged (effect {name}).");
            var blob = Inflate(big, off);
            effects++;
            // Containers: 4-aligned, header 10 2A 11 xx, then big-endian virtual + physical size (whole container).
            int p = 0;
            while (true)
            {
                p = Find(blob, p);
                if (p < 0 || p + 36 > blob.Length) break;
                if (p % 4 == 0)
                {
                    long size = (long)BE32(blob, p + 4) + BE32(blob, p + 8);
                    if (size > 36 && size <= Math.Min(blob.Length - p, 1 << 20))
                    {
                        var c = blob.AsSpan(p, (int)size).ToArray();
                        var sha = Convert.ToHexString(SHA1.HashData(c)).ToLowerInvariant();
                        total++;
                        if (!uniq.TryGetValue(sha, out var u)) uniq[sha] = u = new Container(sha[..16], c, new List<string>());
                        u.Sources.Add($"{name}@{p:X}");
                        p += (int)size;
                        continue;
                    }
                }
                p++;
            }
        }
        if (uniq.Count == 0) throw new Failure(GameFiles, $"{path} holds no shaders.");

        var sorted = uniq.OrderBy(k => k.Key, StringComparer.Ordinal).Select(k => k.Value).ToList();
        using var whole = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var kinds = new SortedDictionary<string, int>(StringComparer.Ordinal);
        foreach (var c in sorted)
        {
            whole.AppendData(c.Data);
            var k = Convert.ToHexString(c.Data, 0, 4);
            kinds[k] = kinds.GetValueOrDefault(k) + 1;
        }
        // The same line as tools/eamma_shader_extract.py prints (the same disc gives the same set hash).
        var summary = $"{effects} effects, {total} containers, {sorted.Count} unique " +
            $"({string.Join(", ", kinds.Select(k => $"{k.Key} x{k.Value}"))}); set {Convert.ToHexString(whole.GetHashAndReset()).ToLowerInvariant()[..16]}";
        return new ShaderSet(sorted, effects, total, summary);
    }

    static int Find(byte[] d, int from)
    {
        ReadOnlySpan<byte> magic = stackalloc byte[] { 0x10, 0x2A, 0x11 };
        if (from >= d.Length) return -1;
        int i = d.AsSpan(from).IndexOf(magic);
        return i < 0 ? -1 : from + i;
    }

    /// <summary>The set as files: &lt;stem&gt;.bin each, plus _sources.txt ("&lt;stem&gt;.bin effect@offset ...").</summary>
    public static void WriteSet(ShaderSet set, string dir)
    {
        Directory.CreateDirectory(dir);
        var src = new StringBuilder();
        foreach (var c in set.Containers)
        {
            File.WriteAllBytes(Path.Combine(dir, c.Stem + ".bin"), c.Data);
            src.Append(c.Stem).Append(".bin ").Append(string.Join(' ', c.Sources)).Append("\r\n");
        }
        File.WriteAllText(Path.Combine(dir, "_sources.txt"), src.ToString(), new UTF8Encoding(false));
    }

    // ---- steps 2-4 ----

    static void RequireTool(string exe, int code)
    {
        if (!File.Exists(exe)) throw new Failure(code, $"{Path.GetFileName(exe)} is not in the game folder ({Path.GetDirectoryName(exe)}).");
        foreach (var dll in new[] { "dxcompiler.dll", "dxil.dll" })
            if (!File.Exists(Path.Combine(Path.GetDirectoryName(exe)!, dll)))
                throw new Failure(code, $"{dll} is not beside {Path.GetFileName(exe)} ({Path.GetDirectoryName(exe)}).");
    }

    static Result Run(string gameData, string cacheFile, string toolDir, IProgress<string>? progress, CancellationToken ct)
    {
        void Say(string s) { NativeRenderer.Log("  " + s); progress?.Report(s); }
        var sw = Stopwatch.StartNew();
        if (!Directory.Exists(gameData)) throw new Failure(GameFiles, $"The game files folder {gameData} was not found.");
        string xr = Path.Combine(toolDir, TranslateTool), pl = Path.Combine(toolDir, LinkTool);
        RequireTool(xr, Translate);
        RequireTool(pl, Link);
        var common = ShaderCommon();

        var outDir = Path.GetDirectoryName(cacheFile)!;
        Directory.CreateDirectory(outDir);
        // One build per output folder at a time (two launchers).
        FileStream lockFile;
        try { lockFile = new FileStream(Path.Combine(outDir, "shader_cache.lock"), FileMode.Create, FileAccess.Write, FileShare.None, 1, FileOptions.DeleteOnClose); }
        catch (IOException) { throw new Failure(Output, $"Another shader cache build is running in {outDir}, or the folder can't be written."); }
        catch (UnauthorizedAccessException) { throw new Failure(Output, $"The folder {outDir} can't be written (is the game inside Program Files?)."); }
        using var _ = lockFile;

        // 1. Extract.
        var set = Extract(gameData);
        Say(set.Summary);
        var work = Path.Combine(outDir, "shader_cache.tmp");
        if (Directory.Exists(work)) Directory.Delete(work, true);
        WriteSet(set, Path.Combine(work, "in"));
        File.WriteAllBytes(Path.Combine(work, "shader_common.h"), common);
        double tExtract = sw.Elapsed.TotalSeconds;
        ct.ThrowIfCancellationRequested();

        // 2. Translate to DXIL. XenosRecomp lists the shaders it can't translate on stderr and still returns 0; the
        // game logs those as misses if it ever asks for one. Relative arguments: it takes ANSI argv, and the work
        // folder may have a non-ASCII path.
        Say($"translating {set.Containers.Count} shaders...");
        var t = RunChild(xr, new[] { "in", "shader_cache.bin", "shader_common.h" }, work, ct);
        if (t.TimedOut) throw new Failure(Translate, $"{TranslateTool} was still running after {Limit.TotalMinutes:0} minutes and was stopped.");
        if (t.Code != 0) throw new Failure(Translate, $"{TranslateTool}, which translates the shaders, failed (exit code 0x{t.Code:X}).\n{Tail(t.Err)}");
        var tmpCache = Path.Combine(work, "shader_cache.bin");
        if (!File.Exists(tmpCache)) throw new Failure(Translate, $"{TranslateTool} wrote no shader cache.\n{Tail(t.Err)}");
        uint shaders = CheckCacheFile(tmpCache);
        if (t.Out.LastOrDefault(l => l.Length > 0) is { } last) Say(last);

        // The failure list, in nr_build_shader_cache.py's format: "<name> <reason> [<sources>]".
        var failures = new StringBuilder();
        int failed = 0;
        foreach (var line in t.Err)
        {
            int s = line.IndexOf("source=", StringComparison.Ordinal);
            if (s < 0) continue;
            int r = line.IndexOf(" reason=", s, StringComparison.Ordinal);
            if (r < 0) continue;
            var name = line[(s + 7)..r];
            var from = set.Containers.FirstOrDefault(c => c.Stem == name);
            failures.Append(name).Append(' ').Append(line[(r + 8)..]).Append(" [")
                .Append(from == null ? "?" : string.Join(' ', from.Sources)).Append("]\r\n");
            failed++;
        }
        Say($"failed {failed} (list: {Path.GetFileName(cacheFile)}.failures.txt)");
        double tTranslate = sw.Elapsed.TotalSeconds - tExtract;
        ct.ThrowIfCancellationRequested();

        // 3. Pre-link the spec-constant variants.
        Say("linking shader variants...");
        var k = RunChild(pl, new[] { "shader_cache.bin", "shader_cache_linked.bin" }, work, ct);
        if (k.TimedOut) throw new Failure(Link, $"{LinkTool} was still running after {Limit.TotalMinutes:0} minutes and was stopped.");
        if (k.Code != 0) throw new Failure(Link, $"{LinkTool}, which links the shaders, failed (exit code 0x{k.Code:X}).\n{Tail(k.Err)}");
        var tmpLinked = Path.Combine(work, "shader_cache_linked.bin");
        if (!File.Exists(tmpLinked) || new FileInfo(tmpLinked).Length == 0)
            throw new Failure(Link, $"{LinkTool} wrote no linked shader cache.\n{Tail(k.Err)}");
        foreach (var l in k.Out.Where(l => l.Length > 0)) Say(l);
        double tLink = sw.Elapsed.TotalSeconds - tExtract - tTranslate;

        // 4. Move into place. The linked file names the cache it was made from (xxh3) and the game refuses a pair that
        // doesn't match, so stopping between the moves does no harm.
        try
        {
            File.WriteAllText(cacheFile + ".failures.txt", failures.ToString(), new UTF8Encoding(false));
            File.Move(tmpLinked, LinkedFor(cacheFile), true);
            File.Move(tmpCache, cacheFile, true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new Failure(Output, $"The shader cache can't be written to {outDir}: {ex.Message} Is the game running, or the folder read-only?");
        }
        try { Directory.Delete(work, true); } catch { }
        // Without a stamp the next Play only builds it again.
        try { File.WriteAllText(Path.ChangeExtension(cacheFile, ".stamp"), ToolStamp(toolDir)); }
        catch (Exception ex) { NativeRenderer.Log("shader cache: could not write the stamp: " + ex.Message); }
        Say($"shader cache ready: {shaders} shaders in {outDir}");
        Say($"time: extract {tExtract:0.0} s, translate {tTranslate:0.0} s, link {tLink:0.0} s, total {sw.Elapsed.TotalSeconds:0.0} s");
        return new Result(true, "Shader cache built.");
    }

    /// <summary>shader_cache.bin as XenosRecomp writes it (third_party/XenosRecomp/XenosRecomp/main.cpp): 8-byte magic,
    /// version 1, entry count, compressed and plain DXIL sizes, 24-byte entries, the compressed DXIL.</summary>
    static uint CheckCacheFile(string file)
    {
        var len = new FileInfo(file).Length;
        var h = new byte[32];
        using (var f = File.OpenRead(file)) { if (f.Read(h, 0, 32) != 32) throw new Failure(Translate, $"{TranslateTool} wrote no shader cache."); }
        uint version = BitConverter.ToUInt32(h, 8), count = BitConverter.ToUInt32(h, 12);
        ulong packed = BitConverter.ToUInt64(h, 16);
        if (Encoding.ASCII.GetString(h, 0, 8) != "UFCSHC01" || version != 1 || count == 0 || (ulong)len != 32 + (ulong)count * 24 + packed)
            throw new Failure(Translate, $"{TranslateTool} wrote a shader cache that does not check out.");
        return count;
    }

    sealed record Child(int Code, List<string> Out, List<string> Err, bool TimedOut);

    static string Tail(List<string> lines) => string.Join("\n", lines.Where(l => l.Length > 0).TakeLast(6));

    static Child RunChild(string exe, string[] args, string cwd, CancellationToken ct)
    {
        var psi = new ProcessStartInfo(exe)
        {
            WorkingDirectory = cwd,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };
        foreach (var a in args) psi.ArgumentList.Add(a);
        // Developer aids of XenosRecomp: never from a player's (or this PC's) environment.
        psi.Environment.Remove("XENOS_RECOMP_HLSL_OVERRIDE");
        psi.Environment.Remove("XENOS_RECOMP_HLSL_DUMP");

        var outLines = new List<string>(); var errLines = new List<string>();
        using var p = new Process { StartInfo = psi };
        p.OutputDataReceived += (_, e) => { if (e.Data != null) lock (outLines) outLines.Add(e.Data); };
        p.ErrorDataReceived += (_, e) => { if (e.Data != null) lock (errLines) errLines.Add(e.Data); };
        try { p.Start(); }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException)
        {
            throw new Failure(Path.GetFileName(exe) == LinkTool ? Link : Translate, $"{Path.GetFileName(exe)} could not start: {ex.Message}");
        }
        running = p;
        p.BeginOutputReadLine();
        p.BeginErrorReadLine();
        bool timedOut = false;
        try
        {
            var sw = Stopwatch.StartNew();
            while (!p.WaitForExit(200))
            {
                if (ct.IsCancellationRequested) { Stop(p); ct.ThrowIfCancellationRequested(); }
                if (sw.Elapsed > Limit) { Stop(p); timedOut = true; break; }
            }
            if (!timedOut) p.WaitForExit(); // the last output lines
        }
        finally { running = null; }
        foreach (var l in errLines.Where(l => l.Contains("error", StringComparison.OrdinalIgnoreCase)).Take(20)) NativeRenderer.Log("  " + l);
        return new Child(timedOut ? -1 : p.ExitCode, outLines, errLines, timedOut);
    }

    static void Stop(Process p)
    {
        try { p.Kill(true); p.WaitForExit(5000); } catch { }
    }
}
